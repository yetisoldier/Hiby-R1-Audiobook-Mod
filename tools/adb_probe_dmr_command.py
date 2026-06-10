#!/usr/bin/env python3
"""
Probe HiBy R1 /data/dmr_streamer commands through the resume helper gate.

The existing r1_audiobook_resume_helper safely flips the player flag required
for DMR commands, sends "get_position_info", then restores the flag. This tool
patches only a temporary copy of that helper so the same code path sends a
different short command such as "next", "previous", or "play@1".
"""

from __future__ import annotations

import argparse
import re
import subprocess
import time
from pathlib import Path


ADB = r"C:\Program Files\Software Fix\adb.exe"
HELPER = Path("work/device-audiobook-helper-20260609/audiobooks/bin/r1_audiobook_resume_helper")
REMOTE_HELPER = "/usr/data/audiobooks/bin/r1_dmr_probe_helper"
ORIGINAL = b"get_position_info\x00"


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


def current_path(adb: str) -> str:
    raw = adb_shell(
        adb,
        "dd if=/usr/data/user.ini bs=1 skip=40 count=512 2>/dev/null | xxd -p -c 512",
    )
    text = "".join(raw.split())
    data = bytes.fromhex(text)
    chars = []
    for index in range(0, len(data) - 1, 2):
        lo = data[index]
        hi = data[index + 1]
        if lo == 0 and hi == 0:
            if not chars:
                continue
            break
        chars.append(chr(lo) if hi == 0 else "?")
    path = "".join(chars)
    if path.startswith(":\\"):
        path = "a" + path
    elif path.startswith("\\Audiobooks\\"):
        path = "a:" + path
    return path


def position(adb: str, helper: str) -> int | None:
    output = adb_shell(adb, f"{helper} position 2>/dev/null", check=False)
    match = re.search(r"position_ms=(\d+)", output)
    return int(match.group(1)) if match else None


def build_helper(source: Path, command: str, output: Path) -> None:
    command_bytes = command.encode("ascii") + b"\x00"
    if len(command_bytes) > len(ORIGINAL):
        raise SystemExit(
            f"Command is too long for this probe ({len(command_bytes) - 1} > {len(ORIGINAL) - 1})"
        )
    data = source.read_bytes()
    if data.count(ORIGINAL) != 1:
        raise SystemExit("Could not find a unique get_position_info string in helper")
    patched = data.replace(ORIGINAL, command_bytes.ljust(len(ORIGINAL), b"\x00"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(patched)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", help='Short DMR command, for example "next" or "play@1"')
    parser.add_argument("--adb", default=ADB)
    parser.add_argument("--helper-source", type=Path, default=HELPER)
    parser.add_argument("--remote-helper", default=REMOTE_HELPER)
    parser.add_argument("--sleep", type=float, default=1.5)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.helper_source.exists():
        raise SystemExit(f"Missing helper: {args.helper_source}")
    local_helper = Path("work/dmr-probe-helper") / (
        "r1_dmr_probe_" + re.sub(r"[^A-Za-z0-9_.-]", "_", args.command)
    )
    build_helper(args.helper_source, args.command, local_helper)

    before_path = current_path(args.adb)
    before_pos = position(args.adb, "/usr/data/audiobooks/bin/r1_audiobook_resume_helper")

    print(f"command:        {args.command}")
    print(f"before_path:    {before_path}")
    print(f"before_pos_ms:  {before_pos}")
    print(f"local_helper:   {local_helper}")
    print(f"remote_helper:  {args.remote_helper}")

    print(run([args.adb, "push", str(local_helper), args.remote_helper], check=True).stdout, end="")
    adb_shell(args.adb, f"chmod 755 {args.remote_helper}")
    output = adb_shell(
        args.adb,
        f"{args.remote_helper} position >/usr/data/audiobooks/dmr-probe.out 2>&1; "
        "cat /usr/data/audiobooks/dmr-probe.out",
        check=False,
    )
    if output.strip():
        print("--- helper output ---")
        print(output.rstrip())
    time.sleep(args.sleep)

    after_path = current_path(args.adb)
    after_pos = position(args.adb, "/usr/data/audiobooks/bin/r1_audiobook_resume_helper")
    print(f"after_path:     {after_path}")
    print(f"after_pos_ms:   {after_pos}")
    print(f"path_changed:   {before_path != after_path}")


if __name__ == "__main__":
    main()
