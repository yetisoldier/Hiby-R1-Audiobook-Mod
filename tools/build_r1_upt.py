#!/usr/bin/env python3
"""
Build a HiBy R1 .upt package from xImage and rootfs.squashfs images.

The R1 OTA format uses an ISO image with chained MD5 chunk names. This script
recreates that structure and writes Rock Ridge/Joliet names so the device sees
the expected lowercase, multi-dot filenames.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".deps" / "python"))

import pycdlib  # type: ignore  # noqa: E402


CHUNK_SIZE = 512 * 1024


def md5_file(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def split_image(image: Path, out_dir: Path, prefix: str) -> tuple[int, str]:
    overall_md5 = md5_file(image)
    image_size = image.stat().st_size
    manifest_path = out_dir / f"ota_md5_{prefix}.{overall_md5}"
    previous_md5 = overall_md5

    with image.open("rb") as src, manifest_path.open("w", newline="\n") as manifest:
        index = 0
        while True:
            chunk = src.read(CHUNK_SIZE)
            if not chunk:
                break
            chunk_md5 = hashlib.md5(chunk).hexdigest()
            manifest.write(chunk_md5 + "\n")
            chunk_name = f"{prefix}.{index:04d}.{previous_md5}"
            (out_dir / chunk_name).write_bytes(chunk)
            previous_md5 = chunk_md5
            index += 1

    return image_size, overall_md5


def prepare_ota_tree(ximage: Path, rootfs: Path, work_dir: Path) -> Path:
    iso_root = work_dir / "iso_root"
    ota_dir = iso_root / "ota_v0"
    ota_dir.mkdir(parents=True)

    x_size, x_md5 = split_image(ximage, ota_dir, "xImage")
    r_size, r_md5 = split_image(rootfs, ota_dir, "rootfs.squashfs")

    (ota_dir / "ota_update.in").write_text(
        "\n".join(
            [
                "ota_version=0",
                "",
                "img_type=kernel",
                "img_name=xImage",
                f"img_size={x_size}",
                f"img_md5={x_md5}",
                "",
                "img_type=rootfs",
                "img_name=rootfs.squashfs",
                f"img_size={r_size}",
                f"img_md5={r_md5}",
                "",
            ]
        ),
        encoding="ascii",
        newline="\n",
    )
    (ota_dir / "ota_v0.ok").write_text("\n", encoding="ascii", newline="\n")
    (iso_root / "ota_config.in").write_text("current_version=0\n", encoding="ascii", newline="\n")
    return iso_root


def iso_identifier(counter: int, suffix: str = "BIN") -> str:
    return f"/F{counter:07d}.{suffix};1"


def add_tree_to_iso(iso: pycdlib.PyCdlib, root: Path) -> None:
    counter = 0

    def add_dir(path: Path, iso_dir: str, joliet_dir: str) -> None:
        nonlocal counter
        for child in sorted(path.iterdir(), key=lambda p: (not p.is_dir(), p.name.lower())):
            rr_name = child.name
            if child.is_dir():
                counter += 1
                child_iso_dir = f"{iso_dir}/D{counter:07d}" if iso_dir else f"/D{counter:07d}"
                child_joliet_dir = f"{joliet_dir}/{child.name}" if joliet_dir else f"/{child.name}"
                iso.add_directory(child_iso_dir, rr_name=rr_name, joliet_path=child_joliet_dir)
                add_dir(child, child_iso_dir, child_joliet_dir)
            else:
                counter += 1
                suffix = "TXT" if child.suffix.lower() in {".in", ".ok"} else "BIN"
                child_iso_path = (iso_dir if iso_dir else "") + iso_identifier(counter, suffix)
                child_joliet_path = f"{joliet_dir}/{child.name}" if joliet_dir else f"/{child.name}"
                iso.add_file(str(child), iso_path=child_iso_path, rr_name=rr_name, joliet_path=child_joliet_path)

    add_dir(root, "", "")


def write_iso(iso_root: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, joliet=3, rock_ridge="1.09", vol_ident="CDROM")
    try:
        add_tree_to_iso(iso, iso_root)
        iso.write(str(output))
    finally:
        iso.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ximage", type=Path, required=True)
    parser.add_argument("--rootfs", type=Path, required=True)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument(
        "--keep-tree",
        type=Path,
        help="Optional directory where the generated ota tree should be copied for inspection.",
    )
    args = parser.parse_args()

    if not args.ximage.exists():
        raise SystemExit(f"xImage not found: {args.ximage}")
    if not args.rootfs.exists():
        raise SystemExit(f"rootfs not found: {args.rootfs}")

    with tempfile.TemporaryDirectory(prefix="r1-upt-") as tmp:
        tmp_path = Path(tmp)
        iso_root = prepare_ota_tree(args.ximage, args.rootfs, tmp_path)
        if args.keep_tree:
            if args.keep_tree.exists():
                shutil.rmtree(args.keep_tree)
            shutil.copytree(iso_root, args.keep_tree)
        write_iso(iso_root, args.output)

    print(f"wrote: {args.output}")
    print(f"size: {args.output.stat().st_size}")
    print(f"md5:  {md5_file(args.output)}")


if __name__ == "__main__":
    main()
