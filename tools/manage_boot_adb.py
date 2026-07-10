#!/usr/bin/env python3
"""Manage boot-time ADB on the HiBy R1.

Converted from adb_manage_boot_adb.ps1.
Checks status of boot ADB init scripts, enable/disable the boot ADB marker,
and provides advice about what to expect on the next reboot.
"""

from __future__ import annotations

import argparse
import logging
import shutil
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

STATUS_SCRIPT = r"""echo "s90adb=$(if [ -x /etc/init.d/S90adb ]; then echo yes; elif [ -e /etc/init.d/S90adb ]; then echo present-not-exec; else echo no; fi)"
echo "s90adb_wrapper=$(if grep -q skip=1856 /etc/init.d/S90adb 2>/dev/null; then echo yes; elif [ -e /etc/init.d/S90adb ]; then echo no; else echo missing; fi)"
echo "t90adb=$(if [ -x /etc/init.d/T90adb ]; then echo yes; elif [ -e /etc/init.d/T90adb ]; then echo present-not-exec; else echo no; fi)"
echo "disableadb=$(if [ -e /usr/data/disableadb ]; then echo yes; else echo no; fi)"
echo "adbd=$(ps | grep '[a]dbd' >/dev/null && echo running || echo stopped)"
echo "adb_gadget=$(if [ -d /sys/kernel/config/usb_gadget/adb_demo ]; then echo yes; else echo no; fi)"
echo "usb_working_mode=$(set -- $(dd if=/usr/data/user.ini bs=1 skip=1856 count=1 2>/dev/null | od -An -t u1 2>/dev/null); echo ${1:-unknown})"
echo "version=$(sed -n 's/^version=//p' /etc/r1_audiobook_version 2>/dev/null | head -1)"
echo "boot_adb_marker=$(sed -n 's/^boot_adb=//p' /etc/r1_audiobook_version 2>/dev/null | head -1)"
"""


def resolve_adb_path(adb_arg: str) -> str:
    """Find the ADB executable."""
    if adb_arg:
        p = Path(adb_arg)
        if p.exists():
            return str(p.resolve())
        raise FileNotFoundError(f"Missing adb path: {adb_arg}")

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    repo_adb = repo_root / ".tools" / "platform-tools" / "adb"
    if repo_adb.exists():
        return str(repo_adb.resolve())

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
    return result.stdout


def read_boot_adb_status(adb: str) -> dict[str, str]:
    """Read boot ADB status from the device and return a dict."""
    text = adb_shell(adb, STATUS_SCRIPT)
    print(text)
    values: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def write_boot_adb_advice(values: dict[str, str]) -> None:
    """Print advice based on boot ADB status values."""
    print()
    s90 = values.get("s90adb", "")
    wrapper = values.get("s90adb_wrapper", "")
    disable = values.get("disableadb", "")
    usb_mode = values.get("usb_working_mode", "")
    t90 = values.get("t90adb", "")
    adbd = values.get("adbd", "")

    if s90 == "yes" and wrapper == "yes" and disable == "no" and usb_mode == "1":
        print("OK   ADB should be allowed to start on the next boot.")
    elif s90 == "yes" and wrapper == "yes" and disable == "no":
        print("INFO Firmware has /etc/init.d/S90adb, but USB working mode is not Device.")
        print("     Set System -> USB working mode -> Device for boot ADB in dev builds.")
    elif s90 == "yes" and wrapper == "no" and disable == "no":
        print("INFO Firmware has /etc/init.d/S90adb, but it is not the USB-mode-gated wrapper.")
        print("     New dev builds use the stock System -> USB working mode setting as the guard.")
    elif s90 == "yes" and disable == "yes":
        print("INFO Firmware has /etc/init.d/S90adb, but /usr/data/disableadb blocks boot ADB.")
        print("     Run with --action enable to remove the marker for the next reboot.")
    elif t90 == "yes":
        print("INFO Stock ADB helper exists as /etc/init.d/T90adb, but boot persistence needs /etc/init.d/S90adb.")
        print("     Build a development firmware with tools/build_firmware.py --enable-boot-adb.")
    else:
        print("WARN No usable stock ADB init helper was found.")

    if adbd == "running":
        print("OK   adbd is running in the current session.")
    else:
        print("INFO adbd is not currently running.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Manage boot-time ADB on the HiBy R1.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument(
        "--action",
        choices=["status", "enable", "disable"],
        default="status",
        help="Action to perform.",
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

    result = subprocess.run([adb, "devices"])
    if result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    if args.action == "enable":
        adb_shell(adb, "rm -f /usr/data/disableadb; sync")
        print("Removed /usr/data/disableadb. This affects the next boot if /etc/init.d/S90adb exists.")

    elif args.action == "disable":
        adb_shell(adb, "touch /usr/data/disableadb; sync")
        print("Created /usr/data/disableadb. This should block boot ADB on the next reboot.")
        print("The current ADB session is left running.")

    values = read_boot_adb_status(adb)
    write_boot_adb_advice(values)
    return 0


if __name__ == "__main__":
    sys.exit(main())