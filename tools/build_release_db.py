#!/usr/bin/env python3
"""Build a release audiobook database on the HiBy R1 via ADB.

Pulls the device media DB, runs add_audiobooks_to_media_db.py with ADB scanning,
generates a resume catalog, verifies release-state, and writes hash summaries.

Python port of adb_build_release_audiobook_db.ps1.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import logging
import shutil
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

FALLBACK_ADB = "/home/yetisoldier/.local/bin/adb"


def resolve_adb(adb_arg: str) -> str:
    """Locate the adb binary."""
    if adb_arg:
        candidate = Path(adb_arg)
        if candidate.exists():
            return str(candidate)
        found = shutil.which(adb_arg)
        if found:
            return found
    found = shutil.which("adb")
    if found:
        return found
    if Path(FALLBACK_ADB).exists():
        return FALLBACK_ADB
    raise RuntimeError(
        f"ADB not found. Install platform-tools, add adb to PATH, "
        f"or place adb at {FALLBACK_ADB}."
    )


def require_path(path: Path) -> Path:
    """Resolve and validate that a path exists."""
    if not path.exists():
        raise FileNotFoundError(f"Missing path: {path}")
    return path.resolve()


def run_command(cmd: list[str], *, tee: Path | None = None) -> str:
    """Run a command, optionally teeing output to a file."""
    logger.info("Running: %s", " ".join(cmd))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    output = proc.stdout
    if tee:
        tee.parent.mkdir(parents=True, exist_ok=True)
        tee.write_text(output, encoding="utf-8")
    logger.info("%s", output.rstrip())
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed with code {proc.returncode}: {' '.join(cmd)}\n{output}"
        )
    return output


def file_hash(path: Path, algorithm: str) -> str:
    """Compute a file hash."""
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def build_release_db(args: argparse.Namespace) -> None:
    adb = resolve_adb(args.adb)
    adb_path = Path(adb)
    require_path(adb_path)

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    target = Path(args.out_dir) / stamp
    target.mkdir(parents=True, exist_ok=True)

    source_db = target / "usrlocal_media.source.db"
    candidate_db = target / "usrlocal_media.release-candidate.db"
    catalog = target / "catalog.release-candidate.tsv"
    hashes_file = target / "hashes.txt"

    # Check device
    devices_output = run_command([adb, "devices"])
    (target / "adb-devices.txt").write_text(devices_output, encoding="utf-8")

    # Pull source DB
    run_command([adb, "pull", args.source_db_remote, str(source_db)])

    # Build release candidate DB
    repo_root = Path(__file__).resolve().parents[1]
    add_script = repo_root / "tools" / "add_audiobooks_to_media_db.py"
    require_path(add_script)

    add_args = [
        sys.executable, str(add_script), str(source_db),
        "--adb-scan",
        "--adb", adb,
        "--device-root", args.device_audiobooks_root,
        "--music-catalog-excludes-audiobooks",
        "--id-base", str(args.id_base),
        "-o", str(candidate_db),
    ]
    if not args.skip_adb_sizes:
        add_args.append("--adb-sizes")

    run_command(add_args, tee=target / "build-db.log")

    # Build resume catalog
    catalog_script = repo_root / "tools" / "write_audiobook_resume_catalog.py"
    require_path(catalog_script)
    run_command(
        [sys.executable, str(catalog_script), str(candidate_db), "-o", str(catalog)],
        tee=target / "build-catalog.log",
    )

    # Verify release state
    check_script = repo_root / "tools" / "check_audiobook_release_state.py"
    require_path(check_script)
    run_command(
        [sys.executable, str(check_script), str(candidate_db),
         "--catalog", str(catalog), "--expect-audiobooks"],
        tee=target / "release-check.log",
    )

    # Write hashes
    candidate_md5 = file_hash(candidate_db, "md5")
    candidate_sha256 = file_hash(candidate_db, "sha256")
    catalog_md5 = file_hash(catalog, "md5")
    catalog_sha256 = file_hash(catalog, "sha256")

    lines = [
        f"source_db={source_db}",
        f"candidate_db={candidate_db}",
        f"catalog={catalog}",
        f"candidate_db_md5={candidate_md5}",
        f"candidate_db_sha256={candidate_sha256}",
        f"catalog_md5={catalog_md5}",
        f"catalog_sha256={catalog_sha256}",
    ]
    hashes_content = "\n".join(lines) + "\n"
    hashes_file.write_text(hashes_content, encoding="ascii")
    logger.info("%s", hashes_content.rstrip())
    logger.info("Release DB candidate directory: %s", target)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build a release audiobook database on the R1 via ADB."
    )
    parser.add_argument("--adb", default="", help="Path to adb binary.")
    parser.add_argument("--out-dir", default="work/release-db-candidate", help="Output directory for candidate builds.")
    parser.add_argument("--id-base", type=int, default=1000, help="Starting ID for audiobook entries.")
    parser.add_argument("--device-audiobooks-root", default="/usr/data/mnt/sd_0/Audiobooks", help="Device audiobooks root path.")
    parser.add_argument("--source-db-remote", default="/usr/data/usrlocal_media.db", help="Remote path to source media DB.")
    parser.add_argument("--skip-adb-sizes", action="store_true", help="Skip ADB file size queries.")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        build_release_db(args)
    except Exception as exc:
        logger.error("error: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())