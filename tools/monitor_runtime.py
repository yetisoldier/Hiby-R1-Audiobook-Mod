#!/usr/bin/env python3
"""Monitor R1 runtime metrics over a period via ADB.

Converted from adb_monitor_r1_runtime.ps1.
Periodically samples device health (uptime, battery, memory, processes,
audiobook logs) and writes the combined output to a local file.
"""

from __future__ import annotations

import argparse
import logging
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

logger = logging.getLogger(__name__)

SAMPLE_COMMAND = r"""echo '=== sample ==='
date
echo '--- version ---'
cat /etc/r1_audiobook_version 2>/dev/null || true
echo '--- uptime/load ---'
cat /proc/uptime 2>/dev/null || true
cat /proc/loadavg 2>/dev/null || true
echo '--- battery ---'
for d in /sys/class/power_supply/*; do
  [ -d "$d" ] || continue
  echo "supply=$(basename "$d")"
  for f in type status capacity voltage_now current_now charge_now energy_now temp present online; do
    [ -r "$d/$f" ] && echo "$f=$(cat "$d/$f" 2>/dev/null)"
  done
done
echo '--- memory ---'
cat /proc/meminfo 2>/dev/null | head -20 || true
echo '--- processes ---'
ps | grep -E 'hiby_player|r1_audiobook|db_watch|dmrd|adbd' | grep -v grep || true
echo '--- top ---'
top -n 1 2>/dev/null | head -20 || true
echo '--- current user.ini path slot ---'
dd if=/usr/data/user.ini bs=1 skip=40 count=256 2>/dev/null | xxd -p -c 64 || true
echo '--- audiobook logs tail ---'
tail -20 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true
tail -20 /usr/data/audiobooks/db-watch.log 2>/dev/null || true
tail -20 /usr/data/audiobooks/db-maint.log 2>/dev/null || true
echo
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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Monitor R1 runtime metrics over a period via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--duration-minutes", type=int, default=60, help="Total monitoring duration in minutes.")
    parser.add_argument("--interval-seconds", type=int, default=60, help="Sampling interval in seconds.")
    parser.add_argument("--out-dir", default="", help="Output directory (defaults to work/runtime-monitor/<stamp>).")
    return parser


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.duration_minutes <= 0:
        print("ERROR: --duration-minutes must be positive", file=sys.stderr)
        return 2
    if args.interval_seconds <= 0:
        print("ERROR: --interval-seconds must be positive", file=sys.stderr)
        return 2

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
        out_dir = repo_root / "work" / "runtime-monitor" / stamp

    out_dir.mkdir(parents=True, exist_ok=True)

    # --- adb devices ---------------------------------------------------------
    devices_path = out_dir / "adb-devices.txt"
    result = subprocess.run([adb, "devices"], capture_output=True, text=True)
    devices_path.write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    # --- Sampling loop -------------------------------------------------------
    sample_path = out_dir / "samples.txt"
    end_time = time.monotonic() + args.duration_minutes * 60
    sample_count = 0

    print(f"Writing monitor samples to: {sample_path}")

    while time.monotonic() < end_time:
        sample_count += 1
        now = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        print(f"Sample {sample_count} at {now}")

        result = subprocess.run(
            [adb, "shell", SAMPLE_COMMAND],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"ERROR: adb shell sample failed: {result.stderr.strip()}", file=sys.stderr)
            return 2

        output = result.stdout
        print(output, end="")
        with sample_path.open("a", encoding="utf-8") as f:
            f.write(output)

        if time.monotonic() < end_time:
            time.sleep(args.interval_seconds)

    print(f"Runtime monitor bundle: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())