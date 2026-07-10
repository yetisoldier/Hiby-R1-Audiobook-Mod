#!/usr/bin/env python3
"""Collect R1 device state (DBs, config, system info) via ADB.

Converted from adb_collect_r1_state.ps1.
Pulls known device files into a timestamped local directory for inspection.
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

# Remote paths to pull if present.
PULL_TARGETS = [
    ("/usr/data/usrlocal_media.db", "usrlocal_media.db"),
    ("/data/usrlocal_media.db", "data-usrlocal_media.db"),
    ("/usr/data/book.db", "book.db"),
    ("/data/book.db", "data-book.db"),
    ("/usr/data/user.ini", "user.ini"),
    ("/usr/data/momery_list.lst", "momery_list.lst"),
    ("/usr/data/menu_cfg", "menu_cfg"),
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


def pull_if_present(adb: str, remote: str, local: Path) -> None:
    """Pull a remote file if it exists on the device."""
    probe = subprocess.run(
        [adb, "shell", f"if [ -e '{remote}' ]; then echo present; else echo missing; fi"],
        capture_output=True,
        text=True,
    )
    if "present" in probe.stdout:
        result = subprocess.run([adb, "pull", remote, str(local)])
        if result.returncode != 0:
            print(f"WARN: failed to pull {remote}", file=sys.stderr)
    else:
        print(f"Skipping missing path: {remote}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Collect R1 device state (DBs, config, system info) via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--out-dir", default="device-dump", help="Output directory name (created under cwd).")
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
    target = Path(args.out_dir) / stamp
    target.mkdir(parents=True, exist_ok=True)

    # --- adb devices -l ------------------------------------------------------
    devices_file = target / "adb-devices.txt"
    result = subprocess.run([adb, "devices", "-l"], capture_output=True, text=True)
    devices_file.write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    # --- Device state shell command ------------------------------------------
    state_cmd = "uname -a; mount; ls -la /usr/data /data /data/mnt /data/mnt/sd_0 2>/dev/null"
    state_result = subprocess.run(
        [adb, "shell", state_cmd],
        capture_output=True,
        text=True,
    )
    state_file = target / "device-state.txt"
    state_file.write_text(state_result.stdout, encoding="utf-8")
    print(state_result.stdout, end="")

    # --- Pull known files ----------------------------------------------------
    for remote, local_name in PULL_TARGETS:
        pull_if_present(adb, remote, target / local_name)

    print(f"Saved device state to {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())