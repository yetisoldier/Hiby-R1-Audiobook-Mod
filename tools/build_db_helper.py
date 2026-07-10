#!/usr/bin/env python3
"""Build the MIPS C DB maintenance helper for the HiBy R1.

Cross-compiles ``tools/r1_audiobook_db_maint.c`` together with the SQLite
amalgamation using the Zig C compiler (``zig cc -target mipsel-linux-musleabi``).
The output is a static MIPS ELF binary suitable for the R1 device.

Converted from ``tools/build_r1_db_maint_helper.ps1``.
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[1]
DEPS_DIR = REPO_ROOT / ".deps"
SQLITE_DIR = DEPS_DIR / "sqlite"

DEFAULT_OUT_FILE = "work/native-db-maint/r1_audiobook_db_maint"
DEFAULT_SQLITE_VERSION = "3530200"

# Known-good SHA3-256 of the SQLite amalgamation zip.
SQLITE_SHA3_256 = "81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40"

# Zig compiler search order: explicit env, known Linux path, PATH.
ZIG_CANDIDATES = [
    Path("/home/yetisoldier/tools/zig/zig"),
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def resolve_zig() -> Path:
    """Locate the Zig executable."""
    for candidate in ZIG_CANDIDATES:
        if candidate.is_file():
            return candidate
    found = shutil.which("zig")
    if found:
        return Path(found)
    raise FileNotFoundError(
        "Zig compiler not found. Expected at /home/yetisoldier/tools/zig/zig "
        "or on PATH."
    )


def sha3_256_file(path: Path) -> str:
    """Return the SHA3-256 hex digest of *path*."""
    h = hashlib.sha3_256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def download_if_missing(url: str, destination: Path) -> None:
    """Download *url* to *destination* if it does not already exist."""
    if destination.exists():
        log.info("Already present: %s", destination)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    log.info("Downloading %s → %s", url, destination)
    urllib.request.urlretrieve(url, str(destination))


def extract_zip(zip_path: Path, dest_dir: Path) -> None:
    """Extract *zip_path* into *dest_dir* using the stdlib zipfile module."""
    import zipfile

    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(dest_dir)


def ensure_sqlite(sqlite_version: str) -> Path:
    """Ensure the SQLite amalgamation is available; return its directory.

    Falls back to system SQLite headers if the amalgamation cannot be
    downloaded and system headers are present.
    """
    sqlite_zip_name = f"sqlite-amalgamation-{sqlite_version}.zip"
    sqlite_zip = SQLITE_DIR / sqlite_zip_name
    extracted_dir = SQLITE_DIR / f"sqlite-amalgamation-{sqlite_version}"
    sqlite_c = extracted_dir / "sqlite3.c"

    if sqlite_c.exists():
        log.info("SQLite amalgamation already available: %s", extracted_dir)
        return extracted_dir

    # Try downloading the amalgamation.
    sqlite_url = f"https://sqlite.org/2026/{sqlite_zip_name}"
    try:
        download_if_missing(sqlite_url, sqlite_zip)
        # Verify hash.
        actual = sha3_256_file(sqlite_zip)
        if actual != SQLITE_SHA3_256:
            log.warning(
                "SHA3-256 mismatch for %s (expected %s, got %s). "
                "Falling back to system SQLite.",
                sqlite_zip, SQLITE_SHA3_256, actual,
            )
            raise RuntimeError("hash mismatch")
        extract_zip(sqlite_zip, SQLITE_DIR)
        if sqlite_c.exists():
            return extracted_dir
    except Exception as exc:
        log.warning("Could not download SQLite amalgamation: %s", exc)

    # Fall back to system SQLite.
    system_header = Path("/usr/include/sqlite3.h")
    if system_header.exists():
        log.info("Using system SQLite headers from /usr/include")
        return Path("/usr/include")

    raise FileNotFoundError(
        "SQLite amalgamation not available and libsqlite3-dev not installed. "
        f"Run: sudo apt install libsqlite3-dev  (or download amalgamation "
        f"{sqlite_version} into {SQLITE_DIR})"
    )


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def build(args: argparse.Namespace) -> Path:
    zig = resolve_zig()

    # Resolve output path.
    out_file = Path(args.out_file)
    if not out_file.is_absolute():
        out_file = REPO_ROOT / out_file
    out_file.parent.mkdir(parents=True, exist_ok=True)

    # Ensure SQLite.
    sqlite_dir = ensure_sqlite(args.sqlite_version)

    # Source files.
    source = REPO_ROOT / "tools" / "r1_audiobook_db_maint.c"
    sqlite_source = sqlite_dir / "sqlite3.c"

    # Build the zig cc command.
    cmd: list[str | Path] = [
        zig,
        "cc",
        "-target", "mipsel-linux-musleabi",
        "-static",
        "-Os",
        "-s",
        "-I", sqlite_dir,
        "-DSQLITE_THREADSAFE=0",
        "-DSQLITE_OMIT_LOAD_EXTENSION",
        "-DSQLITE_DEFAULT_MEMSTATUS=0",
        "-DSQLITE_OMIT_DEPRECATED",
        source,
    ]

    # Only include sqlite3.c if it exists (amalgamation mode).
    if sqlite_source.exists():
        cmd.append(sqlite_source)
    else:
        # System SQLite mode — link against libsqlite3 dynamically.
        # On musl cross-compile this typically won't work; warn the user.
        log.warning(
            "sqlite3.c not found in %s. If the build fails, install the "
            "SQLite amalgamation or libsqlite3-dev.",
            sqlite_dir,
        )

    cmd.extend(["-o", out_file])

    log.info("Running: %s", " ".join(str(c) for c in cmd))
    result = subprocess.run([str(c) for c in cmd])
    if result.returncode != 0:
        raise RuntimeError(f"Failed to build {out_file}")

    log.info("Built: %s (%d bytes)", out_file, out_file.stat().st_size)
    return out_file


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--out-file",
        default=DEFAULT_OUT_FILE,
        help=f"Output binary path (default: {DEFAULT_OUT_FILE})",
    )
    parser.add_argument(
        "--sqlite-version",
        default=DEFAULT_SQLITE_VERSION,
        help=f"SQLite amalgamation version (default: {DEFAULT_SQLITE_VERSION})",
    )
    return parser


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s: %(message)s",
    )
    args = build_parser().parse_args()
    build(args)


if __name__ == "__main__":
    main()