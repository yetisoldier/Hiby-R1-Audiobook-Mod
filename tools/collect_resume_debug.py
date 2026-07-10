#!/usr/bin/env python3
"""Collect audiobook resume debug data from a HiBy R1 via ADB.

Converted from adb_collect_audiobook_resume_debug.ps1.
Gathers system state, logs, resume records, catalogs, and config files
into a timestamped local directory for offline analysis.
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

# Files to pull if they exist on the device.
PULL_FILES = [
    "resume-daemon.log",
    "resume-daemon.stdout.log",
    "db-watch.log",
    "db-maint.log",
    "db-maint.stdout.log",
    "catalog.tsv",
    "catalog-albums.txt",
    "catalog-books.tsv",
    "catalog-view-title.tsv",
    "catalog-view-author.tsv",
    "catalog-view-series.tsv",
]

# Remote paths that don't follow the remote-base prefix.
PULL_ABSOLUTE = [
    "/usr/data/user.ini",
]


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


def pull_if_exists(adb: str, remote: str, local: Path) -> bool:
    """Pull a remote file if it exists. Returns True if pulled."""
    exists = subprocess.run(
        [adb, "shell", f"[ -e '{remote}' ] && echo yes || true"],
        capture_output=True,
        text=True,
    )
    if exists.stdout.strip() != "yes":
        return False

    local.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [adb, "pull", remote, str(local)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"WARN: failed to pull {remote}: {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


# The summary shell script to run on-device.
SUMMARY_SCRIPT = r"""echo '--- date ---'
date
echo '--- version ---'
cat /etc/r1_audiobook_version 2>/dev/null || true
echo '--- df ---'
df -h /usr/data /usr/data/mnt/sd_0 2>/dev/null || true
echo '--- uptime/load ---'
cat /proc/uptime 2>/dev/null || true
cat /proc/loadavg 2>/dev/null || true
echo '--- memory ---'
cat /proc/meminfo 2>/dev/null | head -40 || true
echo '--- processes ---'
ps | grep -E 'hiby_player|r1_audiobook|db_watch' | grep -v grep || true
echo '--- top ---'
top -n 1 2>/dev/null | head -30 || true
echo '--- kernel messages tail ---'
dmesg 2>/dev/null | tail -120 || true
echo '--- current user.ini path slot hex ---'
dd if=/usr/data/user.ini bs=1 skip=40 count=512 2>/dev/null | xxd -p -c 64 || true
echo '--- resume records ---'
ls -l 'REMOTE_BASE/resume.d' 2>/dev/null || true
echo '--- catalog header ---'
head -5 'REMOTE_BASE/catalog.tsv' 2>/dev/null || true
echo '--- daemon log tail ---'
tail -120 'REMOTE_BASE/resume-daemon.log' 2>/dev/null || true
echo '--- daemon stdout tail ---'
tail -80 'REMOTE_BASE/resume-daemon.stdout.log' 2>/dev/null || true
echo '--- db watch log tail ---'
tail -80 'REMOTE_BASE/db-watch.log' 2>/dev/null || true
echo '--- db maint log tail ---'
tail -80 'REMOTE_BASE/db-maint.log' 2>/dev/null || true
echo '--- db maint stdout tail ---'
tail -80 'REMOTE_BASE/db-maint.stdout.log' 2>/dev/null || true
"""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Collect audiobook resume debug data from a HiBy R1 via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--remote-base", default=DEFAULT_REMOTE_BASE, help="Remote audiobook base directory.")
    parser.add_argument("--out-dir", default="", help="Output directory (defaults to work/resume-debug/<stamp>).")
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

    repo_root = Path(__file__).resolve().parent.parent

    if args.out_dir:
        out_dir = Path(args.out_dir).resolve()
    else:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        out_dir = repo_root / "work" / "resume-debug" / stamp

    out_dir.mkdir(parents=True, exist_ok=True)

    # --- adb devices ---------------------------------------------------------
    devices_file = out_dir / "adb-devices.txt"
    result = subprocess.run([adb, "devices"], capture_output=True, text=True)
    devices_file.write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    # --- Summary shell script ------------------------------------------------
    summary_script = SUMMARY_SCRIPT.replace("REMOTE_BASE", args.remote_base)
    summary_file = out_dir / "summary.txt"
    try:
        output = adb_shell(adb, summary_script)
        summary_file.write_text(output, encoding="utf-8")
        print(output)
    except RuntimeError as exc:
        print(f"ERROR: failed to collect summary: {exc}", file=sys.stderr)
        return 2

    # --- Pull files ----------------------------------------------------------
    for name in PULL_FILES:
        remote = f"{args.remote_base}/{name}"
        pull_if_exists(adb, remote, out_dir / name)

    # Also try to pull the resume.d directory.
    pull_if_exists(adb, f"{args.remote_base}/resume.d", out_dir / "resume.d")

    for remote in PULL_ABSOLUTE:
        local_name = Path(remote).name
        pull_if_exists(adb, remote, out_dir / local_name)

    print(f"Debug bundle: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())