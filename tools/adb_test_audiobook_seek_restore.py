#!/usr/bin/env python3
"""Guarded live seek test for the HiBy R1 audiobook resume path.

This intentionally refuses to seek unless the stock helper reports an advancing
playback position. It is meant for a manual test after the user starts an
audiobook track from the device UI.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time


ADB = r"C:\Program Files\Software Fix\adb.exe"
HELPER = "/usr/data/audiobooks/bin/r1_audiobook_resume_helper"


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


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    return run([adb, "shell", command], check=check).stdout


def helper_position(adb: str) -> tuple[int | None, str]:
    output = adb_shell(adb, f"{HELPER} position 2>&1", check=False)
    match = re.search(r"position_ms=(\d+)", output)
    return (int(match.group(1)) if match else None, output)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=ADB)
    parser.add_argument("--sample-seconds", type=float, default=4.0)
    parser.add_argument("--back-seconds", type=int, default=20)
    parser.add_argument("--min-position-ms", type=int, default=30000)
    parser.add_argument("--verify-tolerance-seconds", type=int, default=8)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    pos1, out1 = helper_position(args.adb)
    print("--- position sample 1 ---")
    print(out1.rstrip())
    if pos1 is None:
        print("Refusing to seek: could not read position.", file=sys.stderr)
        return 2

    time.sleep(args.sample_seconds)
    pos2, out2 = helper_position(args.adb)
    print("--- position sample 2 ---")
    print(out2.rstrip())
    if pos2 is None:
        print("Refusing to seek: could not read second position.", file=sys.stderr)
        return 2

    delta = pos2 - pos1
    print(f"position_delta_ms={delta}")
    if pos2 < args.min_position_ms:
        print(
            f"Refusing to seek: position {pos2}ms is below "
            f"{args.min_position_ms}ms.",
            file=sys.stderr,
        )
        return 3
    if delta < max(1000, int(args.sample_seconds * 500)):
        print("Refusing to seek: playback position is not advancing.", file=sys.stderr)
        return 4

    target_ms = max(5000, pos2 - args.back_seconds * 1000)
    target_seconds = target_ms // 1000
    print(f"seeking_to_seconds={target_seconds}")
    seek_cmd = (
        f"{HELPER} seek --seconds {target_seconds} "
        "--verify-delay-ms 1500 "
        f"--verify-tolerance {args.verify_tolerance_seconds} 2>&1"
    )
    seek_output = adb_shell(args.adb, seek_cmd, check=False)
    print("--- seek output ---")
    print(seek_output.rstrip())
    pos3, out3 = helper_position(args.adb)
    print("--- position after seek ---")
    print(out3.rstrip())
    if pos3 is None:
        return 5
    low = (target_seconds - args.verify_tolerance_seconds) * 1000
    high = (target_seconds + args.verify_tolerance_seconds) * 1000
    if low <= pos3 <= high:
        print("seek_verified=true")
        return 0
    print(f"seek_verified=false expected_ms_range={low}..{high} actual_ms={pos3}")
    return 6


if __name__ == "__main__":
    raise SystemExit(main())
