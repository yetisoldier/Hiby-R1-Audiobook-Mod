#!/usr/bin/env python3
"""Live-test a patched hiby_player on the R1, or restore stock player.

Converted from adb_live_test_patched_player.ps1.
Pushes a patched hiby_player binary to the device and starts it in place of
the stock player, or stops the patched player and restarts the stock one.
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

DEFAULT_PLAYER = "work/audiobook-firmware/hiby_player.audiobooks"

STOP_PLAYER_SCRIPT = (
    "ps | sed -n '/hiby_player/ { /\\/bin\\/sh -c/d; /grep/d; "
    "s/^ *\\([0-9][0-9]*\\).*/\\1/p; }' | while read p; do kill -9 \"$p\" 2>/dev/null; done; sleep 1"
)


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


def invoke_adb(adb: str, arguments: list[str]) -> None:
    """Run an adb command, raising on failure."""
    result = subprocess.run([adb, *arguments])
    if result.returncode != 0:
        raise RuntimeError(f"adb failed: {' '.join(arguments)}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Live-test a patched hiby_player on the R1, or restore stock player.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--player", default=DEFAULT_PLAYER, help="Path to the patched hiby_player binary.")
    parser.add_argument("--start-patched", action="store_true", help="Start the patched player on the device.")
    parser.add_argument("--restore-stock", action="store_true", help="Restore the stock hiby_player.")
    parser.add_argument(
        "--i-understand-this-restarts-ui",
        action="store_true",
        help="Acknowledge that this restarts the R1 UI.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.start_patched and args.restore_stock:
        print("ERROR: Choose only one mode: --start-patched or --restore-stock.", file=sys.stderr)
        return 2
    if not args.start_patched and not args.restore_stock:
        print("ERROR: Choose --start-patched or --restore-stock.", file=sys.stderr)
        return 2

    try:
        adb = resolve_adb_path(args.adb)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if args.start_patched:
        if not args.i_understand_this_restarts_ui:
            print(
                "ERROR: Starting the patched player restarts the R1 UI. "
                "Re-run with --i-understand-this-restarts-ui after explicit approval.",
                file=sys.stderr,
            )
            return 2

        try:
            player_path = resolve_path_strict(args.player)
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2

        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        remote_dir = f"/usr/data/codex_audiobook_test_{stamp}"
        remote_player = f"{remote_dir}/hiby_player"
        repo_root = Path(__file__).resolve().parent.parent
        local_state = repo_root / "work" / "audiobook-firmware" / "last-live-test-remote.txt"

        invoke_adb(adb, ["shell", f"mkdir -p '{remote_dir}'"])
        invoke_adb(adb, ["push", str(player_path), remote_player])
        invoke_adb(adb, ["shell", f"chmod 755 '{remote_player}'"])
        invoke_adb(adb, ["shell",
            f"{STOP_PLAYER_SCRIPT}; cd /; nohup setsid '{remote_player}' "
            f"> '{remote_dir}/hiby_player.log' 2>&1 < /dev/null & sleep 2"])

        local_state.parent.mkdir(parents=True, exist_ok=True)
        local_state.write_text(remote_dir, encoding="ascii")

        result = subprocess.run([adb, "shell", "ps | grep hiby_player"])
        print(f"Patched player started from {remote_player}")
        print("Use --restore-stock or reboot the device to return to stock runtime.")
        return 0

    if args.restore_stock:
        invoke_adb(adb, ["shell",
            f"{STOP_PLAYER_SCRIPT}; cd /; nohup setsid /usr/bin/hiby_player "
            f"> /usr/data/codex_audiobook_stock_restart.log 2>&1 < /dev/null & sleep 2"])
        result = subprocess.run([adb, "shell", "ps | grep hiby_player"])
        print("Stock hiby_player.sh restart requested.")
        return 0

    return 0


if __name__ == "__main__":
    sys.exit(main())