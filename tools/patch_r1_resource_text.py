#!/usr/bin/env python3
"""
Patch stock HiBy R1 resource strings for the audiobook-focused Books section.

The resource INI files are UTF-16 XML-like files. This script keeps the
original encoding and only replaces a few display strings.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REPLACEMENTS = {
    "about_dev.ini": {
        "model": "HiBy R1 Audiobook FW 1.6.4",
    },
    "book.ini": {
        "ebook": "Audiobooks",
        "recent": "Recently played",
        "explorer": "Library",
        "no_file": "No audiobooks were found in the /Audiobooks directory",
    },
    "songlist.ini": {
        "ebook": "Audiobooks",
    },
    "launcher.ini": {
        "book": "Audiobooks",
    },
}


def replace_tag(text: str, tag: str, value: str) -> tuple[str, bool]:
    start = f"<{tag}>"
    end = f"</{tag}>"
    pos = text.find(start)
    if pos == -1:
        return text, False
    value_start = pos + len(start)
    value_end = text.find(end, value_start)
    if value_end == -1:
        return text, False
    return text[:value_start] + value + text[value_end:], True


def patch_file(path: Path, replacements: dict[str, str]) -> list[str]:
    raw = path.read_bytes()
    encoding = "utf-16" if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff") else "utf-8"
    text = raw.decode(encoding)

    changed: list[str] = []
    for tag, value in replacements.items():
        text, did_change = replace_tag(text, tag, value)
        if did_change:
            changed.append(tag)

    if changed:
        path.write_bytes(text.encode(encoding))
    return changed


def patch_product_version(rootfs: Path, version: str) -> bool:
    path = rootfs / "usr" / "resource" / "config.json"
    if not path.exists():
        return False

    data = json.loads(path.read_text(encoding="utf-8"))
    changed = False
    for entry in data:
        if isinstance(entry, dict) and entry.get("type") == "product":
            if entry.get("version") != version:
                entry["version"] = version
                changed = True
    if changed:
        path.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    return changed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "rootfs",
        type=Path,
        help="Root of an extracted R1 filesystem, containing usr/resource/str.",
    )
    parser.add_argument(
        "--language",
        action="append",
        dest="languages",
        default=["english"],
        help="Language directory to patch. Repeat for multiple languages.",
    )
    parser.add_argument(
        "--about-model",
        default="HiBy R1 Audiobook FW 1.6.4",
        help="Visible About-screen model/version label.",
    )
    parser.add_argument(
        "--product-version",
        default="1.6.4-audiobook",
        help="Product version string shown by the stock About screen.",
    )
    args = parser.parse_args()

    replacements_by_file = {
        filename: replacements.copy() for filename, replacements in REPLACEMENTS.items()
    }
    replacements_by_file.setdefault("about_dev.ini", {})["model"] = args.about_model

    str_root = args.rootfs / "usr" / "resource" / "str"
    if not str_root.is_dir():
        raise SystemExit(f"Could not find resource string directory: {str_root}")

    for language in args.languages:
        lang_dir = str_root / language
        if not lang_dir.is_dir():
            raise SystemExit(f"Language directory not found: {lang_dir}")
        for filename, replacements in replacements_by_file.items():
            path = lang_dir / filename
            if not path.exists():
                continue
            changed = patch_file(path, replacements)
            if changed:
                print(f"{path}: {', '.join(changed)}")

    if patch_product_version(args.rootfs, args.product_version):
        print(f"{args.rootfs / 'usr' / 'resource' / 'config.json'}: product.version")


if __name__ == "__main__":
    main()
