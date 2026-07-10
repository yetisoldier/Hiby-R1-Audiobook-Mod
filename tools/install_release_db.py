#!/usr/bin/env python3
"""Install a pre-built release audiobook database to the HiBy R1 via ADB.

Pushes DB and catalog files with atomic rename, verifies integrity via MD5/size
checks, optionally moves backups to SD, restarts the resume daemon, or reboots.

Python port of adb_install_release_audiobook_db.ps1.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import logging
import re
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


def file_hash(path: Path, algorithm: str) -> str:
    """Compute a file hash."""
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def adb_run(adb: str, args: list[str], *, check: bool = True, allow_progress: bool = False) -> int:
    """Run an adb subcommand, return exit code. allow_progress suppresses errors for progress output."""
    cmd = [adb] + args
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.stdout.strip():
        logger.info("%s", proc.stdout.rstrip())
    if check and not allow_progress and proc.returncode != 0:
        raise RuntimeError(
            f"adb {' '.join(args)} failed with code {proc.returncode}\n{proc.stdout}"
        )
    return proc.returncode


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    """Run an adb shell command and return stdout."""
    proc = subprocess.run(
        [adb, "shell", command],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.stdout.strip():
        logger.debug("%s", proc.stdout.rstrip())
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"adb shell failed: {command}\n{proc.stdout}"
        )
    return proc.stdout


def get_remote_md5(adb: str, remote_path: str) -> str:
    """Get MD5 of a remote file."""
    output = adb_shell(adb, f"md5sum '{remote_path}' 2>/dev/null || true", check=False)
    match = re.match(r"^([0-9a-fA-F]{32})\b", output.strip())
    if match:
        return match.group(1).lower()
    return ""


def get_remote_size(adb: str, remote_path: str) -> int:
    """Get byte count of a remote file."""
    output = adb_shell(adb, f"wc -c < '{remote_path}'", check=False)
    digits = re.sub(r"[^0-9]", "", output)
    return int(digits) if digits else 0


def assert_remote_file_matches(adb: str, remote_path: str, local_path: Path, expected_md5: str) -> None:
    """Verify a remote file matches the local file by size and MD5."""
    local_size = local_path.stat().st_size
    remote_size = get_remote_size(adb, remote_path)
    if remote_size != local_size:
        raise RuntimeError(
            f"remote size mismatch for {remote_path}: local={local_size} remote={remote_size}"
        )
    remote_md5 = get_remote_md5(adb, remote_path)
    if remote_md5 != expected_md5:
        raise RuntimeError(
            f"remote MD5 mismatch for {remote_path}: local={expected_md5} remote={remote_md5}"
        )


def remote_leaf(remote_path: str) -> str:
    """Get the leaf name of a remote path."""
    return re.sub(r"^.*/", "", remote_path)


def run_python_script(script_path: Path, script_args: list[str]) -> None:
    """Run a Python script and check exit code."""
    cmd = [sys.executable, str(script_path)] + script_args
    logger.info("Running: %s", " ".join(cmd))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.stdout.strip():
        logger.info("%s", proc.stdout.rstrip())
    if proc.returncode != 0:
        raise RuntimeError(
            f"script failed with code {proc.returncode}: {' '.join(cmd)}\n{proc.stdout}"
        )


def install_release_db(args: argparse.Namespace) -> None:
    if not args.i_understand_this_modifies_device:
        raise RuntimeError(
            "Refusing to install release DB without --i-understand-this-modifies-device"
        )

    adb = resolve_adb(args.adb)
    db_path = require_path(Path(args.database))
    catalog_path = require_path(Path(args.catalog))

    repo_root = Path(__file__).resolve().parents[1]
    check_script = repo_root / "tools" / "check_audiobook_release_state.py"
    if args.check_script:
        check_script = require_path(Path(args.check_script))
    else:
        check_script = require_path(check_script)

    # Pre-install release-state check
    run_python_script(check_script, [str(db_path), "--catalog", str(catalog_path), "--expect-audiobooks"])

    db_md5 = file_hash(db_path, "md5")
    catalog_md5 = file_hash(catalog_path, "md5")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = Path(args.backup_out_dir) / stamp
    backup_dir.mkdir(parents=True, exist_ok=True)

    # Check device
    adb_run(adb, ["devices"])

    logger.info("Local DB:      %s", db_path)
    logger.info("Local DB MD5:  %s", db_md5)
    logger.info("Local catalog: %s", catalog_path)
    logger.info("Catalog MD5:   %s", catalog_md5)
    logger.info("Backup dir:    %s", backup_dir)

    # Pull existing DB for backup
    local_db_backup = backup_dir / "usrlocal_media.db.before"
    pull_db_code = adb_run(adb, ["pull", args.remote_db, str(local_db_backup)], check=False, allow_progress=True)
    if pull_db_code != 0:
        raise RuntimeError("failed to pull existing media DB backup")

    # Check and pull existing catalog
    catalog_presence = adb_shell(adb, f"if [ -e '{args.remote_catalog}' ]; then echo present; else echo missing; fi", check=False)
    (backup_dir / "remote-catalog-presence.txt").write_text(catalog_presence, encoding="utf-8")
    local_catalog_backup = backup_dir / "catalog.tsv.before"
    if "present" in catalog_presence:
        pull_catalog_code = adb_run(adb, ["pull", args.remote_catalog, str(local_catalog_backup)], check=False, allow_progress=True)
        if pull_catalog_code != 0:
            raise RuntimeError("failed to pull existing catalog backup")

    # Define remote paths
    remote_backup_db = f"{args.remote_db}.pre-release-{stamp}.bak"
    remote_backup_catalog = f"{args.remote_catalog}.pre-release-{stamp}.bak"
    remote_tmp_db = f"{args.remote_db}.uploading-{stamp}"
    remote_tmp_catalog = f"{args.remote_catalog}.uploading-{stamp}"

    # Create remote directories
    adb_shell(adb, "mkdir -p '/usr/data/audiobooks' '/usr/data/audiobooks/release-backups'")

    # Push temp files
    adb_run(adb, ["push", str(db_path), remote_tmp_db])
    assert_remote_file_matches(adb, remote_tmp_db, db_path, db_md5)

    adb_run(adb, ["push", str(catalog_path), remote_tmp_catalog])
    assert_remote_file_matches(adb, remote_tmp_catalog, catalog_path, catalog_md5)

    # Atomic install: backup old, move new into place
    install_cmd = (
        f"cp -p '{args.remote_db}' '{remote_backup_db}' && "
        f"if [ -e '{args.remote_catalog}' ]; then cp -p '{args.remote_catalog}' '{remote_backup_catalog}'; fi && "
        f"mv '{remote_tmp_db}' '{args.remote_db}' && "
        f"mv '{remote_tmp_catalog}' '{args.remote_catalog}' && "
        f"tail -n +2 '{args.remote_catalog}' | cut -f1 | sed '/^$/d' | sort -u > /usr/data/audiobooks/catalog-roots.txt && "
        f"tail -n +2 '{args.remote_catalog}' | cut -f7 | sed '/^$/d' | sort -u > /usr/data/audiobooks/catalog-albums.txt && "
        "sync"
    )
    adb_shell(adb, install_cmd)

    # Verify installed files
    assert_remote_file_matches(adb, args.remote_db, db_path, db_md5)
    assert_remote_file_matches(adb, args.remote_catalog, catalog_path, catalog_md5)

    logger.info("Installed release DB and catalog.")
    logger.info("Remote DB backup:      %s", remote_backup_db)
    logger.info("Remote catalog backup: %s", remote_backup_catalog)

    # Move backups to SD if requested
    if args.move_remote_backups_to_sd:
        remote_backup_sd_dir = f"{args.remote_backup_sd_root}/release-db-{stamp}"
        adb_shell(adb, f"mkdir -p '{remote_backup_sd_dir}'")

        local_db_backup_md5 = file_hash(local_db_backup, "md5")
        sd_db_backup = f"{remote_backup_sd_dir}/{remote_leaf(remote_backup_db)}"
        adb_run(adb, ["push", str(local_db_backup), f"{sd_db_backup}.tmp"])
        assert_remote_file_matches(adb, f"{sd_db_backup}.tmp", local_db_backup, local_db_backup_md5)
        adb_shell(adb, f"mv '{sd_db_backup}.tmp' '{sd_db_backup}'")
        assert_remote_file_matches(adb, sd_db_backup, local_db_backup, local_db_backup_md5)

        rm_internal = f"rm -f '{remote_backup_db}'"
        if local_catalog_backup.exists():
            local_catalog_backup_md5 = file_hash(local_catalog_backup, "md5")
            sd_catalog_backup = f"{remote_backup_sd_dir}/{remote_leaf(remote_backup_catalog)}"
            adb_run(adb, ["push", str(local_catalog_backup), f"{sd_catalog_backup}.tmp"])
            assert_remote_file_matches(adb, f"{sd_catalog_backup}.tmp", local_catalog_backup, local_catalog_backup_md5)
            adb_shell(adb, f"mv '{sd_catalog_backup}.tmp' '{sd_catalog_backup}'")
            assert_remote_file_matches(adb, sd_catalog_backup, local_catalog_backup, local_catalog_backup_md5)
            rm_internal += f" '{remote_backup_catalog}'"
            logger.info("SD catalog backup:     %s", sd_catalog_backup)

        adb_shell(adb, f"{rm_internal} && sync")
        logger.info("SD DB backup:          %s", sd_db_backup)
        logger.info("Moved verified remote backups to SD to save internal /usr/data space.")

    # Restart resume daemon if requested (and not rebooting)
    if args.restart_resume_daemon and not args.reboot_after_install:
        adb_shell(adb, "/etc/init.d/S91audiobook_resume.sh start")
        logger.info("Restarted resume daemon.")

    # Reboot if requested
    if args.reboot_after_install:
        logger.info("Rebooting device. ADB may need to be manually re-enabled afterward.")
        adb_shell(adb, "sync; reboot")
    else:
        logger.info("Device not rebooted. Reboot before final UI verification so hiby_player reloads the DB cleanly.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Install a pre-built release audiobook database to the R1 via ADB."
    )
    parser.add_argument("--adb", default="", help="Path to adb binary.")
    parser.add_argument("--database", required=True, help="Local path to the release DB file.")
    parser.add_argument("--catalog", required=True, help="Local path to the release catalog TSV.")
    parser.add_argument("--check-script", default="tools/check_audiobook_release_state.py", help="Release-state check script.")
    parser.add_argument("--remote-db", default="/usr/data/usrlocal_media.db", help="Remote DB path.")
    parser.add_argument("--remote-catalog", default="/usr/data/audiobooks/catalog.tsv", help="Remote catalog path.")
    parser.add_argument("--backup-out-dir", default="work/release-db-install-backups", help="Local backup output directory.")
    parser.add_argument("--remote-backup-sd-root", default="/usr/data/mnt/sd_0/.r1-audiobook-backups", help="Remote SD backup root.")
    parser.add_argument("--restart-resume-daemon", action="store_true", help="Restart resume daemon after install.")
    parser.add_argument("--reboot-after-install", action="store_true", help="Reboot device after install.")
    parser.add_argument("--move-remote-backups-to-sd", action="store_true", help="Move remote backups to SD card.")
    parser.add_argument(
        "--i-understand-this-modifies-device",
        action="store_true",
        help="Acknowledge that this script modifies the device.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        install_release_db(args)
    except Exception as exc:
        logger.error("error: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())