#!/usr/bin/env python3
"""Probe R1 settings before/after a USB mode toggle.

Converted from adb_probe_usb_mode_toggle.ps1.
Takes settings snapshots before and after a USB mode change on the R1,
and compares them to identify which bytes in user.ini changed.

Modes:
  full: Take a "before" snapshot, prompt the user to change USB mode on
        the device, then take an "after" snapshot and compare.
  before: Take only the "before" snapshot.
  after: Take an "after" snapshot and compare against a previously
         saved "before" snapshot (requires --before-snapshot).
"""

from __future__ import annotations

import argparse
import logging
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

logger = logging.getLogger(__name__)


def resolve_path_strict(path_value: str) -> Path:
    """Resolve a path and assert it exists."""
    p = Path(path_value).resolve()
    if not p.exists():
        raise FileNotFoundError(f"Missing path: {path_value}")
    return p


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


def to_safe_name(value: str) -> str:
    """Convert a string to a safe directory name component."""
    safe = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("_")
    return safe if safe else "snapshot"


def get_latest_snapshot(snapshot_root: Path, label: str) -> Path | None:
    """Find the most recent snapshot directory matching *label*."""
    if not snapshot_root.exists():
        return None
    safe_label = to_safe_name(label)
    candidates = sorted(
        [d for d in snapshot_root.iterdir() if d.is_dir() and d.name.endswith(f"-{safe_label}")],
        key=lambda d: d.stat().st_mtime,
        reverse=True,
    )
    return candidates[0] if candidates else None


def invoke_settings_snapshot(
    adb: str,
    label: str,
    out_dir: str,
    compare_to: str = "",
) -> Path:
    """Run snapshot_settings.py and return the snapshot directory path."""
    script = Path(__file__).resolve().parent / "snapshot_settings.py"
    cmd = [
        sys.executable, str(script),
        "--adb", adb,
        "--label", label,
        "--out-dir", out_dir,
    ]
    if compare_to:
        cmd.extend(["--compare-to", compare_to])
    result = subprocess.run(cmd)
    if result.returncode != 0:
        raise RuntimeError("settings snapshot failed")

    repo_root = Path(__file__).resolve().parent.parent
    snapshot_root = repo_root / out_dir
    snapshot = get_latest_snapshot(snapshot_root, label)
    if not snapshot:
        raise RuntimeError(f"Could not locate snapshot for label {label}")
    return snapshot


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Probe R1 settings before/after a USB mode toggle.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument(
        "--mode",
        choices=["full", "before", "after"],
        default="full",
        help="Snapshot mode.",
    )
    parser.add_argument("--before-snapshot", default="", help="Path to a before snapshot (required for --mode after).")
    parser.add_argument("--out-dir", default="work/settings-snapshots", help="Output directory for snapshots.")
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

    if args.mode == "before":
        try:
            before = invoke_settings_snapshot(adb, "before-usb-mode", args.out_dir)
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        print(f"Before snapshot: {before}")
        return 0

    if args.mode == "after":
        if not args.before_snapshot:
            print("ERROR: --before-snapshot is required when --mode after is used.", file=sys.stderr)
            return 2
        try:
            before = resolve_path_strict(args.before_snapshot)
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        try:
            after = invoke_settings_snapshot(adb, "after-usb-mode", args.out_dir, compare_to=str(before))
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        print(f"Before snapshot: {before}")
        print(f"After snapshot : {after}")
        return 0

    # Full mode
    try:
        before = invoke_settings_snapshot(adb, "before-usb-mode", args.out_dir)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print()
    print(f"Before snapshot: {before}")
    print()
    print("On the R1, change System -> USB device mode.")
    print("For the ADB persistence investigation, the interesting change is Storage <-> Dock.")
    print("After the device finishes changing mode, press Enter here.")
    input()

    try:
        after = invoke_settings_snapshot(adb, "after-usb-mode", args.out_dir, compare_to=str(before))
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"Before snapshot: {before}")
    print(f"After snapshot : {after}")
    return 0


if __name__ == "__main__":
    sys.exit(main())