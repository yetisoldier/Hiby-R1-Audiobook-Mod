#!/usr/bin/env python3
"""Inject a single Linux input key press into an R1 /dev/input/event device."""

from __future__ import annotations

import argparse
import struct
import subprocess
from pathlib import Path


ADB = r"C:\Program Files\Software Fix\adb.exe"


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


def event_stream(code: int) -> bytes:
    return b"".join(
        [
            input_event(1, code, 1),
            input_event(0, 0, 0),
            input_event(1, code, 0),
            input_event(0, 0, 0),
        ]
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("event", help="event node, for example event2")
    parser.add_argument("code", type=int, help="Linux input key code")
    parser.add_argument("--adb", default=ADB)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    local = Path("work/input-events") / f"{args.event}_key_{args.code}.bin"
    remote = f"/tmp/{args.event}_key_{args.code}.bin"
    local.parent.mkdir(parents=True, exist_ok=True)
    local.write_bytes(event_stream(args.code))
    print(run([args.adb, "push", str(local), remote]).stdout, end="")
    command = f"cat {remote} > /dev/input/{args.event}; rm -f {remote}"
    output = run([args.adb, "shell", command], check=False).stdout
    if output.strip():
        print(output.rstrip())


if __name__ == "__main__":
    main()
