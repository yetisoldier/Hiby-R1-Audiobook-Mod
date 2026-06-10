#!/usr/bin/env python3
"""Build or inject HiBy R1 input event streams.

The R1 accepts raw Linux input_event packets written back to its input nodes.
Touchscreen taps are useful for list selections; physical key events are more
stable for playback controls because they are not tied to a screen layout.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
from pathlib import Path


ADB = r"C:\Program Files\Software Fix\adb.exe"
DEFAULT_X = 356
DEFAULT_Y = 735
DEFAULT_TOUCH_FRAMES = 8

BUTTONS = {
    "next": ("event0", 163),
    "prev": ("event2", 165),
    "playpause": ("event2", 164),
}


def run(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=check,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def input_event(event_type: int, code: int, value: int) -> bytes:
    return struct.pack("<llHHl", 0, 0, event_type, code, value)


def abs_frame(x: int, y: int, *, include_press: bool) -> list[bytes]:
    events = [
        input_event(3, 57, 0),  # ABS_MT_TRACKING_ID
        input_event(3, 58, 63),  # ABS_MT_PRESSURE
        input_event(3, 48, 9),  # ABS_MT_TOUCH_MAJOR
        input_event(3, 53, x),  # ABS_MT_POSITION_X
        input_event(3, 54, y),  # ABS_MT_POSITION_Y
        input_event(0, 2, 0),  # SYN_MT_REPORT
    ]
    if include_press:
        events.append(input_event(1, 330, 1))  # BTN_TOUCH down
    events.append(input_event(0, 0, 0))  # SYN_REPORT
    return events


def tap_stream(x: int, y: int) -> bytes:
    events: list[bytes] = []
    events.extend(abs_frame(x, y, include_press=True))
    for _ in range(DEFAULT_TOUCH_FRAMES - 1):
        events.extend(abs_frame(x, y, include_press=False))
    events.extend(
        [
            input_event(1, 330, 0),  # BTN_TOUCH up
            input_event(0, 2, 0),  # SYN_MT_REPORT
            input_event(0, 0, 0),  # SYN_REPORT
        ]
    )
    return b"".join(events)


def drag_stream(x: int, y: int, to_x: int, to_y: int, frames: int) -> bytes:
    frames = max(2, frames)
    events: list[bytes] = []
    events.extend(abs_frame(x, y, include_press=True))
    for step in range(1, frames):
        ratio = step / (frames - 1)
        move_x = round(x + (to_x - x) * ratio)
        move_y = round(y + (to_y - y) * ratio)
        events.extend(abs_frame(move_x, move_y, include_press=False))
    events.extend(
        [
            input_event(1, 330, 0),  # BTN_TOUCH up
            input_event(0, 2, 0),  # SYN_MT_REPORT
            input_event(0, 0, 0),  # SYN_REPORT
        ]
    )
    return b"".join(events)


def touch_phase_stream(x: int, y: int, phase: str) -> bytes:
    if phase == "down":
        return b"".join(abs_frame(x, y, include_press=True))
    if phase == "move":
        return b"".join(abs_frame(x, y, include_press=False))
    if phase == "up":
        return b"".join(
            [
                input_event(1, 330, 0),  # BTN_TOUCH up
                input_event(0, 2, 0),  # SYN_MT_REPORT
                input_event(0, 0, 0),  # SYN_REPORT
            ]
        )
    raise ValueError(f"unsupported touch phase: {phase}")


def key_stream(code: int) -> bytes:
    return b"".join(
        [
            input_event(1, code, 1),  # key down
            input_event(0, 0, 0),  # SYN_REPORT
            input_event(1, code, 0),  # key up
            input_event(0, 0, 0),  # SYN_REPORT
        ]
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--x", type=int, default=DEFAULT_X)
    parser.add_argument("--y", type=int, default=DEFAULT_Y)
    parser.add_argument("--to-x", type=int, help="drag destination x coordinate")
    parser.add_argument("--to-y", type=int, help="drag destination y coordinate")
    parser.add_argument("--drag-frames", type=int, default=16, help="number of touch frames for a drag")
    parser.add_argument("--event", default="event1", help="event node, for example event1")
    parser.add_argument(
        "--button",
        choices=sorted(BUTTONS),
        help="generate a physical key event stream instead of a touchscreen tap",
    )
    parser.add_argument("--key-code", type=int, help="raw Linux input key code")
    parser.add_argument("--adb", default=ADB)
    parser.add_argument("--output", type=Path, help="write the generated stream to this file")
    parser.add_argument("--inject", action="store_true", help="push and inject the generated tap")
    parser.add_argument("--remote-dir", default="/usr/data/mnt/sd_0/.r1-audiobooks/devtmp")
    parser.add_argument(
        "--touch-phase",
        choices=["down", "move", "up"],
        help="generate one delayed-tap phase instead of a complete tap stream",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    event = args.event
    description = f"tap:    {args.x},{args.y}"
    if args.button:
        event, key_code = BUTTONS[args.button]
        if args.event != "event1":
            event = args.event
        data = key_stream(key_code)
        default_name = f"{event}_key_{args.button}.bin"
        description = f"button: {args.button} code={key_code}"
    elif args.key_code is not None:
        data = key_stream(args.key_code)
        default_name = f"{event}_key_{args.key_code}.bin"
        description = f"key:    code={args.key_code}"
    elif args.to_x is not None or args.to_y is not None:
        to_x = args.x if args.to_x is None else args.to_x
        to_y = args.y if args.to_y is None else args.to_y
        data = drag_stream(args.x, args.y, to_x, to_y, args.drag_frames)
        default_name = f"{event}_drag_{args.x}_{args.y}_to_{to_x}_{to_y}.bin"
        description = f"drag:   {args.x},{args.y} -> {to_x},{to_y}"
    elif args.touch_phase:
        data = touch_phase_stream(args.x, args.y, args.touch_phase)
        default_name = f"{event}_tap_{args.x}_{args.y}_{args.touch_phase}.bin"
        description = f"phase:  {args.touch_phase} at {args.x},{args.y}"
    else:
        data = tap_stream(args.x, args.y)
        default_name = f"{event}_tap_{args.x}_{args.y}.bin"

    local = args.output or Path("work/input-events") / default_name
    local.parent.mkdir(parents=True, exist_ok=True)
    local.write_bytes(data)
    print(f"event:  /dev/input/{event}")
    print(description)
    print(f"output: {local}")
    print(f"bytes:  {len(data)}")

    if args.inject:
        remote = f"{args.remote_dir.rstrip('/')}/{local.name}"
        print(run([args.adb, "shell", f"mkdir -p '{args.remote_dir}'"]).stdout, end="")
        print(run([args.adb, "push", str(local), remote]).stdout, end="")
        output = run(
            [args.adb, "shell", f"cat '{remote}' > /dev/input/{event}"],
            check=False,
        ).stdout
        if output.strip():
            print(output.rstrip())


if __name__ == "__main__":
    main()
