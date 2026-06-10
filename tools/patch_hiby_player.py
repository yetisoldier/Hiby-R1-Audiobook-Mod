#!/usr/bin/env python3
"""
Apply guarded patches to the stock HiBy R1 1.6 hiby_player binary.

This only edits a local copy of the binary. It refuses to run unless the input
matches the known stock 1.6 MD5, which keeps accidental cross-version patching
out of the danger zone.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


STOCK_MD5 = "ad69fa8377fb85b01ed5d65fe976b19a"
STOCK_SHA256 = "8398e1e1295e83b033bf7b8c39932fff3f620831f5a91682869554047b26f6b2"

SCAN_SKIP_OFFSET = 0x35BE70
SCAN_SKIP_ORIGINAL = "System Volume Information".encode("utf-16le") + b"\x00\x00"
SCAN_SKIP_PATCHED = "Audiobooks".encode("utf-16le")
SCAN_SKIP_PATCHED += b"\x00" * (len(SCAN_SKIP_ORIGINAL) - len(SCAN_SKIP_PATCHED))

BOOK_AUDIO_SHIM_PATCHES = (
    # Redirect the book-row open path from the text-book open routine to a
    # code cave that builds an audio play request.
    (0x140B0C, bytes.fromhex("7000150c"), bytes.fromhex("bb761d0c")),
    # Suppress the unsupported-text dialog path after the redirected branch.
    (0x140B40, bytes.fromhex("68fc110c"), bytes.fromhex("00000000")),
    (
        0x35DAEC,
        b"\x00" * 0xA8,
        bytes.fromhex(
            "20f5bd27dc0abfafd80ab0afd40ab1af2000a42725280000"
            "880a0624c0ea200c000000002400a4272528200208020624"
            "b843100c00000000a80aa4272528000020000624c0ea200c"
            "000000006c3b088e0600001100000000d001098dbc0aa9af"
            "25302001020000100000000025300000252000022000a527"
            "a80aa7278078120c0000000025200002387f120c00000000"
            "dc0abf8fd80ab08fd40ab18f010002240800e003e00abd27"
        ),
    ),
)

SELECT_DISPATCH_PATCH = (
    0x3EF24,
    bytes.fromhex("0c004310"),
    bytes.fromhex("0c000010"),
)

AUDIOBOOK_LAUNCHER_CAVE_OFFSET = 0x35DAEC
AUDIOBOOK_LAUNCHER_CALLBACK_OFFSET = 0x482030
AUDIOBOOK_TITLE_SOURCE_MAGIC = 0xA0B00515
AUDIOBOOK_LAUNCHER_CODE = bytes.fromhex(
    "a0fbbd275c04bfaf5804b1af5404b0af5000b08c1800001200000000"
    "2800048ee0381c0c000000001300401000000000252000027600113c"
    "a8db31267800053ca070a5247600063c80dbc6248e00083c00400835"
    "b0a0093c15052935200009ad28000a8d01004a252c000aad1000b1af"
    "70c0130c2000a727010002245c04bf8f5804b18f5404b08f0800e003"
    "6004bd27"
)
AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE = 0x94
_AUDIOBOOK_LAUNCHER_ROUTE_FIELD_SIZE = len(
    "Audiobook\\Audiobook".encode("utf-16le") + b"\x00\x00"
)
AUDIOBOOK_LAUNCHER_ROUTE = (
    "genre\\Audiobook".encode("utf-16le") + b"\x00\x00"
).ljust(_AUDIOBOOK_LAUNCHER_ROUTE_FIELD_SIZE, b"\x00")
AUDIOBOOK_LAUNCHER_SELECTED_GENRE = (
    "Audiobook".encode("utf-16le") + b"\x00\x00"
).ljust(24, b"\x00")
AUDIOBOOK_BOOK_OPEN_ROOT_OFFSET = 0x35DBC0
AUDIOBOOK_BOOK_OPEN_ROOT_CODE = bytes.fromhex(
    "a0fbbd275c04bfaf5804b1af5404b0af258080001300001200000000"
    "252000027600113ca8db31267800053ca070a5247600063c80dbc624"
    "8e00083c00400835b0a0093c15052935200009ad28000a8d01004a25"
    "2c000aad1000b1af70c0130c2000a727010002245c04bf8f5804b18f"
    "5404b08f0800e0036004bd27"
)
AUDIOBOOK_BOOK_OPEN_HOOK = (
    0x140F20,
    bytes.fromhex("7800063c7800053c"),
    bytes.fromhex("f0761d0800000000"),
)
AUDIOBOOK_LAUNCHER_CAVE_PATCHED = (
    AUDIOBOOK_LAUNCHER_CODE
    + b"\x00" * (AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE - len(AUDIOBOOK_LAUNCHER_CODE))
    + AUDIOBOOK_LAUNCHER_ROUTE
    + AUDIOBOOK_LAUNCHER_SELECTED_GENRE
)
AUDIOBOOK_LAUNCHER_PATCHES = (
    (
        AUDIOBOOK_LAUNCHER_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_LAUNCHER_CAVE_PATCHED),
        AUDIOBOOK_LAUNCHER_CAVE_PATCHED,
    ),
    (
        AUDIOBOOK_BOOK_OPEN_ROOT_OFFSET,
        b"\x00" * len(AUDIOBOOK_BOOK_OPEN_ROOT_CODE),
        AUDIOBOOK_BOOK_OPEN_ROOT_CODE,
    ),
    AUDIOBOOK_BOOK_OPEN_HOOK,
    (
        AUDIOBOOK_LAUNCHER_CALLBACK_OFFSET,
        bytes.fromhex("20bb5300"),
        bytes.fromhex("ecda7500"),
    ),
)

AUDIOBOOK_TITLE_MARKER_CAVE_OFFSET = 0x35DE00
AUDIOBOOK_TITLE_MARKER_HOOK = (
    0x09FE40,
    bytes.fromhex("c8fdbd272c02b2af"),
    bytes.fromhex("80771d0800000000"),
)
AUDIOBOOK_TITLE_MARKER_CODE = bytes.fromhex(
    "8e00083c00400835dec0093c174a2935000009ad04001fad"
    "080004ad0c0005ad100006ad140007ad180005ad28000a8d"
    "01004a2528000aadc8fdbd272c02b2af927f120800000000"
)
AUDIOBOOK_TITLE_MARKER_PATCHES = (
    (
        AUDIOBOOK_TITLE_MARKER_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_TITLE_MARKER_CODE),
        AUDIOBOOK_TITLE_MARKER_CODE,
    ),
    AUDIOBOOK_TITLE_MARKER_HOOK,
)


def digest(path: Path, algorithm: str) -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def patch_bytes(data: bytearray, offset: int, expected: bytes, replacement: bytes) -> None:
    found = bytes(data[offset : offset + len(expected)])
    if found != expected:
        raise SystemExit(
            f"Refusing to patch offset 0x{offset:x}: expected {expected.hex()}, found {found.hex()}"
        )
    data[offset : offset + len(replacement)] = replacement


def apply_patches(
    input_path: Path,
    output_path: Path,
    *,
    scan_skip: bool,
    book_audio_shim: bool,
    audiobook_launcher_genre: bool,
    audiobook_title_autostart_marker: bool,
    select_dispatch: bool,
) -> None:
    md5 = digest(input_path, "md5")
    sha256 = digest(input_path, "sha256")
    if md5 != STOCK_MD5 or sha256 != STOCK_SHA256:
        raise SystemExit(
            "Input does not match stock HiBy R1 firmware 1.6 hiby_player.\n"
            f"  expected md5    {STOCK_MD5}\n"
            f"  actual md5      {md5}\n"
            f"  expected sha256 {STOCK_SHA256}\n"
            f"  actual sha256   {sha256}"
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(input_path, output_path)
    data = bytearray(output_path.read_bytes())

    applied: list[str] = []
    if book_audio_shim and audiobook_launcher_genre:
        raise SystemExit(
            "The Books audio shim and Audiobooks launcher patch both use the same code cave. "
            "Choose only one."
        )

    if scan_skip:
        patch_bytes(data, SCAN_SKIP_OFFSET, SCAN_SKIP_ORIGINAL, SCAN_SKIP_PATCHED)
        applied.append("scan-skip-audiobooks")

    if book_audio_shim:
        for offset, expected, replacement in BOOK_AUDIO_SHIM_PATCHES:
            patch_bytes(data, offset, expected, replacement)
        applied.append("book-audio-shim")

    if audiobook_launcher_genre:
        for offset, expected, replacement in AUDIOBOOK_LAUNCHER_PATCHES:
            patch_bytes(data, offset, expected, replacement)
        applied.append("audiobook-launcher-genre")

    if audiobook_title_autostart_marker:
        for offset, expected, replacement in AUDIOBOOK_TITLE_MARKER_PATCHES:
            patch_bytes(data, offset, expected, replacement)
        applied.append("audiobook-title-autostart-marker")

    if select_dispatch:
        patch_bytes(data, *SELECT_DISPATCH_PATCH)
        applied.append("select-dispatch-branch")

    output_path.write_bytes(data)
    print(f"input:  {input_path}")
    print(f"output: {output_path}")
    print("applied:")
    for name in applied:
        print(f"  {name}")
    print(f"md5:    {digest(output_path, 'md5')}")
    print(f"sha256: {digest(output_path, 'sha256')}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="Stock hiby_player input path")
    parser.add_argument("-o", "--output", type=Path, default=Path("work/hiby_player.audiobooks"))
    scanner_group = parser.add_mutually_exclusive_group()
    scanner_group.add_argument(
        "--scan-skip",
        action="store_true",
        help="Replace the scanner skip folder with Audiobooks. Experimental.",
    )
    scanner_group.add_argument(
        "--no-scan-skip",
        action="store_true",
        help="Compatibility no-op. Scanner skip is off by default.",
    )
    shim_group = parser.add_mutually_exclusive_group()
    shim_group.add_argument(
        "--book-audio-shim",
        action="store_true",
        help="Apply the experimental Books-row audio playback shim.",
    )
    shim_group.add_argument(
        "--no-book-audio-shim",
        action="store_true",
        help="Compatibility no-op. Books-row audio shim is off by default.",
    )
    parser.add_argument(
        "--select-dispatch-branch",
        action="store_true",
        help="Also apply the old one-byte dispatch branch patch. Kept off by default.",
    )
    parser.add_argument(
        "--audiobook-launcher-genre",
        action="store_true",
        help=(
            "Repurpose the Books launcher callback to open the stock Genre -> Album media "
            "route for the Audiobook genre. Experimental and off by default."
        ),
    )
    parser.add_argument(
        "--audiobook-title-autostart-marker",
        action="store_true",
        help=(
            "Record Genre -> Album list opens in a small memory marker so the resume "
            "daemon can auto-start audiobook title taps. Experimental and off by default."
        ),
    )
    args = parser.parse_args()

    apply_patches(
        input_path=args.input,
        output_path=args.output,
        scan_skip=args.scan_skip,
        book_audio_shim=args.book_audio_shim,
        audiobook_launcher_genre=args.audiobook_launcher_genre,
        audiobook_title_autostart_marker=args.audiobook_title_autostart_marker,
        select_dispatch=args.select_dispatch_branch,
    )


if __name__ == "__main__":
    main()
