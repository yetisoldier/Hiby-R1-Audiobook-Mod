#!/usr/bin/env python3
"""Guarded live test for the audiobook UI-seek fallback.

This test intentionally changes the current audiobook playback position. It is
for development sessions where the R1 is already playing an audiobook and the
resume daemon is installed under /usr/data/audiobooks.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ADB = r"C:\Program Files\Software Fix\adb.exe"
REMOTE_BASE = "/usr/data/audiobooks"
PLAYER_POSITION_ADDR = 9115148
PLAYER_DURATION_ADDR = 9115252


def run(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        args,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if check and proc.returncode != 0:
        raise RuntimeError(f"{' '.join(args)} failed with {proc.returncode}\n{proc.stdout}")
    return proc


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    return run([adb, "shell", command], check=check).stdout


def shell_quote(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def current_path(adb: str) -> str:
    raw = adb_shell(
        adb,
        "dd if=/usr/data/user.ini bs=1 skip=40 count=512 2>/dev/null | xxd -p -c 512",
    )
    data = bytes.fromhex("".join(raw.split()))
    chars: list[str] = []
    for index in range(0, len(data) - 1, 2):
        lo = data[index]
        hi = data[index + 1]
        if lo == 0 and hi == 0:
            if chars:
                break
            continue
        chars.append(chr(lo) if hi == 0 else "?")
    path = "".join(chars)
    if path.startswith(":\\"):
        path = "a" + path
    elif path.startswith("\\Audiobooks\\"):
        path = "a:" + path
    return path


def safe_id(root: str) -> str:
    root = re.sub(r"^[Aa]:\\Audiobooks\\", "", root)
    return re.sub(r"[^A-Za-z0-9._-]", "_", root)


def read_u32(adb: str, addr: int) -> int:
    output = adb_shell(
        adb,
        f"pid=$(pidof hiby_player); "
        f"dd if=/proc/$pid/mem bs=1 skip={addr} count=4 2>/dev/null | xxd -p -c4",
    )
    text = "".join(output.split())
    if len(text) < 8:
        raise RuntimeError(f"could not read player memory at {addr}: {output!r}")
    return int.from_bytes(bytes.fromhex(text[:8]), "little")


def pull_json(adb: str, remote: str, local: Path) -> dict[str, object]:
    run([adb, "pull", remote, str(local)])
    return json.loads(local.read_text(encoding="utf-8-sig"))


def push_json(adb: str, remote: str, local: Path, data: dict[str, object]) -> None:
    local.write_text(json.dumps(data, indent=2) + "\n", encoding="ascii")
    run([adb, "push", str(local), remote])


def start_daemon(
    adb: str,
    remote_base: str,
    *,
    touch_frames: int,
    screen_guard_enabled: bool,
    screen_min_bar_pixels: int,
) -> None:
    daemon = f"{remote_base}/bin/r1_audiobook_resume_daemon.sh"
    pidfile = f"{remote_base}/resume-daemon.ssd.pid"
    env = " ".join(
        [
            "AUDIOBOOK_POSITION_SOURCE=memory",
            "AUDIOBOOK_RESTORE_ENABLED=1",
            "AUDIOBOOK_TRACK_RESTORE_ENABLED=1",
            "AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=1",
            "AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH=1",
            "AUDIOBOOK_INTERVAL_SECONDS=1",
            "AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS=1",
            "AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS=300",
            "AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS=300",
            "AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS=30000",
            "AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS=5000",
            "AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED=1",
            f"AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED={1 if screen_guard_enabled else 0}",
            f"AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS={screen_min_bar_pixels}",
            f"AUDIOBOOK_UI_SEEK_TOUCH_FRAMES={touch_frames}",
        ]
    )
    adb_shell(
        adb,
        f"env {env} start-stop-daemon -S -b -m -p {shell_quote(pidfile)} -x {shell_quote(daemon)}",
    )


def stop_daemon(adb: str, remote_base: str) -> None:
    adb_shell(
        adb,
        f"start-stop-daemon -K -p {shell_quote(remote_base + '/resume-daemon.ssd.pid')} "
        f"2>/dev/null || true; "
        f"rm -f {shell_quote(remote_base + '/resume-daemon.pid')} "
        f"{shell_quote(remote_base + '/resume-daemon.ssd.pid')}",
    )


def choose_target(position_ms: int, duration_ms: int, offset_ms: int) -> int:
    if duration_ms < 120_000:
        raise RuntimeError(f"duration is too short for a useful seek test: {duration_ms}ms")
    target = position_ms + offset_ms
    high = duration_ms - 60_000
    if target > high:
        target = max(60_000, duration_ms // 2)
    if abs(target - position_ms) < 30_000:
        target = min(high, position_ms + 60_000)
    if not (0 < target < duration_ms):
        raise RuntimeError(
            f"could not choose an in-range target: pos={position_ms} duration={duration_ms}"
        )
    return target


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=ADB)
    parser.add_argument("--remote-base", default=REMOTE_BASE)
    parser.add_argument("--offset-seconds", type=int, default=120)
    parser.add_argument("--wait-seconds", type=int, default=10)
    parser.add_argument("--touch-frames", type=int, default=2)
    parser.add_argument("--disable-screen-guard", action="store_true")
    parser.add_argument("--screen-min-bar-pixels", type=int, default=300)
    parser.add_argument(
        "--i-understand-this-seeks-playback",
        action="store_true",
        help="Required. This test changes the current audiobook playback position.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.i_understand_this_seeks_playback:
        print("Refusing to run without --i-understand-this-seeks-playback.", file=sys.stderr)
        return 2

    path = current_path(args.adb)
    if not re.match(r"^[aA]:\\Audiobooks\\", path):
        print(f"Refusing to run: current path is not an audiobook: {path}", file=sys.stderr)
        return 3
    root = path.rsplit("\\", 1)[0]
    record_remote = f"{args.remote_base}/resume.d/{safe_id(root)}.json"
    pos = read_u32(args.adb, PLAYER_POSITION_ADDR)
    duration = read_u32(args.adb, PLAYER_DURATION_ADDR)
    target = choose_target(pos, duration, args.offset_seconds * 1000)

    print(f"path:        {path}")
    print(f"position_ms: {pos}")
    print(f"duration_ms: {duration}")
    print(f"target_ms:   {target}")
    print(f"record:      {record_remote}")

    with tempfile.TemporaryDirectory(prefix="r1-ui-seek-") as tmp_dir:
        record_local = Path(tmp_dir) / "record.json"
        record = pull_json(args.adb, record_remote, record_local)
        record["current_path"] = path
        record["root_hiby_path"] = root
        record["position_ms"] = target

        stop_daemon(args.adb, args.remote_base)
        push_json(args.adb, record_remote, record_local, record)
        start_daemon(
            args.adb,
            args.remote_base,
            touch_frames=args.touch_frames,
            screen_guard_enabled=not args.disable_screen_guard,
            screen_min_bar_pixels=args.screen_min_bar_pixels,
        )
        time.sleep(args.wait_seconds)

    tail = adb_shell(args.adb, f"tail -n 60 {shell_quote(args.remote_base + '/resume-daemon.log')}")
    print("--- daemon log tail ---")
    print(tail.rstrip())

    after_path = current_path(args.adb)
    after_pos = read_u32(args.adb, PLAYER_POSITION_ADDR)
    print(f"after_path:   {after_path}")
    print(f"after_pos_ms: {after_pos}")
    if after_path != path:
        print("seek_verified=false reason=path_changed", file=sys.stderr)
        return 4
    tolerance = 20_000 + duration // 438
    if abs(after_pos - target) <= tolerance:
        print(f"seek_verified=true tolerance_ms={tolerance}")
        return 0
    print(
        f"seek_verified=false target_ms={target} after_pos_ms={after_pos} "
        f"tolerance_ms={tolerance}",
        file=sys.stderr,
    )
    return 5


if __name__ == "__main__":
    raise SystemExit(main())
