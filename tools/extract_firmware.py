#!/usr/bin/env python3
"""Extract a stock HiBy R1 firmware .upt package into its component parts.

Converted from tools/extract_r1_firmware.ps1.

The .upt file is an ISO 9660 image (with Rock Ridge / Joliet extensions)
containing an ``ota_vN/`` directory.  Inside that directory the rootfs and
kernel image are split into 512 KiB chunks whose filenames embed an MD5
chain.  This script:

  1. Opens the .upt ISO with *pycdlib* and extracts the ota_vN tree.
  2. Reassembles ``rootfs.squashfs`` and ``xImage`` by sorting and
     concatenating their chunk files.
  3. Verifies the MD5 chain and overall file hashes against the
     ``ota_update.in`` manifest.
  4. Reports the extracted file sizes.
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import re
import shutil
import sys
from pathlib import Path

log = logging.getLogger(__name__)

CHUNK_SIZE = 512 * 1024

# ---------------------------------------------------------------------------
# ISO extraction
# ---------------------------------------------------------------------------

def extract_iso(iso_path: Path, out_dir: Path) -> None:
    """Extract all files from *iso_path* into *out_dir* using pycdlib."""
    import pycdlib  # type: ignore  # noqa: E402

    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    try:
        # Walk the ISO using Joliet paths first (preserves real lowercase
        # multi-dot filenames like ``rootfs.squashfs.0000.<md5>``).  Fall
        # back to Rock Ridge if Joliet is not available.  ISO 9660 paths
        # mangle names into uppercase 8.3 format with ``;1`` version
        # suffixes, which breaks chunk reassembly downstream.
        try:
            entries = list(iso.walk(joliet_path="/"))
            path_kw = "joliet_path"
        except Exception:
            entries = list(iso.walk(rr_path="/"))
            path_kw = "rr_path"

        extracted: list[tuple[str, str]] = []
        for root, dirs, files in entries:
            for name in files:
                full = f"{root.rstrip('/')}/{name}"
                extracted.append((full, name))

        for remote_path, _name in extracted:
            rel = remote_path.lstrip("/")
            dest = out_dir / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            iso.get_file_from_iso(str(dest), **{path_kw: remote_path})
    finally:
        iso.close()


# ---------------------------------------------------------------------------
# Chunk reassembly
# ---------------------------------------------------------------------------


def join_chunks(input_dir: Path, pattern: str, output_path: Path) -> str:
    """Concatenate chunk files matching *pattern* in *input_dir* into *output_path*.

    Returns the overall MD5 of the reassembled file.
    """
    output_path.unlink(missing_ok=True)
    rx = re.compile(pattern)

    chunks = sorted(
        (f for f in input_dir.iterdir() if f.is_file() and rx.match(f.name)),
        key=lambda f: f.name,
    )
    if not chunks:
        raise FileNotFoundError(f"No chunks matching {pattern!r} in {input_dir}")

    overall = hashlib.md5()
    with output_path.open("wb") as out:
        for chunk in chunks:
            data = chunk.read_bytes()
            out.write(data)
            overall.update(data)
    return overall.hexdigest()


# ---------------------------------------------------------------------------
# Manifest verification
# ---------------------------------------------------------------------------

def parse_ota_update(ota_dir: Path) -> dict[str, dict[str, str]]:
    """Parse ``ota_update.in`` and return a dict keyed by img_type."""
    manifest_path = ota_dir / "ota_update.in"
    if not manifest_path.exists():
        log.warning("ota_update.in not found in %s; skipping hash verification", ota_dir)
        return {}

    result: dict[str, dict[str, str]] = {}
    current: dict[str, str] = {}
    for line in manifest_path.read_text(encoding="ascii").splitlines():
        line = line.strip()
        if not line:
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if key == "img_type":
                if current:
                    result[current.get("img_type", "")] = current
                current = {"img_type": value}
            else:
                current[key] = value
    if current:
        result[current.get("img_type", "")] = current
    return result


def verify_chunk_chain(ota_dir: Path, prefix: str, expected_overall_md5: str) -> None:
    """Verify that the chunk MD5 chain matches the manifest.

    Chunk filenames are ``<prefix>.<NNNN>.<previous_md5>`` where the first
    chunk's suffix is the overall file MD5 and each subsequent chunk's
    suffix is the previous chunk's MD5.
    """
    rx = re.compile(rf"^{re.escape(prefix)}\.(\d{{4}})\.([0-9a-fA-F]{{32}})$")
    chunks = sorted(
        (f for f in ota_dir.iterdir() if f.is_file() and rx.match(f.name)),
        key=lambda f: f.name,
    )
    if not chunks:
        raise FileNotFoundError(f"No chunks for prefix {prefix!r} in {ota_dir}")

    expected_prev = expected_overall_md5.lower()
    for chunk in chunks:
        m = rx.match(chunk.name)
        assert m is not None
        index = int(m.group(1))
        chain_md5 = m.group(2).lower()
        if chain_md5 != expected_prev:
            raise ValueError(
                f"Chunk chain mismatch at {prefix}.{index:04d}: "
                f"expected prev MD5 {expected_prev}, got {chain_md5}"
            )
        data = chunk.read_bytes()
        expected_prev = hashlib.md5(data).hexdigest()

    log.info("OK   %s chunk chain verified (%d chunks)", prefix, len(chunks))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def find_ota_dir(extract_dir: Path) -> Path:
    """Find the ``ota_vN`` directory inside the extracted ISO tree."""
    candidates = sorted(extract_dir.glob("ota_v*"))
    for c in candidates:
        if c.is_dir():
            return c
    # Fall back to a direct child search
    for child in extract_dir.iterdir():
        if child.is_dir() and child.name.startswith("ota_v"):
            return child
    raise FileNotFoundError("Extracted package does not contain an ota_vN directory")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Extract a stock HiBy R1 .upt firmware package into rootfs and kernel images.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--upt-path",
        type=Path,
        default=Path("stock/r1.upt"),
        help="Path to the .upt firmware package (default: stock/r1.upt)",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("work/original"),
        help="Output directory for extracted components (default: work/original)",
    )
    return parser


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    parser = build_parser()
    args = parser.parse_args()

    upt = args.upt_path.resolve()
    out_dir = args.out_dir.resolve()

    if not upt.exists():
        log.error("Firmware package not found: %s", upt)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    extract_dir = out_dir / "upt"
    if extract_dir.exists():
        shutil.rmtree(extract_dir)
    extract_dir.mkdir(parents=True)

    # --- Extract ISO -------------------------------------------------------
    log.info("== Extracting %s ==", upt.name)
    try:
        extract_iso(upt, extract_dir)
    except Exception as exc:
        log.error("ISO extraction failed: %s", exc)
        return 1

    # --- Locate ota_vN directory ------------------------------------------
    ota_dir = find_ota_dir(extract_dir)
    log.info("Found OTA directory: %s", ota_dir.name)

    # --- Reassemble chunked images ----------------------------------------
    rootfs_path = out_dir / "rootfs.squashfs"
    ximage_path = out_dir / "xImage"

    log.info("== Reassembling rootfs.squashfs ==")
    rootfs_md5 = join_chunks(ota_dir, r"^rootfs\.squashfs\.\d{4}\.", rootfs_path)

    log.info("== Reassembling xImage ==")
    ximage_md5 = join_chunks(ota_dir, r"^xImage\.\d{4}\.", ximage_path)

    # --- Verify against manifest ------------------------------------------
    manifest = parse_ota_update(ota_dir)
    if manifest:
        rootfs_entry = manifest.get("rootfs", {})
        ximage_entry = manifest.get("kernel", {})

        if rootfs_entry:
            expected = rootfs_entry.get("img_md5", "").lower()
            if expected and rootfs_md5 != expected:
                log.error(
                    "rootfs.squashfs MD5 mismatch: expected %s, got %s",
                    expected, rootfs_md5,
                )
                return 1
            verify_chunk_chain(ota_dir, "rootfs.squashfs", expected)
            log.info("OK   rootfs.squashfs hash verified (%s)", rootfs_md5)

        if ximage_entry:
            expected = ximage_entry.get("img_md5", "").lower()
            if expected and ximage_md5 != expected:
                log.error(
                    "xImage MD5 mismatch: expected %s, got %s",
                    expected, ximage_md5,
                )
                return 1
            verify_chunk_chain(ota_dir, "xImage", expected)
            log.info("OK   xImage hash verified (%s)", ximage_md5)

    # --- Report ------------------------------------------------------------
    log.info("")
    log.info("Extracted components:")
    for name, path in [("rootfs.squashfs", rootfs_path), ("xImage", ximage_path)]:
        size = path.stat().st_size
        log.info("  %-20s  %12d bytes  %s", name, size, path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())