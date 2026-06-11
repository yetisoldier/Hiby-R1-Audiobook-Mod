#!/usr/bin/env python3
"""Small ADB control console for the HiBy R1.

This wraps the framebuffer and Linux input-event tricks used during audiobook
development into one repeatable tool. It is intentionally non-persistent: input
actions write short event streams to /dev/input and screenshots read /dev/fb0.
No firmware files or process memory are modified.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path


DEFAULT_ADB = r"C:\Program Files\Software Fix\adb.exe"
WIDTH = 480
HEIGHT = 800
STRIDE = WIDTH * 2
DEFAULT_REMOTE_DIR = "/usr/data/r1_adb_control"
DEFAULT_TOUCH_EVENT = "event1"
DEFAULT_TOUCH_FRAMES = 8


@dataclass(frozen=True)
class TouchPreset:
    x: int
    y: int
    description: str


@dataclass(frozen=True)
class KeyPreset:
    event: str
    code: int
    description: str


TOUCH_PRESETS: dict[str, TouchPreset] = {
    "main-music": TouchPreset(120, 145, "launcher Music tile"),
    "main-stream": TouchPreset(360, 145, "launcher Stream media tile"),
    "main-wireless": TouchPreset(120, 390, "launcher Wireless tile"),
    "main-audiobooks": TouchPreset(360, 390, "launcher Audiobooks tile"),
    "main-system": TouchPreset(120, 640, "launcher System tile"),
    "main-about": TouchPreset(360, 640, "launcher About tile"),
    "soft-back": TouchPreset(34, 88, "top-left back arrow"),
    "top-left-back": TouchPreset(34, 88, "top-left back arrow"),
    "title-row-1": TouchPreset(240, 234, "visible audiobook title row 1"),
    "title-row-2": TouchPreset(240, 358, "visible audiobook title row 2"),
    "title-row-3": TouchPreset(240, 485, "visible audiobook title row 3"),
    "title-row-4": TouchPreset(240, 611, "visible audiobook title row 4"),
    "title-row-5": TouchPreset(240, 738, "visible audiobook title row 5"),
    "list-row-1": TouchPreset(240, 234, "visible list row 1"),
    "list-row-2": TouchPreset(240, 358, "visible list row 2"),
    "list-row-3": TouchPreset(240, 485, "visible list row 3"),
    "list-row-4": TouchPreset(240, 611, "visible list row 4"),
    "list-row-5": TouchPreset(240, 738, "visible list row 5"),
    "seek-start": TouchPreset(22, 619, "Now Playing seek bar near start"),
    "seek-middle": TouchPreset(240, 619, "Now Playing seek bar middle"),
    "seek-end": TouchPreset(458, 619, "Now Playing seek bar near end"),
}

KEY_PRESETS: dict[str, KeyPreset] = {
    "next": KeyPreset("event0", 163, "physical next"),
    "prev": KeyPreset("event2", 165, "physical previous"),
    "playpause": KeyPreset("event2", 164, "physical play/pause"),
}


def timestamp() -> str:
    return time.strftime("%Y%m%d-%H%M%S")


def quote_remote(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def run(
    args: list[str],
    *,
    check: bool = True,
    text: bool = True,
) -> subprocess.CompletedProcess[str] | subprocess.CompletedProcess[bytes]:
    kwargs: dict[str, object] = {
        "stdout": subprocess.PIPE,
        "stderr": subprocess.STDOUT,
        "check": False,
    }
    if text:
        kwargs.update({"text": True, "encoding": "utf-8", "errors": "replace"})
    proc = subprocess.run(args, **kwargs)
    if check and proc.returncode != 0:
        output = proc.stdout if isinstance(proc.stdout, str) else proc.stdout.decode("utf-8", "replace")
        raise RuntimeError(f"{' '.join(args)} failed with {proc.returncode}\n{output}")
    return proc


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    proc = run([adb, "shell", command], check=check)
    assert isinstance(proc.stdout, str)
    return proc.stdout


def ensure_adb(adb: str) -> None:
    if not Path(adb).exists() and shutil.which(adb) is None:
        raise RuntimeError(f"ADB not found: {adb}")


def check_device(adb: str) -> str:
    proc = run([adb, "devices", "-l"])
    assert isinstance(proc.stdout, str)
    lines = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            lines.append(line)
    if not lines:
        raise RuntimeError(f"No ADB device is connected or authorized.\n{proc.stdout}")
    return proc.stdout


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def rgb565_to_png(raw: bytes) -> bytes:
    if len(raw) < STRIDE * HEIGHT:
        raise ValueError(f"framebuffer capture too short: {len(raw)} bytes")
    rows: list[bytes] = []
    for y in range(HEIGHT):
        row = bytearray()
        offset = y * STRIDE
        row.append(0)
        for x in range(WIDTH):
            value = raw[offset + x * 2] | (raw[offset + x * 2 + 1] << 8)
            r5 = (value >> 11) & 0x1F
            g6 = (value >> 5) & 0x3F
            b5 = value & 0x1F
            row.extend(
                (
                    (r5 << 3) | (r5 >> 2),
                    (g6 << 2) | (g6 >> 4),
                    (b5 << 3) | (b5 >> 2),
                )
            )
        rows.append(bytes(row))
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(b"".join(rows), level=6))
        + png_chunk(b"IEND", b"")
    )


def capture_screenshot(
    adb: str,
    output: Path,
    *,
    raw_output: Path | None = None,
    remote_raw: str = f"{DEFAULT_REMOTE_DIR}/fb0.raw",
) -> tuple[Path, Path]:
    output.parent.mkdir(parents=True, exist_ok=True)
    raw_path = raw_output or output.with_suffix(".raw")
    raw_path.parent.mkdir(parents=True, exist_ok=True)
    remote_dir = remote_raw.rsplit("/", 1)[0]
    adb_shell(adb, f"mkdir -p {quote_remote(remote_dir)}")
    adb_shell(adb, f"rm -f {quote_remote(remote_raw)}; dd if=/dev/fb0 of={quote_remote(remote_raw)} bs={STRIDE} count={HEIGHT}")
    run([adb, "pull", remote_raw, str(raw_path)])
    raw = raw_path.read_bytes()
    output.write_bytes(rgb565_to_png(raw))
    return output, raw_path


def input_event(event_type: int, code: int, value: int) -> bytes:
    return struct.pack("<llHHl", 0, 0, event_type, code, value)


def abs_frame(x: int, y: int, *, include_press: bool) -> list[bytes]:
    events = [
        input_event(3, 57, 0),
        input_event(3, 58, 63),
        input_event(3, 48, 9),
        input_event(3, 53, x),
        input_event(3, 54, y),
        input_event(0, 2, 0),
    ]
    if include_press:
        events.append(input_event(1, 330, 1))
    events.append(input_event(0, 0, 0))
    return events


def tap_stream(x: int, y: int, frames: int) -> bytes:
    frames = max(2, frames)
    events: list[bytes] = []
    events.extend(abs_frame(x, y, include_press=True))
    for _ in range(frames - 1):
        events.extend(abs_frame(x, y, include_press=False))
    events.extend([input_event(1, 330, 0), input_event(0, 2, 0), input_event(0, 0, 0)])
    return b"".join(events)


def drag_stream(x: int, y: int, to_x: int, to_y: int, frames: int) -> bytes:
    frames = max(2, frames)
    events: list[bytes] = []
    events.extend(abs_frame(x, y, include_press=True))
    for step in range(1, frames):
        ratio = step / (frames - 1)
        events.extend(
            abs_frame(
                round(x + (to_x - x) * ratio),
                round(y + (to_y - y) * ratio),
                include_press=False,
            )
        )
    events.extend([input_event(1, 330, 0), input_event(0, 2, 0), input_event(0, 0, 0)])
    return b"".join(events)


def key_stream(code: int) -> bytes:
    return b"".join(
        [
            input_event(1, code, 1),
            input_event(0, 0, 0),
            input_event(1, code, 0),
            input_event(0, 0, 0),
        ]
    )


def validate_point(x: int, y: int) -> None:
    if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
        raise ValueError(f"coordinate outside {WIDTH}x{HEIGHT}: {x},{y}")


def inject_stream(
    adb: str,
    event: str,
    data: bytes,
    *,
    label: str,
    remote_dir: str = DEFAULT_REMOTE_DIR,
    dry_run: bool = False,
) -> Path:
    local_dir = Path("work") / "adb-control" / "input-events"
    local_dir.mkdir(parents=True, exist_ok=True)
    local = local_dir / f"{timestamp()}-{label}-{event}.bin"
    local.write_bytes(data)
    print(f"event:  /dev/input/{event}")
    print(f"stream: {local}")
    print(f"bytes:  {len(data)}")
    if dry_run:
        print("dry_run=true")
        return local
    remote = f"{remote_dir.rstrip('/')}/{local.name}"
    adb_shell(adb, f"mkdir -p {quote_remote(remote_dir)}")
    run([adb, "push", str(local), remote])
    adb_shell(adb, f"cat {quote_remote(remote)} > /dev/input/{event}; rm -f {quote_remote(remote)}", check=False)
    return local


def default_screenshot_path(label: str = "screen") -> Path:
    safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in label).strip("_") or "screen"
    return Path("work") / "adb-control" / "screenshots" / f"{timestamp()}-{safe}.png"


def maybe_after_screenshot(args: argparse.Namespace, label: str) -> None:
    if not getattr(args, "after_screenshot", False):
        return
    delay = getattr(args, "after_delay", 0.8)
    if delay > 0:
        time.sleep(delay)
    output = default_screenshot_path(label)
    capture_screenshot(args.adb, output)
    print(f"after_screenshot: {output}")


def command_devices(args: argparse.Namespace) -> int:
    ensure_adb(args.adb)
    print(check_device(args.adb), end="")
    return 0


def command_screenshot(args: argparse.Namespace) -> int:
    ensure_adb(args.adb)
    check_device(args.adb)
    output = args.output or default_screenshot_path(args.label)
    png, raw = capture_screenshot(args.adb, output, raw_output=args.raw_output, remote_raw=args.remote_raw)
    print(f"raw: {raw} ({raw.stat().st_size} bytes)")
    print(f"png: {png} ({png.stat().st_size} bytes)")
    return 0


def command_tap(args: argparse.Namespace) -> int:
    validate_point(args.x, args.y)
    data = tap_stream(args.x, args.y, args.frames)
    inject_stream(args.adb, args.event, data, label=f"tap-{args.x}-{args.y}", dry_run=args.dry_run)
    maybe_after_screenshot(args, f"tap-{args.x}-{args.y}")
    return 0


def command_drag(args: argparse.Namespace) -> int:
    validate_point(args.x, args.y)
    validate_point(args.to_x, args.to_y)
    data = drag_stream(args.x, args.y, args.to_x, args.to_y, args.frames)
    inject_stream(
        args.adb,
        args.event,
        data,
        label=f"drag-{args.x}-{args.y}-to-{args.to_x}-{args.to_y}",
        dry_run=args.dry_run,
    )
    maybe_after_screenshot(args, f"drag-{args.x}-{args.y}")
    return 0


def command_key(args: argparse.Namespace) -> int:
    if args.name:
        preset = KEY_PRESETS[args.name]
        event = args.event or preset.event
        code = preset.code
        label = f"key-{args.name}"
        print(f"preset: {args.name} ({preset.description})")
    else:
        if args.code is None:
            raise ValueError("key requires either a preset name or --code")
        event = args.event or "event2"
        code = args.code
        label = f"key-{code}"
    inject_stream(args.adb, event, key_stream(code), label=label, dry_run=args.dry_run)
    maybe_after_screenshot(args, label)
    return 0


def command_preset(args: argparse.Namespace) -> int:
    preset = TOUCH_PRESETS[args.name]
    print(f"preset: {args.name} ({preset.description}) at {preset.x},{preset.y}")
    data = tap_stream(preset.x, preset.y, args.frames)
    inject_stream(args.adb, args.event, data, label=f"preset-{args.name}", dry_run=args.dry_run)
    maybe_after_screenshot(args, f"preset-{args.name}")
    return 0


def command_row(args: argparse.Namespace) -> int:
    if not (1 <= args.number <= 5):
        raise ValueError("row number must be 1..5")
    preset_name = f"{args.kind}-row-{args.number}"
    preset = TOUCH_PRESETS[preset_name]
    print(f"row: {args.kind} {args.number} at {preset.x},{preset.y}")
    data = tap_stream(preset.x, preset.y, args.frames)
    inject_stream(args.adb, args.event, data, label=preset_name, dry_run=args.dry_run)
    maybe_after_screenshot(args, preset_name)
    return 0


def command_seek(args: argparse.Namespace) -> int:
    percent = max(0.0, min(100.0, args.percent))
    x_min = 21
    x_max = 459
    x = round(x_min + ((x_max - x_min) * percent / 100.0))
    y = 619
    print(f"seek_tap: {percent:.1f}% at {x},{y}")
    data = tap_stream(x, y, args.frames)
    inject_stream(args.adb, args.event, data, label=f"seek-{int(percent)}pct", dry_run=args.dry_run)
    maybe_after_screenshot(args, f"seek-{int(percent)}pct")
    return 0


def command_macro(args: argparse.Namespace) -> int:
    if args.name == "edge-back":
        start_x, start_y, end_x, end_y = 30, 400, 360, 400
        if args.dry_run:
            print("macro: edge-back")
            print(f"would_drag: {start_x},{start_y} -> {end_x},{end_y}")
            print(f"would_sleep: {args.settle}s")
            return 0
        inject_stream(
            args.adb,
            DEFAULT_TOUCH_EVENT,
            drag_stream(start_x, start_y, end_x, end_y, args.frames),
            label="macro-edge-back",
            dry_run=args.dry_run,
        )
        time.sleep(args.settle)
        if getattr(args, "after_screenshot", False):
            after = default_screenshot_path("after-edge-back")
            capture_screenshot(args.adb, after)
            print(f"after_screenshot: {after}")
        return 0

    if args.name == "open-audiobooks":
        if args.dry_run:
            preset = TOUCH_PRESETS["main-audiobooks"]
            print("macro: open-audiobooks")
            print(f"would_capture: before-open-audiobooks")
            print(f"would_tap: {preset.x},{preset.y} ({preset.description})")
            print(f"would_sleep: {args.settle}s")
            print(f"would_capture: after-open-audiobooks")
            return 0
        before = default_screenshot_path("before-open-audiobooks")
        capture_screenshot(args.adb, before)
        print(f"before_screenshot: {before}")
        preset = TOUCH_PRESETS["main-audiobooks"]
        inject_stream(
            args.adb,
            DEFAULT_TOUCH_EVENT,
            tap_stream(preset.x, preset.y, args.frames),
            label="macro-open-audiobooks",
            dry_run=args.dry_run,
        )
        time.sleep(args.settle)
        after = default_screenshot_path("after-open-audiobooks")
        capture_screenshot(args.adb, after)
        print(f"after_screenshot: {after}")
        return 0

    if args.name == "tap-title-row":
        if args.row is None:
            raise ValueError("macro tap-title-row requires --row")
        if not (1 <= args.row <= 5):
            raise ValueError("--row must be 1..5")
        if args.dry_run:
            preset = TOUCH_PRESETS[f"title-row-{args.row}"]
            print("macro: tap-title-row")
            print(f"would_capture: before-title-row-{args.row}")
            print(f"would_tap: {preset.x},{preset.y} ({preset.description})")
            print(f"would_sleep: {args.settle}s")
            print(f"would_capture: after-title-row-{args.row}")
            return 0
        before = default_screenshot_path(f"before-title-row-{args.row}")
        capture_screenshot(args.adb, before)
        print(f"before_screenshot: {before}")
        preset = TOUCH_PRESETS[f"title-row-{args.row}"]
        inject_stream(
            args.adb,
            DEFAULT_TOUCH_EVENT,
            tap_stream(preset.x, preset.y, args.frames),
            label=f"macro-title-row-{args.row}",
            dry_run=args.dry_run,
        )
        time.sleep(args.settle)
        after = default_screenshot_path(f"after-title-row-{args.row}")
        capture_screenshot(args.adb, after)
        print(f"after_screenshot: {after}")
        return 0

    raise ValueError(f"unknown macro: {args.name}")


def add_input_common(parser: argparse.ArgumentParser, *, frames_default: int = DEFAULT_TOUCH_FRAMES) -> None:
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--event", default=DEFAULT_TOUCH_EVENT)
    parser.add_argument("--frames", type=int, default=frames_default)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--after-screenshot", action="store_true")
    parser.add_argument("--after-delay", type=float, default=0.8)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default=DEFAULT_ADB)
    sub = parser.add_subparsers(dest="command", required=True)

    devices = sub.add_parser("devices", help="show connected ADB devices")
    devices.set_defaults(func=command_devices)

    screenshot = sub.add_parser("screenshot", help="capture /dev/fb0 as PNG")
    screenshot.add_argument("--adb", default=DEFAULT_ADB)
    screenshot.add_argument("--output", type=Path)
    screenshot.add_argument("--raw-output", type=Path)
    screenshot.add_argument("--remote-raw", default=f"{DEFAULT_REMOTE_DIR}/fb0.raw")
    screenshot.add_argument("--label", default="screen")
    screenshot.set_defaults(func=command_screenshot)

    tap = sub.add_parser("tap", help="tap raw screen coordinates")
    add_input_common(tap)
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)
    tap.set_defaults(func=command_tap)

    drag = sub.add_parser("drag", help="drag from one coordinate to another")
    add_input_common(drag, frames_default=18)
    drag.add_argument("x", type=int)
    drag.add_argument("y", type=int)
    drag.add_argument("to_x", type=int)
    drag.add_argument("to_y", type=int)
    drag.set_defaults(func=command_drag)

    key = sub.add_parser("key", help="press a playback key")
    key.add_argument("--adb", default=DEFAULT_ADB)
    key.add_argument("name", nargs="?", choices=sorted(KEY_PRESETS))
    key.add_argument("--event")
    key.add_argument("--code", type=int)
    key.add_argument("--dry-run", action="store_true")
    key.add_argument("--after-screenshot", action="store_true")
    key.add_argument("--after-delay", type=float, default=0.8)
    key.set_defaults(func=command_key)

    preset = sub.add_parser("preset", help="tap a named coordinate preset")
    add_input_common(preset)
    preset.add_argument("name", choices=sorted(TOUCH_PRESETS))
    preset.set_defaults(func=command_preset)

    row = sub.add_parser("row", help="tap a visible list row")
    add_input_common(row)
    row.add_argument("number", type=int)
    row.add_argument("--kind", choices=["title", "list"], default="title")
    row.set_defaults(func=command_row)

    seek = sub.add_parser("seek", help="tap Now Playing seek bar by percent")
    add_input_common(seek)
    seek.add_argument("percent", type=float)
    seek.set_defaults(func=command_seek)

    macro = sub.add_parser("macro", help="run a screenshot-assisted common flow")
    macro.add_argument("--adb", default=DEFAULT_ADB)
    macro.add_argument("name", choices=["edge-back", "open-audiobooks", "tap-title-row"])
    macro.add_argument("--row", type=int)
    macro.add_argument("--settle", type=float, default=2.0)
    macro.add_argument("--frames", type=int, default=DEFAULT_TOUCH_FRAMES)
    macro.add_argument("--dry-run", action="store_true")
    macro.add_argument("--after-screenshot", action="store_true")
    macro.set_defaults(func=command_macro)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        dry_run = bool(getattr(args, "dry_run", False))
        after_screenshot = bool(getattr(args, "after_screenshot", False))
        needs_adb = args.command in {"devices", "screenshot"} or not dry_run or after_screenshot
        if needs_adb:
            ensure_adb(args.adb)
        if args.command != "devices" and needs_adb:
            check_device(args.adb)
        return int(args.func(args) or 0)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
