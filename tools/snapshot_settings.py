#!/usr/bin/env python3
"""Snapshot R1 settings files via ADB.

Converted from adb_snapshot_r1_settings.ps1.
Pulls small settings/config files from the device into a timestamped
local directory, creates a manifest with sizes and MD5 hashes, and
optionally compares against a previous snapshot.
"""

from __future__ import annotations

import argparse
import hashlib
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


def adb_shell_capture(adb: str, command: str) -> str:
    """Run ``adb shell <command>`` and return stdout."""
    result = subprocess.run(
        [adb, "shell", command],
        capture_output=True,
        text=True,
    )
    return result.stdout


# Shell scripts for device data collection.
DEVICE_INFO_COMMAND = r"""echo "date=$(date 2>/dev/null)"
echo "kernel=$(uname -a 2>/dev/null)"
echo
echo "== audiobook version =="
cat /etc/r1_audiobook_version 2>/dev/null || true
echo
echo "== boot adb files =="
ls -l /etc/init.d/T90adb /etc/init.d/S90adb /usr/data/disableadb 2>/dev/null || true
echo
echo "== process list =="
ps 2>/dev/null || true
echo
echo "== mounts =="
mount 2>/dev/null || true
"""

MANIFEST_COMMAND = r"""echo "## targeted /usr/data settings"
for f in /usr/data/user.ini /usr/data/menu_cfg /usr/data/*.ini /usr/data/*.conf /usr/data/*cfg /usr/data/bluetooth/*/settings; do
    [ -f $f ] || continue
    set -- $(wc -c $f 2>/dev/null)
    size=${1:-}
    set -- $(md5sum $f 2>/dev/null)
    hash=${1:-}
    echo $size $hash $f
done
"""

CANDIDATE_COMMAND = r"""for f in /usr/data/user.ini /usr/data/menu_cfg /usr/data/*.ini /usr/data/*.conf /usr/data/*cfg /usr/data/bluetooth/*/settings; do
    [ -f $f ] || continue
    set -- $(wc -c $f 2>/dev/null)
    size=${1:-}
    if [ -n "$size" ] && [ "$size" -le 262144 ]; then
        echo $size $f
    fi
done
"""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Snapshot R1 settings files via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--label", default="snapshot", help="Label for the snapshot directory name.")
    parser.add_argument("--out-dir", default="work/settings-snapshots", help="Output directory root.")
    parser.add_argument("--compare-to", default="", help="Path to a previous snapshot to compare against.")
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
    safe_label = to_safe_name(args.label)
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    snapshot_root = repo_root / args.out_dir
    snapshot_dir = snapshot_root / f"{timestamp}-{safe_label}"
    files_dir = snapshot_dir / "files"
    files_dir.mkdir(parents=True, exist_ok=True)

    # --- adb devices ---------------------------------------------------------
    devices_result = subprocess.run([adb, "devices"], capture_output=True, text=True)
    (snapshot_dir / "adb-devices.txt").write_text(devices_result.stdout, encoding="utf-8")
    print(devices_result.stdout, end="")
    if devices_result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    # --- device-info.txt -----------------------------------------------------
    info_output = adb_shell_capture(adb, DEVICE_INFO_COMMAND)
    (snapshot_dir / "device-info.txt").write_text(info_output, encoding="utf-8")

    # --- settings-manifest.tsv ----------------------------------------------
    manifest_output = adb_shell_capture(adb, MANIFEST_COMMAND)
    (snapshot_dir / "settings-manifest.tsv").write_text(manifest_output, encoding="utf-8")

    # --- candidate-files.tsv -------------------------------------------------
    candidate_output = adb_shell_capture(adb, CANDIDATE_COMMAND)
    (snapshot_dir / "candidate-files.tsv").write_text(candidate_output, encoding="utf-8")

    # --- Pull candidate files ------------------------------------------------
    pull_log = snapshot_dir / "pull.log"
    pull_log.write_text("# Pulled small settings/config candidates\n", encoding="utf-8")

    candidates_text = candidate_output.strip()
    for line in candidates_text.splitlines():
        line = line.strip()
        if not line:
            continue
        # Parse "size path" format.
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        size_text, remote_path = parts[0].strip(), parts[1].strip()
        if not size_text.isdigit():
            continue
        if not remote_path.startswith("/"):
            continue

        # Sanitize remote path to a local filename.
        trimmed = remote_path.lstrip("/")
        local_name = re.sub(r'[\\/:*?"<>| ]+', "_", trimmed)
        dest_path = files_dir / local_name

        with pull_log.open("a", encoding="utf-8") as f:
            f.write(f"PULL {remote_path} -> {dest_path}\n")

        pull_result = subprocess.run(
            [adb, "pull", remote_path, str(dest_path)],
            capture_output=True,
            text=True,
        )
        with pull_log.open("a", encoding="utf-8") as f:
            f.write(pull_result.stdout)
            if pull_result.stderr:
                f.write(pull_result.stderr)
            if pull_result.returncode != 0:
                f.write(f"WARN pull failed for {remote_path}\n")

    # --- Compare -------------------------------------------------------------
    if args.compare_to:
        try:
            compare_path = resolve_path_strict(args.compare_to)
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        compare_script = Path(__file__).resolve().parent / "compare_binary_settings.py"
        if compare_script.exists():
            result = subprocess.run(
                [sys.executable, str(compare_script),
                 "--before", str(compare_path),
                 "--after", str(snapshot_dir)],
            )
            if result.returncode != 0:
                print("ERROR: settings snapshot comparison failed", file=sys.stderr)
                return 2

    print(f"Snapshot saved to {snapshot_dir}")
    print("Use a before/after pair around a UI setting change to identify persisted settings.")
    return 0


if __name__ == "__main__":
    sys.exit(main())