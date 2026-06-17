#!/usr/bin/env python3
"""Prepare a publishable HiBy R1 OTA site directory.

The stock R1 network updater does not download a single .upt file. It fetches
ota_config.in, then ota_vN/ota_update.in, ota_vN/ota_vN.ok, and the chunked
kernel/rootfs files listed by ota_update.in. This tool turns a generated
build_r1_upt.py --keep-tree directory into that server-side layout.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import shutil
from pathlib import Path


OTA_DIR_RE = re.compile(r"^ota_v(\d+)$")


def digest(path: Path, algorithm: str) -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def source_ota_dir(source_tree: Path, ota_version: int) -> Path:
    exact = source_tree / f"ota_v{ota_version}"
    if exact.is_dir():
        return exact

    candidates = [path for path in source_tree.iterdir() if path.is_dir() and OTA_DIR_RE.match(path.name)]
    if len(candidates) == 1:
        return candidates[0]

    found = ", ".join(path.name for path in candidates) or "none"
    raise SystemExit(f"Could not choose source ota_v directory. Expected ota_v{ota_version}; found {found}")


def rewrite_ota_update(path: Path, ota_version: int) -> None:
    lines = path.read_text(encoding="ascii", errors="replace").splitlines()
    replaced = False
    for index, line in enumerate(lines):
        if line.startswith("ota_version="):
            lines[index] = f"ota_version={ota_version}"
            replaced = True
            break
    if not replaced:
        lines.insert(0, f"ota_version={ota_version}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def prepare_site(
    source_tree: Path,
    out_dir: Path,
    ota_version: int,
    *,
    firmware_version: str,
    release_url: str,
    upt: Path | None,
    include_main_os_mirror: bool,
    force: bool,
) -> None:
    if ota_version < 0:
        raise SystemExit("--ota-version must be non-negative")
    if not source_tree.is_dir():
        raise SystemExit(f"source tree not found: {source_tree}")
    if out_dir.exists():
        if not force:
            raise SystemExit(f"output directory already exists; pass --force to replace it: {out_dir}")
        shutil.rmtree(out_dir)

    src_ota = source_ota_dir(source_tree, ota_version)
    ota_dir_name = f"ota_v{ota_version}"
    dest_ota = out_dir / ota_dir_name
    shutil.copytree(src_ota, dest_ota)

    update_file = dest_ota / "ota_update.in"
    if not update_file.exists():
        raise SystemExit(f"missing ota_update.in in {src_ota}")
    rewrite_ota_update(update_file, ota_version)

    for ok_file in dest_ota.glob("ota_v*.ok"):
        ok_file.unlink()
    (dest_ota / f"{ota_dir_name}.ok").write_text("\n", encoding="ascii", newline="\n")
    (out_dir / "ota_config.in").write_text(
        f"current_version={ota_version}\n", encoding="ascii", newline="\n"
    )

    if include_main_os_mirror:
        main_os_ota = out_dir / "main_os" / ota_dir_name
        shutil.copytree(dest_ota, main_os_ota)
        (out_dir / "main_os" / "ota_config.in").write_text(
            f"current_version={ota_version}\n", encoding="ascii", newline="\n"
        )

    manifest = {
        "generated_at_utc": dt.datetime.now(dt.UTC).replace(microsecond=0).isoformat(),
        "ota_version": ota_version,
        "firmware_version": firmware_version,
        "release_url": release_url,
        "files": {
            "ota_config": "ota_config.in",
            "ota_update": f"{ota_dir_name}/ota_update.in",
            "ota_ok": f"{ota_dir_name}/{ota_dir_name}.ok",
        },
        "update": {
            "kernel_md5": None,
            "rootfs_md5": None,
        },
    }

    values = update_file.read_text(encoding="ascii", errors="replace").splitlines()
    for index, value in enumerate(values):
        if value == "img_type=kernel" and index + 3 < len(values):
            manifest["update"]["kernel_md5"] = values[index + 3].removeprefix("img_md5=")
        if value == "img_type=rootfs" and index + 3 < len(values):
            manifest["update"]["rootfs_md5"] = values[index + 3].removeprefix("img_md5=")

    if upt is not None:
        if not upt.exists():
            raise SystemExit(f"upt package not found: {upt}")
        manifest["upt"] = {
            "path": str(upt),
            "size": upt.stat().st_size,
            "md5": digest(upt, "md5"),
            "sha256": digest(upt, "sha256"),
        }

    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="ascii",
        newline="\n",
    )
    (out_dir / "README.txt").write_text(
        "\n".join(
            [
                "HiBy R1 Audiobook Mod OTA site",
                "",
                f"OTA version: {ota_version}",
                f"Firmware version: {firmware_version or 'unspecified'}",
                "",
                "Serve this directory exactly as a static HTTP/HTTPS tree.",
                "The stock updater expects ota_config.in at the OTA site root and",
                f"{ota_dir_name}/ under that root. Some stock scripts also expect",
                f"main_os/{ota_dir_name}/, so this tool mirrors that layout by default.",
                "",
            ]
        ),
        encoding="ascii",
        newline="\n",
    )

    print(f"wrote OTA site: {out_dir}")
    print(f"ota version: {ota_version}")
    print(f"ota update:  {dest_ota / 'ota_update.in'}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-tree", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--ota-version", type=int, required=True)
    parser.add_argument("--firmware-version", default="")
    parser.add_argument("--release-url", default="")
    parser.add_argument("--upt", type=Path, default=None)
    parser.add_argument("--no-main-os-mirror", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    prepare_site(
        args.source_tree,
        args.out_dir,
        args.ota_version,
        firmware_version=args.firmware_version,
        release_url=args.release_url,
        upt=args.upt,
        include_main_os_mirror=not args.no_main_os_mirror,
        force=args.force,
    )


if __name__ == "__main__":
    main()
