#!/usr/bin/env python3
"""Build the MIPS direct-open helper for the HiBy R1.

Cross-compiles ``tools/r1_audiobook_direct_open.c`` using the Zig C compiler
(``zig cc -target mipsel-linux-musleabi``).  The output is a static MIPS ELF
binary suitable for the R1 device.

Converted from ``tools/build_r1_direct_open_helper.ps1``.
"""

from __future__ import annotations

import argparse
import logging
import shutil
import subprocess
from pathlib import Path

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[1]

DEFAULT_OUT_FILE = "work/native-direct-open/r1_audiobook_direct_open"

# Zig compiler search order: known Linux path, PATH.
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

    # Source file.
    source = REPO_ROOT / "tools" / "r1_audiobook_direct_open.c"

    # Build the zig cc command.
    cmd: list[str | Path] = [
        zig,
        "cc",
        "-target", "mipsel-linux-musleabi",
        "-static",
        "-Os",
        "-s",
        source,
        "-o", out_file,
    ]

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