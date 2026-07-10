#!/usr/bin/env python3
"""Stage a firmware .upt package to SD card or ADB device.

Converted from stage_r1_firmware_package.ps1.
On Linux, auto-detects mounted SD cards with R1-like directory structure,
or uses ADB if a device is connected.  Verifies the package locally before
staging, and writes with atomic rename + hash verification.
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import os
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


def resolve_adb_path_or_empty(adb_arg: str) -> str:
    """Find the ADB executable, or return empty string if not found."""
    if adb_arg:
        p = Path(adb_arg)
        if p.exists():
            return str(p.resolve())
        return ""

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    repo_adb = repo_root / ".tools" / "platform-tools" / "adb"
    if repo_adb.exists():
        return str(repo_adb.resolve())

    which = shutil.which("adb")
    if which:
        return which

    return ""


def test_adb_device(adb: str) -> bool:
    """Return True if an ADB device is connected."""
    if not adb:
        return False
    result = subprocess.run([adb, "devices"], capture_output=True, text=True)
    if result.returncode != 0:
        return False
    return "\tdevice" in result.stdout


def compute_md5(path: Path) -> str:
    """Compute MD5 hex digest of a file."""
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compute_sha256(path: Path) -> str:
    """Compute SHA-256 hex digest of a file."""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def invoke_local_verification(package_path: Path, build_dir: str, args) -> None:
    """Run local firmware verification."""
    if args.skip_local_verification:
        return

    verify_script = Path(__file__).resolve().parent / "verify_r1_audiobook_build.py"
    if not verify_script.exists():
        print(f"WARN: verification script not found: {verify_script}", file=sys.stderr)
        return

    cmd = [
        sys.executable, str(verify_script),
        "--out-dir", build_dir,
        "--upt-name", package_path.name,
        "--expected-version", args.expected_version,
        "--expected-label", args.expected_label,
        "--require-db-maintenance",
        "--expect-batd-disabled",
        "--expect-audiobook-launcher-icon",
    ]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        raise RuntimeError("local firmware verification failed; refusing to stage")


def get_candidate_sd_roots(sd_root: str = "") -> list[Path]:
    """Find candidate SD card roots with R1-like structure."""
    if sd_root:
        root = Path(sd_root).resolve()
        if not root.exists():
            raise FileNotFoundError(f"SD root does not exist: {root}")
        return [root]

    roots: list[Path] = []
    # On Linux, check /media and /mnt for mounted removable media.
    search_dirs = [Path("/media"), Path("/mnt")]
    for search_dir in search_dirs:
        if not search_dir.exists():
            continue
        for entry in search_dir.iterdir():
            if not entry.is_dir():
                continue
            has_r1_shape = (
                (entry / "Music").exists()
                or (entry / "Audiobooks").exists()
                or (entry / "r1.upt").exists()
            )
            if has_r1_shape:
                roots.append(entry)
    return roots


def stage_to_sd_root(package_path: Path, sd_root: Path) -> None:
    """Stage the package to an SD card root with atomic rename and verification."""
    package_size = package_path.stat().st_size
    if package_size < 1_048_576:
        raise RuntimeError(
            f"Refusing suspiciously small package ({package_size} bytes): {package_path}"
        )

    local_md5 = compute_md5(package_path)
    local_sha256 = compute_sha256(package_path)

    final = sd_root / "r1.upt"
    tmp = sd_root / "r1.upt.uploading"
    backup = ""

    if tmp.exists():
        tmp.unlink()

    if final.exists():
        existing_md5 = compute_md5(final)
        if existing_md5 != local_md5:
            timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
            backup = final.with_name(f"r1.upt.previous-{timestamp}.bak")
            final.rename(backup)

    # Copy to temp.
    shutil.copy2(package_path, tmp)
    tmp_size = tmp.stat().st_size
    if tmp_size != package_size:
        tmp.unlink()
        raise RuntimeError(
            f"temporary SD copy size mismatch: local={package_size} sd={tmp_size}"
        )
    tmp_md5 = compute_md5(tmp)
    if tmp_md5 != local_md5:
        tmp.unlink()
        raise RuntimeError(
            f"temporary SD copy MD5 mismatch: local={local_md5} sd={tmp_md5}"
        )

    # Atomic rename.
    tmp.rename(final)
    final_size = final.stat().st_size
    final_md5 = compute_md5(final)
    final_sha256 = compute_sha256(final)
    if final_size != package_size or final_md5 != local_md5 or final_sha256 != local_sha256:
        raise RuntimeError("final SD package verification failed")

    print(f"Staged firmware on SD root: {sd_root}")
    print(f"Final file: {final}")
    print(f"Bytes:      {final_size}")
    print(f"MD5:        {final_md5}")
    print(f"SHA256:     {final_sha256}")
    if backup:
        print(f"Backup:     {backup}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Stage a firmware .upt package to SD card or ADB device.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--package",
        default="work/audiobook-firmware-1.6.28-sd-ready-dev/r1-audiobooks-1.6.28-sd-ready-dev.upt",
        help="Path to the .upt firmware package.",
    )
    parser.add_argument("--build-out-dir", default="", help="Build output directory.")
    parser.add_argument("--expected-version", default="1.6.28-sd-ready-dev", help="Expected firmware version string.")
    parser.add_argument("--expected-label", default="HiBy R1 Audiobook FW 1.6.28", help="Expected firmware label string.")
    parser.add_argument("--sd-root", default="", help="Explicit SD card root path.")
    parser.add_argument("--adb", default="", help="Path to the adb executable.")
    parser.add_argument("--skip-local-verification", action="store_true", help="Skip local firmware verification.")
    parser.add_argument(
        "--i-understand-this-stages-firmware",
        action="store_true",
        help="Acknowledge that this script stages firmware.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    if not args.i_understand_this_stages_firmware:
        print(
            "ERROR: Refusing to stage firmware without --i-understand-this-stages-firmware",
            file=sys.stderr,
        )
        return 2

    try:
        package_path = resolve_path_strict(args.package)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    build_out_dir = args.build_out_dir or str(package_path.parent)

    # --- Local verification --------------------------------------------------
    try:
        invoke_local_verification(package_path, build_out_dir, args)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    # --- Check for ADB device ------------------------------------------------
    adb = resolve_adb_path_or_empty(args.adb)
    if test_adb_device(adb):
        print("ADB device detected; staging through stage_firmware.py.")
        stage_script = Path(__file__).resolve().parent / "stage_firmware.py"
        cmd = [
            sys.executable, str(stage_script),
            "--adb", adb,
            "--package", str(package_path),
            "--build-out-dir", build_out_dir,
            "--expected-version", args.expected_version,
            "--expected-label", args.expected_label,
            "--i-understand-this-stages-firmware",
        ]
        if args.skip_local_verification:
            cmd.append("--skip-local-verification")
        result = subprocess.run(cmd)
        return result.returncode

    # --- Find SD card root ---------------------------------------------------
    try:
        candidates = get_candidate_sd_roots(args.sd_root)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if not candidates:
        print(
            "ERROR: No ADB device and no SD card root with Music, Audiobooks, or r1.upt was found.",
            file=sys.stderr,
        )
        return 2
    if len(candidates) > 1:
        names = ", ".join(str(c) for c in candidates)
        print(f"ERROR: Multiple possible SD roots found: {names}. Re-run with --sd-root <path>.", file=sys.stderr)
        return 2

    try:
        stage_to_sd_root(package_path, candidates[0])
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())