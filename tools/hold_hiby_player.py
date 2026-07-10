#!/usr/bin/env python3
"""Hold (or release) the HiBy player process on the R1 via ADB.

Converted from adb_hold_hiby_player.ps1.
On Linux, a backgrounded ``adb shell /usr/bin/hiby_player`` keeps the player
alive.  This script manages that background process with a PID marker file.

Modes:
  --start (default): Launch a background adb holder for hiby_player.
  --stop: Kill the held adb holder process and clean up.
  --force-restart: Stop any existing holder and player, then start fresh.
"""

from __future__ import annotations

import argparse
import logging
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

logger = logging.getLogger(__name__)


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


def get_remote_player_pids(adb: str) -> list[str]:
    """Return list of hiby_player PIDs on the device."""
    out = subprocess.run(
        [adb, "shell",
         "ps | sed -n '/\\/usr\\/bin\\/hiby_player$/ { /grep/d; s/^ *\\([0-9][0-9]*\\).*/\\1/p; }'"],
        capture_output=True,
        text=True,
    )
    if out.returncode != 0:
        raise RuntimeError("failed to query hiby_player")
    return [line.strip() for line in out.stdout.splitlines() if line.strip().isdigit()]


def stop_held_adb_process(marker: Path) -> None:
    """Stop the held adb background process using the marker file."""
    stopped = False
    if marker.exists():
        pid_text = marker.read_text().strip()
        if pid_text.isdigit():
            pid = int(pid_text)
            try:
                os.kill(pid, signal.SIGTERM)
                print(f"stopped held adb process pid={pid}")
                stopped = True
            except ProcessLookupError:
                pass
            except PermissionError:
                print(f"WARN: cannot kill pid={pid} (permission denied)", file=sys.stderr)
        marker.unlink(missing_ok=True)

    # Also try to find any adb process running shell /usr/bin/hiby_player.
    try:
        ps_out = subprocess.run(
            ["ps", "aux"],
            capture_output=True,
            text=True,
            check=True,
        )
        for line in ps_out.stdout.splitlines():
            if "adb" in line and "shell" in line and "/usr/bin/hiby_player" in line:
                parts = line.split()
                if len(parts) > 1:
                    pid_str = parts[1]
                    if pid_str.isdigit():
                        try:
                            os.kill(int(pid_str), signal.SIGTERM)
                            print(f"stopped matching adb holder pid={pid_str}")
                            stopped = True
                        except (ProcessLookupError, PermissionError):
                            pass
    except Exception:
        pass

    if not stopped:
        print("no held adb player process found")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Hold (or release) the HiBy player process on the R1 via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--start", action="store_true", help="Start a background adb holder for hiby_player.")
    parser.add_argument("--stop", action="store_true", help="Stop the held adb holder process.")
    parser.add_argument("--force-restart", action="store_true", help="Force-restart: stop everything, then start fresh.")
    return parser


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    if not args.start and not args.stop:
        args.start = True
    if args.start and args.stop:
        print("ERROR: choose only one of --start or --stop", file=sys.stderr)
        return 2

    try:
        adb = resolve_adb_path(args.adb)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parent.parent
    marker = repo_root / "work" / "adb-control" / "held-hiby-player-adb.pid"
    marker.parent.mkdir(parents=True, exist_ok=True)

    # --- Stop mode -----------------------------------------------------------
    if args.stop:
        stop_held_adb_process(marker)
        return 0

    # --- Start mode (possibly force-restart) ---------------------------------
    existing = get_remote_player_pids(adb)
    if existing and not args.force_restart:
        print(f"hiby_player already running: {', '.join(existing)}")
        print("use --force-restart only when the UI is wedged and you intentionally want to stop the player")
        return 0

    if args.force_restart:
        stop_held_adb_process(marker)
        try:
            adb_shell(adb,
                "for p in $(ps | sed -n '/hiby_player.sh/ { /sed/d; s/^ *\\([0-9][0-9]*\\).*/\\1/p; }'); "
                "do kill $p 2>/dev/null; done; sleep 0.2; killall hiby_player 2>/dev/null || true")
        except RuntimeError as exc:
            print(f"WARN: {exc}", file=sys.stderr)
        time.sleep(0.5)

    # Launch adb shell /usr/bin/hiby_player in the background.
    proc = subprocess.Popen(
        [adb, "shell", "/usr/bin/hiby_player"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    marker.write_text(str(proc.pid), encoding="ascii")
    print(f"started background adb holder pid={proc.pid}")
    time.sleep(2)

    running = get_remote_player_pids(adb)
    if not running:
        print("ERROR: hiby_player did not stay running", file=sys.stderr)
        return 2
    print(f"hiby_player running: {', '.join(running)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())