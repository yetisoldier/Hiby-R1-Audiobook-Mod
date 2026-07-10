#!/usr/bin/env python3
"""Archive known development artifacts from a HiBy R1 via ADB.

Converted from adb_archive_audiobook_dev_artifacts.ps1.
Moves known debug/development files from the device's audiobook directory
into a timestamped archive directory.  Dry-run by default; requires
``--i-understand-this-moves-device-files`` to actually move files.
"""

from __future__ import annotations

import argparse
import logging
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

logger = logging.getLogger(__name__)

DEFAULT_REMOTE_BASE = "/usr/data/audiobooks"

DEV_ARTIFACTS = [
    "debug-daemon.out",
    "debug-daemon.pid",
    "dmr-probe.out",
    "helper-current.out",
    "helper-current.strace",
    "mem-pos-near.bin",
    "player-restart.out",
    "player-restart2.out",
    "position-watch-holidays-on-ice-2008.nohup.log",
    "position-watch-holidays-on-ice-2008.pid",
    "position-watch-holidays.loop.log",
    "position-watch-holidays.nohup.log",
    "position-watch-holidays.pid",
    "ptrwins",
    "r1_audiobook_resume_daemon.syntax-test.sh",
    "resume-daemon.testpid",
    "resume-daemon.trace",
    "scan_skip_runtime_patch.json",
    "tracklist-window.bin",
    "user.ini.before-stock-audiobook-last-clear",
]


def resolve_adb_path(adb_arg: str) -> str:
    """Find the ADB executable."""
    if adb_arg:
        p = Path(adb_arg)
        if p.exists():
            return str(p.resolve())
        raise FileNotFoundError(f"Missing adb path: {adb_arg}")

    which = shutil.which("adb")
    if which:
        return which

    fallback = "/home/yetisoldier/.local/bin/adb"
    if Path(fallback).exists():
        return fallback

    raise FileNotFoundError(
        "ADB not found. Install platform-tools, add adb to PATH, "
        "or use --adb to specify the path."
    )


def adb_shell(adb: str, command: str) -> str:
    """Run ``adb shell <command>`` and return stdout."""
    result = subprocess.run(
        [adb, "shell", command],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"adb shell failed (exit {result.returncode}): {command}\n"
            f"stderr: {result.stderr.strip()}"
        )
    return result.stdout.strip()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Archive known development artifacts from a HiBy R1 via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--remote-base", default=DEFAULT_REMOTE_BASE, help="Remote audiobook base directory.")
    parser.add_argument("--remote-archive-root", default="", help="Root directory for archives (defaults to remote-base).")
    parser.add_argument(
        "--i-understand-this-moves-device-files",
        action="store_true",
        help="Actually move files; without this flag, runs as a dry-run.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        adb = resolve_adb_path(args.adb)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    if args.remote_archive_root:
        archive_dir = f"{args.remote_archive_root.rstrip('/')}/dev-archive-{stamp}"
    else:
        archive_dir = f"{args.remote_base}/dev-archive-{stamp}"

    # --- Check device connectivity -------------------------------------------
    result = subprocess.run([adb, "devices"])
    if result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    # --- Probe for artifacts --------------------------------------------------
    present: list[str] = []
    for name in DEV_ARTIFACTS:
        remote = f"{args.remote_base}/{name}"
        exists = adb_shell(adb, f"if [ -e '{remote}' ]; then echo yes; else echo no; fi")
        if exists.strip() == "yes":
            present.append(name)

    if not present:
        print(f"No known development artifacts found under {args.remote_base}.")
        return 0

    print(f"Known development artifacts under {args.remote_base}:")
    for name in present:
        print(f"  {name}")

    if not args.i_understand_this_moves_device_files:
        print()
        print("Dry run only. Re-run with --i-understand-this-moves-device-files to move these into:")
        print(f"  {archive_dir}")
        return 0

    # --- Move artifacts ------------------------------------------------------
    adb_shell(adb, f"mkdir -p '{archive_dir}'")
    for name in present:
        remote = f"{args.remote_base}/{name}"
        try:
            adb_shell(adb, f"mv '{remote}' '{archive_dir}/'")
        except RuntimeError as exc:
            print(f"ERROR: failed to archive {remote}: {exc}", file=sys.stderr)
            return 2

    print(f"Moved {len(present)} development artifacts to {archive_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())