#!/usr/bin/env python3
"""Stage a verified firmware .upt package to the HiBy R1 device via ADB.

Converted from adb_stage_verified_firmware.ps1.
Verifies the local package against a known-bad MD5 blocklist, optionally
runs a local verification script, pushes the package to the device via ADB,
verifies temp/final byte counts and hashes, backs up existing staged firmware,
and reports success or failure with detailed hash comparison.
"""

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

KNOWN_BAD_MD5 = [
    # Flashed on 2026-06-09; rootfs repack left hiby_player non-executable.
    "2dc1152f096e84b3b8b52f809fc30e59",
    # Flashed on 2026-06-09; update reported success but booted to a black screen.
    "3bed523d5843522186164029139db7b1",
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def resolve_path_strict(path_value: str) -> Path:
    """Resolve a path and assert it exists."""
    p = Path(path_value).resolve()
    if not p.exists():
        raise FileNotFoundError(f"Missing path: {path_value}")
    return p


def resolve_adb_path(adb_arg: str) -> str:
    """Find the ADB executable.

    Order of precedence:
      1. Explicit --adb argument (must exist)
      2. .tools/platform-tools/adb under the repo root
      3. ``shutil.which('adb')``
      4. Hardcoded fallback /home/yetisoldier/.local/bin/adb
    """
    if adb_arg:
        p = Path(adb_arg)
        if p.exists():
            return str(p.resolve())
        raise FileNotFoundError(f"Missing adb path: {adb_arg}")

    # Look for repo-local platform-tools.
    # stage_firmware.py lives in <repo>/tools/, so repo root is two parents up.
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
        "or place adb at .tools/platform-tools/adb."
    )


def adb_shell(adb: str, command: str, *, check: bool = False) -> str:
    """Run ``adb shell <command>`` and return stdout."""
    result = subprocess.run(
        [adb, "shell", command],
        capture_output=True,
        text=True,
    )
    if check and result.returncode != 0:
        raise RuntimeError(
            f"adb shell failed (exit {result.returncode}): {command}\n"
            f"stderr: {result.stderr.strip()}"
        )
    return result.stdout.strip()


def get_remote_sha256_or_empty(adb: str, remote_path: str) -> str:
    """Return the SHA-256 of a remote file, or '' if unavailable."""
    output = adb_shell(adb, f"sha256sum '{remote_path}' 2>/dev/null || true")
    m = re.match(r"^([0-9a-fA-F]{64})\b", output)
    if m:
        return m.group(1).lower()
    return ""


def get_remote_size_or_empty(adb: str, remote_path: str) -> str:
    """Return the size in bytes of a remote file, or '' if unavailable."""
    # Try wc -c first.
    output = adb_shell(adb, f"wc -c < '{remote_path}' 2>/dev/null || true")
    m = re.search(r"([0-9]+)", output)
    if m:
        return m.group(1)

    # Fall back to stat.
    output = adb_shell(adb, f"stat -c %s '{remote_path}' 2>/dev/null || true")
    m = re.search(r"([0-9]+)", output)
    if m:
        return m.group(1)

    # Last resort: ls -l.
    output = adb_shell(adb, f"ls -l '{remote_path}' 2>/dev/null || true")
    for line in output.splitlines():
        m = re.match(r"^\S+\s+\S+\s+\S+\s+\S+\s+([0-9]+)\s+", line)
        if m:
            return m.group(1)
    return ""


def remove_remote_if_exists(adb: str, remote_path: str) -> None:
    """Remove a remote file if it exists (best-effort)."""
    adb_shell(adb, f"rm -f '{remote_path}' 2>/dev/null || true")


def adb_push(adb: str, source: str, destination: str) -> int:
    """Run ``adb push <source> <destination>`` and return the exit code."""
    result = subprocess.run(
        [adb, "push", source, destination],
        capture_output=True,
        text=True,
    )
    if result.stdout.strip():
        print(result.stdout.strip())
    if result.stderr.strip():
        print(result.stderr.strip())
    return result.returncode


def compute_md5(path: Path) -> str:
    """Compute the MD5 hex digest of a file."""
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compute_sha256(path: Path) -> str:
    """Compute the SHA-256 hex digest of a file."""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Stage a verified firmware .upt package to the HiBy R1 via ADB.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "--adb",
        default="",
        help="Path to the adb executable. If empty, auto-detect.",
    )
    parser.add_argument(
        "--package",
        default=r"work/audiobook-firmware-1.6.18-audiobook/r1-audiobooks-1.6.18-audiobook.upt",
        help="Path to the local .upt firmware package.",
    )
    parser.add_argument(
        "--build-out-dir",
        default="",
        help="Build output directory (defaults to parent of package).",
    )
    parser.add_argument(
        "--verify-script",
        default="tools/verify_r1_audiobook_build.py",
        help="Path to the local verification script.",
    )
    parser.add_argument(
        "--stock-rootfs",
        default="work/original/rootfs.squashfs",
        help="Path to the stock rootfs.squashfs.",
    )
    parser.add_argument(
        "--expected-version",
        default="1.6.18-audiobook",
        help="Expected firmware version string.",
    )
    parser.add_argument(
        "--expected-label",
        default="HiBy R1 Audiobook FW 1.6.18",
        help="Expected firmware label string.",
    )
    parser.add_argument(
        "--remote-final",
        default="/usr/data/mnt/sd_0/r1.upt",
        help="Remote final destination path on the device.",
    )

    # Switches → store_true flags.
    parser.add_argument("--expect-current-hashes", action="store_true")
    parser.add_argument("--expect-native-dsd", action="store_true")
    parser.add_argument("--expect-bluetooth-sbc-xq", action="store_true")
    parser.add_argument("--expect-usb-dac-mode", action="store_true")
    parser.add_argument("--expect-native-hub-title-row", action="store_true")
    parser.add_argument("--expect-native-hub-launcher", action="store_true")
    parser.add_argument("--expect-native-hub-folder-rows", action="store_true")
    parser.add_argument("--expect-native-hub-view-rows", action="store_true")
    parser.add_argument("--expect-private-direct-route", action="store_true")
    parser.add_argument(
        "--require-db-maintenance",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Require db-maintenance feature (use --no-require-db-maintenance to disable).",
    )
    parser.add_argument("--skip-local-verification", action="store_true")
    parser.add_argument("--no-backup-existing-final", action="store_true")
    parser.add_argument(
        "--i-understand-this-stages-firmware",
        action="store_true",
        help="Acknowledge that this script stages firmware on the device.",
    )

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if not args.i_understand_this_stages_firmware:
        print(
            "ERROR: Refusing to stage firmware without --i-understand-this-stages-firmware",
            file=sys.stderr,
        )
        return 2

    # --- Resolve ADB ---------------------------------------------------------
    try:
        adb = resolve_adb_path(args.adb)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    # --- Resolve package -----------------------------------------------------
    try:
        package_path = resolve_path_strict(args.package)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    package_size = package_path.stat().st_size
    if package_size < 1_048_576:
        print(
            f"ERROR: Refusing to stage suspiciously small package "
            f"({package_size} bytes): {package_path}",
            file=sys.stderr,
        )
        return 2

    local_md5 = compute_md5(package_path)
    local_sha256 = compute_sha256(package_path)

    if local_md5 in KNOWN_BAD_MD5:
        print(
            f"ERROR: Refusing to stage known-bad package MD5 {local_md5}",
            file=sys.stderr,
        )
        return 2

    # --- Local verification --------------------------------------------------
    if not args.skip_local_verification:
        try:
            verify_script_path = resolve_path_strict(args.verify_script)
            stock_rootfs_path = resolve_path_strict(args.stock_rootfs)
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2

        build_out_dir = args.build_out_dir or str(package_path.parent)
        try:
            build_out_dir_path = resolve_path_strict(build_out_dir)
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2

        upt_name = package_path.name
        verify_cmd = [
            sys.executable,
            str(verify_script_path),
            "--out-dir", str(build_out_dir_path),
            "--upt-name", upt_name,
            "--stock-rootfs", str(stock_rootfs_path),
            "--expected-version", args.expected_version,
            "--expected-label", args.expected_label,
        ]
        if args.expect_current_hashes:
            verify_cmd.append("--expect-current-hashes")
        if args.require_db_maintenance:
            verify_cmd.append("--require-db-maintenance")
        if args.expect_native_dsd:
            verify_cmd.append("--expect-native-dsd")
        if args.expect_bluetooth_sbc_xq:
            verify_cmd.append("--expect-sbc-xq")
        if args.expect_usb_dac_mode:
            verify_cmd.append("--expect-usb-dac-mode")
        if args.expect_native_hub_title_row:
            verify_cmd.append("--expect-native-hub-title-row")
        if args.expect_native_hub_launcher:
            verify_cmd.append("--expect-native-hub-launcher")
        if args.expect_native_hub_folder_rows:
            verify_cmd.append("--expect-native-hub-folder-rows")
        if args.expect_native_hub_view_rows:
            verify_cmd.append("--expect-native-hub-view-rows")
        if args.expect_private_direct_route:
            verify_cmd.append("--expect-private-direct-route")

        result = subprocess.run(verify_cmd)
        if result.returncode != 0:
            print(
                "ERROR: local firmware verification failed; refusing to stage",
                file=sys.stderr,
            )
            return 2

    # --- Resolve remote paths ------------------------------------------------
    remote_final = args.remote_final
    remote_final_dir = remote_final.rsplit("/", 1)[0] if "/" in remote_final else ""
    if not remote_final_dir or remote_final_dir == remote_final:
        print(
            f"ERROR: --remote-final must be an absolute path with a parent "
            f"directory: {remote_final}",
            file=sys.stderr,
        )
        return 2

    remote_tmp_dir = f"{remote_final_dir}/.r1-audiobook-staging"
    remote_tmp = f"{remote_tmp_dir}/r1.upt.uploading"

    # --- Check device connectivity -------------------------------------------
    result = subprocess.run([adb, "devices"])
    if result.returncode != 0:
        print("ERROR: adb devices failed", file=sys.stderr)
        return 2

    print(f"Local package: {package_path}")
    print(f"Local bytes:   {package_size}")
    print(f"Local MD5:     {local_md5}")
    print(f"Local SHA256:  {local_sha256}")
    print(f"Remote final:  {remote_final}")

    # --- Prepare temp directory ----------------------------------------------
    prep = subprocess.run(
        [adb, "shell", f"mkdir -p '{remote_tmp_dir}' && rm -f '{remote_tmp}'"],
        capture_output=True,
        text=True,
    )
    if prep.returncode != 0:
        print("ERROR: failed to prepare temp package path", file=sys.stderr)
        print(prep.stderr.strip(), file=sys.stderr)
        return 2

    # --- Push to temp --------------------------------------------------------
    push_exit = adb_push(adb, str(package_path), remote_tmp)
    if push_exit != 0:
        remove_remote_if_exists(adb, remote_tmp)
        print("ERROR: adb push failed", file=sys.stderr)
        return 2

    # --- Verify temp size ----------------------------------------------------
    remote_size = get_remote_size_or_empty(adb, remote_tmp)
    if remote_size != str(package_size):
        remove_remote_if_exists(adb, remote_tmp)
        print(
            f"ERROR: remote temp size mismatch: "
            f"local={package_size} remote={remote_size}",
            file=sys.stderr,
        )
        return 2

    # --- Verify temp MD5 -----------------------------------------------------
    remote_md5_output = adb_shell(adb, f"sync; md5sum '{remote_tmp}'", check=True)
    remote_md5 = remote_md5_output.split()[0].lower() if remote_md5_output else ""
    if remote_md5 != local_md5:
        remove_remote_if_exists(adb, remote_tmp)
        print(
            f"ERROR: remote temp MD5 mismatch: "
            f"local={local_md5} remote={remote_md5}",
            file=sys.stderr,
        )
        return 2

    # --- Verify temp SHA-256 -------------------------------------------------
    remote_sha256 = get_remote_sha256_or_empty(adb, remote_tmp)
    if remote_sha256:
        if remote_sha256 != local_sha256:
            remove_remote_if_exists(adb, remote_tmp)
            print(
                f"ERROR: remote temp SHA256 mismatch: "
                f"local={local_sha256} remote={remote_sha256}",
                file=sys.stderr,
            )
            return 2
    else:
        logger.warning(
            "remote sha256sum unavailable for temp package; "
            "continuing after byte count and MD5 verification"
        )

    # --- Backup existing final ----------------------------------------------
    remote_backup = ""
    remote_final_exists_output = adb_shell(
        adb,
        f"if [ -e '{remote_final}' ]; then echo yes; else echo no; fi",
        check=True,
    )
    remote_final_exists = remote_final_exists_output.strip() == "yes"

    if remote_final_exists and not args.no_backup_existing_final:
        existing_md5_output = adb_shell(
            adb, f"md5sum '{remote_final}' 2>/dev/null || true"
        )
        existing_md5 = ""
        m = re.match(r"^([0-9a-fA-F]{32})\b", existing_md5_output)
        if m:
            existing_md5 = m.group(1).lower()

        if not existing_md5 or existing_md5 != local_md5:
            timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
            remote_backup = f"{remote_final}.previous-{timestamp}.bak"
            print(f"Backing up existing remote final to: {remote_backup}")
            if existing_md5:
                print(f"Existing remote final MD5: {existing_md5}")
            backup_result = subprocess.run(
                [adb, "shell", f"mv '{remote_final}' '{remote_backup}'; sync"],
                capture_output=True,
                text=True,
            )
            if backup_result.returncode != 0:
                print("ERROR: remote final backup failed", file=sys.stderr)
                print(backup_result.stderr.strip(), file=sys.stderr)
                return 2
        else:
            print("Existing remote final already matches local package; backup skipped.")

    # --- Rename temp → final -------------------------------------------------
    rename_result = subprocess.run(
        [adb, "shell", f"mv '{remote_tmp}' '{remote_final}'; sync"],
        capture_output=True,
        text=True,
    )
    if rename_result.returncode != 0:
        remove_remote_if_exists(adb, remote_tmp)
        print("ERROR: remote final rename failed", file=sys.stderr)
        print(rename_result.stderr.strip(), file=sys.stderr)
        return 2

    # --- Verify final size ---------------------------------------------------
    remote_final_size = get_remote_size_or_empty(adb, remote_final)
    if remote_final_size != str(package_size):
        print(
            f"ERROR: remote final size mismatch: "
            f"local={package_size} remote={remote_final_size}",
            file=sys.stderr,
        )
        return 2

    # --- Verify final MD5 ----------------------------------------------------
    remote_final_md5_output = adb_shell(
        adb, f"md5sum '{remote_final}'", check=True
    )
    remote_final_md5 = (
        remote_final_md5_output.split()[0].lower()
        if remote_final_md5_output
        else ""
    )
    if remote_final_md5 != local_md5:
        print(
            f"ERROR: remote final MD5 mismatch: "
            f"local={local_md5} remote={remote_final_md5}",
            file=sys.stderr,
        )
        return 2

    # --- Verify final SHA-256 -----------------------------------------------
    remote_final_sha256 = get_remote_sha256_or_empty(adb, remote_final)
    if remote_final_sha256:
        if remote_final_sha256 != local_sha256:
            print(
                f"ERROR: remote final SHA256 mismatch: "
                f"local={local_sha256} remote={remote_final_sha256}",
                file=sys.stderr,
            )
            return 2
    else:
        logger.warning(
            "remote sha256sum unavailable for final package; "
            "continuing after byte count and MD5 verification"
        )

    # --- Success -------------------------------------------------------------
    print(f"Remote final bytes: {remote_final_size}")
    print(f"Remote final MD5:   {remote_final_md5}")
    if remote_final_sha256:
        print(f"Remote final SHA256: {remote_final_sha256}")
    if remote_backup:
        print(f"Previous remote final backup: {remote_backup}")

    # ls -l for visual confirmation.
    adb_shell(adb, f"ls -l '{remote_final}'")

    print("Staged firmware package successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())