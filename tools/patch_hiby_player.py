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


def pack_u32(value: int) -> bytes:
    return (value & 0xFFFFFFFF).to_bytes(4, "little")


def pack_words(*words: int) -> bytes:
    return b"".join(pack_u32(word) for word in words)


def ins_j(addr: int) -> int:
    return (2 << 26) | ((addr >> 2) & 0x03FFFFFF)


def ins_jal(addr: int) -> int:
    return (3 << 26) | ((addr >> 2) & 0x03FFFFFF)


def ins_jalr(rs: int) -> int:
    return ((rs & 0x1F) << 21) | (31 << 11) | 9


def ins_lui(rt: int, imm: int) -> int:
    return (15 << 26) | ((rt & 0x1F) << 16) | (imm & 0xFFFF)


def ins_addiu(rt: int, rs: int, imm: int) -> int:
    return (9 << 26) | ((rs & 0x1F) << 21) | ((rt & 0x1F) << 16) | (imm & 0xFFFF)


def ins_lw(rt: int, base: int, off: int) -> int:
    return (35 << 26) | ((base & 0x1F) << 21) | ((rt & 0x1F) << 16) | (off & 0xFFFF)


def ins_sw(rt: int, base: int, off: int) -> int:
    return (43 << 26) | ((base & 0x1F) << 21) | ((rt & 0x1F) << 16) | (off & 0xFFFF)


def ins_beq(rs: int, rt: int, imm: int) -> int:
    return (4 << 26) | ((rs & 0x1F) << 21) | ((rt & 0x1F) << 16) | (imm & 0xFFFF)


def ins_jr(rs: int) -> int:
    return (rs & 0x1F) << 21 | 8


def load_addr_words(reg: int, addr: int) -> tuple[int, int]:
    hi = (addr + 0x8000) >> 16
    lo = addr & 0xFFFF
    return ins_lui(reg, hi), ins_addiu(reg, reg, lo)


TEXT_LOAD_BASE = 0x00400000


def text_addr(offset: int) -> int:
    return TEXT_LOAD_BASE + offset


STOCK_MD5 = "cd4d2812ab3425174b52925766424d2b"
STOCK_SHA256 = "a977d74043d997c6eb34720bd3e0e8c17f88caee6e0ec520cb05807b7a987bd4"

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
AUDIOBOOK_LAUNCHER_ROUTE_OFFSET = (
    AUDIOBOOK_LAUNCHER_CAVE_OFFSET + AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE
)
AUDIOBOOK_LAUNCHER_ROUTE = (
    "genre\\Audiobook".encode("utf-16le") + b"\x00\x00"
).ljust(_AUDIOBOOK_LAUNCHER_ROUTE_FIELD_SIZE, b"\x00")
AUDIOBOOK_LAUNCHER_SELECTED_GENRE_OFFSET = (
    AUDIOBOOK_LAUNCHER_ROUTE_OFFSET + len(AUDIOBOOK_LAUNCHER_ROUTE)
)
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

AUDIOBOOK_PRIVATE_DIRECT_ROUTE_RECORD_OFFSET = 0x35DF40
AUDIOBOOK_PRIVATE_DIRECT_ROUTE_RECORD_ADDR = 0x0075DF40
AUDIOBOOK_PRIVATE_DIRECT_ROUTE_RECORD = pack_words(
    # Clone the stock genre-chain route record header, but start directly at
    # Albums-of-Genre for the Audiobooks launcher only. This avoids modifying
    # the global Music genre route table at 0x007870a0.
    0x0075CAC8,
    0x00786E98,
    0x0077F284,
    0x0077F2A4,
    0x00000000,
    0x004F01C0,
)
AUDIOBOOK_PRIVATE_DIRECT_ROUTE_PATCHES = (
    (
        AUDIOBOOK_LAUNCHER_CAVE_OFFSET + 0x3C,
        pack_words(ins_lui(5, 0x78), ins_addiu(5, 5, 0x70A0)),
        pack_words(ins_lui(5, 0x76), ins_addiu(5, 5, -0x20C0)),
    ),
    (
        AUDIOBOOK_PRIVATE_DIRECT_ROUTE_RECORD_OFFSET,
        b"\x00" * len(AUDIOBOOK_PRIVATE_DIRECT_ROUTE_RECORD),
        AUDIOBOOK_PRIVATE_DIRECT_ROUTE_RECORD,
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

# ── Extended autostart marker (Approach E) ──────────────────────────────
# Second marker hook that fires when a .m3u file is opened from the
# filesystem explorer (vg_listview_explorer -> m3u callback at 0x4efe00).
# This enables the daemon to detect title taps from the native hub's
# Titles/Authors/Series views, which use .m3u playlists.
# Uses the same marker address (0x8E4000) so the daemon's existing
# marker polling detects the change.
AUDIOBOOK_EXPLORER_MARKER_CAVE_OFFSET = 0x360E38
AUDIOBOOK_EXPLORER_MARKER_HOOK = (
    0x00EFE00,
    bytes.fromhex("c0ffbd273800b7af"),
    bytes.fromhex("8e831d0800000000"),
)
AUDIOBOOK_EXPLORER_MARKER_CODE = bytes.fromhex(
    # addiu sp, sp, -32
    # sw ra, 28(sp)
    # sw s0, 24(sp)
    # lui s0, 0x008E
    # addiu s0, s0, 0x4000   → s0 = 0x8E4000 (marker address)
    # lw t0, 0(s0)           ← FIXED: was lw t0, 0(t0)
    # addiu t0, t0, 1
    # sw t0, 0(s0)           ← FIXED: was sw t0, 0(t0)
    # lw s0, 24(sp)
    # lw ra, 28(sp)
    # addiu sp, sp, 32
    # addiu sp, sp, -64  (original instruction 1)
    # sw s7, 56(sp)       (original instruction 2)
    # j 0x4efe08
    # nop
    "e0ffbd271c00bfaf1800b0af8e00103c00401026"
    "0000088e01000825000008ae1800b08f1c00bf8f"
    "2000bd27c0ffbd273800b7af82bf130800000000"
    "0000"
)
AUDIOBOOK_EXPLORER_MARKER_PATCHES = (
    (
        AUDIOBOOK_EXPLORER_MARKER_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_EXPLORER_MARKER_CODE),
        AUDIOBOOK_EXPLORER_MARKER_CODE,
    ),
    AUDIOBOOK_EXPLORER_MARKER_HOOK,
)

AUDIOBOOK_DIRECT_OPEN_CAVE_OFFSET = 0x360F00
AUDIOBOOK_DIRECT_OPEN_CAVE_ADDR = text_addr(AUDIOBOOK_DIRECT_OPEN_CAVE_OFFSET)
AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR = 0x008E4400
AUDIOBOOK_DIRECT_OPEN_NEEDLE = b"Audiobooks\x00"


def audiobook_direct_open_cave() -> bytes:
    """
    Redirect audiobook title taps straight into shared_media_open.

    The wrapper still falls back to the stock path for everything that is not
    under /Audiobooks, and it uses the shared scratch slot at 0x8E4400 for the
    track index so the daemon can populate it later.
    """

    # The direct-open stub is 57 words long before the needle string.
    needle_addr = AUDIOBOOK_DIRECT_OPEN_CAVE_ADDR + 57 * 4
    needle_hi, needle_lo = load_addr_words(5, needle_addr)
    scratch_hi, scratch_lo = load_addr_words(8, AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR)
    return pack_words(
        ins_addiu(29, 29, -0x38),
        ins_sw(31, 29, 0x34),
        ins_sw(17, 29, 0x30),
        ins_sw(16, 29, 0x2C),
        ins_sw(18, 29, 0x28),
        ins_jal(0x00456BE0),
        ins_lw(4, 4, 0x00F8),
        ins_beq(2, 0, 33),
        ins_addiu(16, 2, 0),
        ins_lw(4, 16, 0x0064),
        ins_lw(2, 16, 0x3B6C),
        ins_lw(25, 4, 0x002C),
        ins_jalr(25),
        ins_lw(5, 2, 0x01D0),
        ins_beq(2, 0, 26),
        ins_addiu(17, 2, 4),
        ins_addiu(4, 17, 0),
        ins_jal(0x0046E040),
        ins_addiu(5, 0, 0),
        ins_addiu(3, 0, -1),
        ins_beq(2, 3, 28),
        ins_lw(4, 18, 0x00D8),
        ins_addiu(18, 4, 0),
        needle_hi,
        needle_lo,
        ins_jal(0x00839F80),
        ins_addiu(4, 17, 0),
        ins_addiu(4, 18, 0),
        ins_beq(2, 0, 10),
        0,
        scratch_hi,
        scratch_lo,
        ins_lw(6, 8, 0),
        ins_addiu(7, 0, 0),
        ins_addiu(5, 17, 0),
        ins_jal(0x0049E200),
        0,
        ins_j(AUDIOBOOK_DIRECT_OPEN_CAVE_ADDR + 41 * 4),
        0,
        ins_j(0x00540AD0),
        0,
        ins_lw(31, 29, 0x34),
        ins_lw(17, 29, 0x30),
        ins_lw(16, 29, 0x2C),
        ins_lw(18, 29, 0x28),
        ins_addiu(2, 0, 1),
        ins_addiu(29, 29, 0x38),
        ins_jr(31),
        0,
        ins_lw(31, 29, 0x34),
        ins_lw(17, 29, 0x30),
        ins_lw(16, 29, 0x2C),
        ins_lw(18, 29, 0x28),
        ins_addiu(2, 0, -1),
        ins_addiu(29, 29, 0x38),
        ins_jr(31),
        0,
    ) + AUDIOBOOK_DIRECT_OPEN_NEEDLE


AUDIOBOOK_DIRECT_OPEN_CODE = audiobook_direct_open_cave()
AUDIOBOOK_DIRECT_OPEN_PATCHES = (
    (
        AUDIOBOOK_DIRECT_OPEN_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_DIRECT_OPEN_CODE),
        AUDIOBOOK_DIRECT_OPEN_CODE,
    ),
    (
        0x540A80,
        bytes.fromhex("27bdffc8afbf0034"),
        pack_words(ins_j(AUDIOBOOK_DIRECT_OPEN_CAVE_ADDR), 0),
    ),
)

AUDIOBOOK_NATIVE_HUB_TITLE_ROW_CAVE_OFFSET = 0x35DE60
AUDIOBOOK_NATIVE_HUB_TITLE_ROW_CODE = bytes.fromhex(
    # move a0, s2
    # jal 0x0075dbc0
    # nop
    # j 0x00540d2c
    # nop
    "25204002"
    "f0761d0c"
    "00000000"
    "4b031508"
    "00000000"
)
AUDIOBOOK_NATIVE_HUB_TITLE_ROW_PATCHES = (
    (
        AUDIOBOOK_BOOK_OPEN_ROOT_OFFSET,
        b"\x00" * len(AUDIOBOOK_BOOK_OPEN_ROOT_CODE),
        AUDIOBOOK_BOOK_OPEN_ROOT_CODE,
    ),
    (
        AUDIOBOOK_LAUNCHER_ROUTE_OFFSET,
        b"\x00" * len(AUDIOBOOK_LAUNCHER_ROUTE),
        AUDIOBOOK_LAUNCHER_ROUTE,
    ),
    (
        AUDIOBOOK_LAUNCHER_SELECTED_GENRE_OFFSET,
        b"\x00" * len(AUDIOBOOK_LAUNCHER_SELECTED_GENRE),
        AUDIOBOOK_LAUNCHER_SELECTED_GENRE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_TITLE_ROW_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_TITLE_ROW_CODE),
        AUDIOBOOK_NATIVE_HUB_TITLE_ROW_CODE,
    ),
    (
        0x38D27C,
        bytes.fromhex("08077600"),
        bytes.fromhex("60de7500"),
    ),
)

AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_CAVE_OFFSET = 0x35DEA0
AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATH_OFFSET = 0x35DF00
AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_CAVE_CODE = pack_words(
    # lw a0, 0xd8(s0)
    ins_lw(4, 16, 0x00D8),
    # a1 = "vg_listview_explorer"
    ins_lui(5, 0x0076),
    ins_addiu(5, 5, -0x3ECC),
    # a2 = L"a:\\Audiobooks\\*"
    ins_lui(6, 0x0076),
    ins_addiu(6, 6, -0x2100),
    # create native explorer subpage, then return through the stock hub epilogue
    ins_jal(0x004961E0),
    0,
    ins_j(0x00540D2C),
    0,
)
AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATH = (
    "a:\\Audiobooks\\*".encode("utf-16le") + b"\x00\x00"
).ljust(0x40, b"\x00")
AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATCHES = (
    (
        AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_CAVE_CODE),
        AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_CAVE_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATH_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATH),
        AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATH,
    ),
    (
        # Stock Favorites row -> audiobook folder explorer.
        0x38D280,
        bytes.fromhex("38077600"),
        pack_u32(0x0075DEA0),
    ),
    (
        # Stock Files row -> audiobook folder explorer.
        0x38D284,
        bytes.fromhex("68077600"),
        pack_u32(0x0075DEA0),
    ),
)

AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET = 0x360708
AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_ADDR = 0x00760708
AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x30
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x60
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x90
AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0xC0
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0xD0
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0xE0
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0xF0
AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x100
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x180
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x200
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH_OFFSET = AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_OFFSET + 0x280
AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE_OFFSET = 0x360A08
AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD_OFFSET = 0x360A80
AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_OFFSET = 0x360D50
AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_SYSTEM_PLT_ADDR = 0x0083AD80

AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH_OFFSET)
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH_ADDR = text_addr(AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH_OFFSET)


def explorer_open_helper() -> bytes:
    view_a1_hi, view_a1_lo = load_addr_words(5, 0x0075C134)  # "vg_listview_explorer"
    reopen_a1_hi, reopen_a1_lo = load_addr_words(5, 0x0075C134)
    sub_back_a1_hi, sub_back_a1_lo = load_addr_words(5, 0x007605C0)  # "hiby_set_sub_back"
    explorer_v0_hi, explorer_v0_lo = load_addr_words(2, 0x0075C134)
    back_cb_a1_hi, back_cb_a1_lo = load_addr_words(5, 0x0053F360)
    return pack_words(
        ins_addiu(29, 29, -0x40),
        ins_sw(31, 29, 0x3C),
        ins_sw(16, 29, 0x38),
        ins_sw(17, 29, 0x34),
        ins_sw(18, 29, 0x30),
        ins_sw(19, 29, 0x2C),
        ins_addiu(16, 4, 0),
        ins_addiu(17, 5, 0),
        ins_addiu(18, 6, 0),
        ins_sw(0, 29, 0x10),
        ins_addiu(4, 16, 0),
        view_a1_hi,
        view_a1_lo,
        ins_addiu(6, 29, 0x10),
        ins_jal(0x004F13A0),
        0,
        ins_lw(2, 29, 0x10),
        ins_beq(2, 0, 6),
        0,
        ins_lw(4, 2, 0x3B90),
        ins_beq(4, 0, 3),
        0,
        ins_jal(0x004C64A0),
        0,
        ins_addiu(4, 16, 0),
        reopen_a1_hi,
        reopen_a1_lo,
        ins_addiu(6, 17, 0),
        ins_jal(0x004961E0),
        0,
        ins_beq(2, 0, 20),
        ins_addiu(19, 2, 0),
        ins_addiu(4, 16, 0),
        sub_back_a1_hi,
        sub_back_a1_lo,
        ins_addiu(6, 18, 0),
        ins_addiu(7, 0, 1),
        explorer_v0_hi,
        explorer_v0_lo,
        ins_sw(2, 29, 0x10),
        ins_jal(0x004C6760),
        0,
        ins_sw(2, 19, 0x3B90),
        ins_lw(2, 19, 0x00E8),
        ins_beq(2, 0, 6),
        0,
        ins_lw(4, 2, 0x00D8),
        back_cb_a1_hi,
        back_cb_a1_lo,
        ins_jal(0x00456DC0),
        0,
        ins_lw(31, 29, 0x3C),
        ins_lw(16, 29, 0x38),
        ins_lw(17, 29, 0x34),
        ins_lw(18, 29, 0x30),
        ins_lw(19, 29, 0x2C),
        ins_jr(31),
        ins_addiu(29, 29, 0x40),
    )


def explorer_row_cave(path_addr: int, label_addr: int) -> bytes:
    a1_hi, a1_lo = load_addr_words(5, path_addr)
    a2_hi, a2_lo = load_addr_words(6, label_addr)
    return pack_words(
        ins_lw(4, 16, 0x00D8),
        a1_hi,
        a1_lo,
        a2_hi,
        a2_lo,
        ins_jal(AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_ADDR),
        0,
        ins_j(0x00540D2C),
        0,
    ).ljust(0x30, b"\x00")


def refresh_row_cave() -> bytes:
    a0_hi, a0_lo = load_addr_words(4, AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD_ADDR)
    title_a1_hi, title_a1_lo = load_addr_words(5, AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH_ADDR)
    title_a2_hi, title_a2_lo = load_addr_words(6, AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL_ADDR)
    return pack_words(
        ins_addiu(29, 29, -0x18),
        ins_sw(31, 29, 0x14),
        a0_hi,
        a0_lo,
        ins_jal(AUDIOBOOK_NATIVE_HUB_VIEW_SYSTEM_PLT_ADDR),
        0,
        ins_lw(4, 16, 0x00D8),
        title_a1_hi,
        title_a1_lo,
        title_a2_hi,
        title_a2_lo,
        ins_jal(AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_ADDR),
        0,
        ins_lw(31, 29, 0x14),
        ins_addiu(29, 29, 0x18),
        ins_j(0x00540D2C),
        0,
    ).ljust(0x70, b"\x00")


def wide_path(path: str, size: int = 0x80) -> bytes:
    encoded = path.encode("utf-16le") + b"\x00\x00"
    if len(encoded) > size:
        raise ValueError(f"wide path is too large for patch field: {path}")
    return encoded.ljust(size, b"\x00")


def wide_label(label: str, size: int = 0x10) -> bytes:
    return wide_path(label, size)


AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE = explorer_row_cave(
    AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH_ADDR,
    AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL_ADDR,
)
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE = explorer_row_cave(
    AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH_ADDR,
    AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL_ADDR,
)
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE = explorer_row_cave(
    AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH_ADDR,
    AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL_ADDR,
)
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE = explorer_row_cave(
    AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH_ADDR,
    AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL_ADDR,
)
AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE = refresh_row_cave()
AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_CODE = explorer_open_helper()
AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL = wide_label("Titles")
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL = wide_label("Authors")
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL = wide_label("Series")
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL = wide_label("Folders")
AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH = wide_path("a:\\Audiobooks\\_views\\Titles\\*")
AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH = wide_path("a:\\Audiobooks\\_views\\Authors\\*")
AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH = wide_path("a:\\Audiobooks\\_views\\Series\\*")
AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH = wide_path("a:\\Audiobooks\\.\\*")
AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD = b"/usr/bin/r1_audiobook_refresh.sh &\x00"

for name, blob, limit in (
    ("native hub Refresh row cave", AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE, 0x70),
    ("native hub Titles row cave", AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE, 0x30),
    ("native hub Authors row cave", AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE, 0x30),
    ("native hub Series row cave", AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE, 0x30),
    ("native hub Folders row cave", AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE, 0x30),
    ("native hub Titles label", AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL, 0x10),
    ("native hub Authors label", AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL, 0x10),
    ("native hub Series label", AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL, 0x10),
    ("native hub Folders label", AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL, 0x10),
    ("native hub Refresh command", AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD, 0x40),
    ("native hub explorer helper", AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_CODE, 0x180),
):
    if len(blob) > limit:
        raise ValueError(f"{name} is too large for its reserved patch cave: {len(blob)} > {limit}")

AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_PATCHES = (
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE),
        AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE),
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE),
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE),
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE),
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_CODE),
        AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL),
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL),
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL),
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL),
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH),
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH),
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH),
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH),
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD),
        AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD,
    ),
    (
        # Row 0: Refresh audiobook catalog.
        0x38D278,
        bytes.fromhex("000e5400"),
        pack_u32(AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CODE_ADDR),
    ),
    (
        # Row 1: Titles.
        0x38D27C,
        bytes.fromhex("08077600"),
        pack_u32(AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_ADDR),
    ),
    (
        # Row 2: Authors.
        0x38D280,
        bytes.fromhex("38077600"),
        pack_u32(AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_ADDR + 0x30),
    ),
    (
        # Row 3: Series.
        0x38D284,
        bytes.fromhex("68077600"),
        pack_u32(AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_ADDR + 0x60),
    ),
    (
        # Row 4: Folders.
        0x38D288,
        bytes.fromhex("b40d5400"),
        pack_u32(AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_BASE_ADDR + 0x90),
    ),
)

AUDIOBOOK_NATIVE_HUB_LAUNCHER_CAVE_OFFSET = AUDIOBOOK_LAUNCHER_CAVE_OFFSET
AUDIOBOOK_NATIVE_HUB_TITLE_LABEL_OFFSET = 0x35DF40
AUDIOBOOK_NATIVE_HUB_LAUNCHER_CODE = bytes.fromhex(
    # Clone the stock Books launcher callback body after its initial
    # already-active guard. This preserves the stock cleanup/open sequence:
    # find/remove vg_listview_main_book if present, then call the native
    # Books hub opener at 0x00540f20.
    "d8ffbd27"
    "2400bfaf"
    "2000b1af"
    "1c00b0af"
    "5000b08c"
    "e0381c0c"
    "2800048e"
    "07004014"
    "01000324"
    "2400bf8f"
    "2000b18f"
    "1c00b08f"
    "25106000"
    "0800e003"
    "2800bd27"
    "7800113c"
    "2cc12526"
    "d0c9100c"
    "25200002"
    "03004010"
    "2cc12526"
    "78cf100c"
    "25200002"
    "c803150c"
    "25200002"
    "2400bf8f"
    "01000324"
    "2000b18f"
    "1c00b08f"
    "25106000"
    "0800e003"
    "2800bd27"
)
AUDIOBOOK_NATIVE_HUB_TITLE_LABEL = b"titles\x00"
AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES = (
    (
        AUDIOBOOK_NATIVE_HUB_LAUNCHER_CAVE_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_LAUNCHER_CODE),
        AUDIOBOOK_NATIVE_HUB_LAUNCHER_CODE,
    ),
    (
        AUDIOBOOK_NATIVE_HUB_TITLE_LABEL_OFFSET,
        b"\x00" * len(AUDIOBOOK_NATIVE_HUB_TITLE_LABEL),
        AUDIOBOOK_NATIVE_HUB_TITLE_LABEL,
    ),
    (
        # The native hub's row-2 label normally reuses the shared "ebook"
        # resource key, which also names the launcher entry. Point just this
        # row at a private resource key so the launcher can still say
        # Audiobooks while the row displays as Titles.
        0x1407A0,
        bytes.fromhex("7800023c"),
        pack_u32(ins_lui(2, 0x0076)),
    ),
    (
        0x1407A8,
        bytes.fromhex("182a4224"),
        pack_u32(ins_addiu(2, 2, -0x20C0)),
    ),
    (
        AUDIOBOOK_LAUNCHER_CALLBACK_OFFSET,
        bytes.fromhex("20bb5300"),
        pack_u32(0x0075DAEC),
    ),
)


def digest(path: Path, algorithm: str) -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def patch_bytes(data: bytearray, offset: int, expected: bytes, replacement: bytes, skip_existing: bool = False) -> None:
    found = bytes(data[offset : offset + len(expected)])
    if found != expected:
        if skip_existing:
            if found == replacement:
                return  # Already patched with our patch, skip
            # Bytes don't match original or replacement — possibly patched by a different version
            # Skip with warning rather than failing
            import sys as _sys
            print(f"WARNING: skip_existing: offset 0x{offset:x} has unexpected bytes (found {found.hex()}, expected {expected.hex()}) — skipping", file=_sys.stderr)
            return
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
    audiobook_private_direct_route: bool,
    audiobook_native_hub_title_row: bool,
    audiobook_native_hub_launcher: bool,
    audiobook_native_hub_folder_rows: bool,
    audiobook_native_hub_view_rows: bool,
    audiobook_direct_open: bool,
    audiobook_title_autostart_marker: bool,
    audiobook_explorer_marker: bool,
    select_dispatch: bool,
    skip_existing: bool = False,
) -> None:
    md5 = digest(input_path, "md5")
    sha256 = digest(input_path, "sha256")
    if not skip_existing and (md5 != STOCK_MD5 or sha256 != STOCK_SHA256):
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
    if book_audio_shim and audiobook_native_hub_launcher:
        raise SystemExit(
            "The Books audio shim and native Audiobooks hub launcher patch both use the same "
            "code cave. Choose only one."
        )
    if audiobook_launcher_genre and (audiobook_native_hub_title_row or audiobook_native_hub_view_rows):
        raise SystemExit(
            "The Audiobooks direct launcher and native Audiobooks hub patches are alternative "
            "entry paths. Choose only one."
        )
    if audiobook_native_hub_launcher and not (audiobook_native_hub_title_row or audiobook_native_hub_view_rows):
        raise SystemExit(
            "The native Audiobooks hub launcher requires --audiobook-native-hub-title-row "
            "or --audiobook-native-hub-view-rows."
        )
    if audiobook_native_hub_view_rows and not audiobook_native_hub_launcher:
        raise SystemExit(
            "The native Audiobooks hub view rows require --audiobook-native-hub-launcher."
        )
    if audiobook_native_hub_folder_rows and not audiobook_native_hub_title_row:
        raise SystemExit(
            "The native Audiobooks hub folder rows require --audiobook-native-hub-title-row "
            "so the launcher uses the native hub entry path."
        )

    if scan_skip:
        patch_bytes(data, SCAN_SKIP_OFFSET, SCAN_SKIP_ORIGINAL, SCAN_SKIP_PATCHED, skip_existing=skip_existing)
        applied.append("scan-skip-audiobooks")

    if book_audio_shim:
        for offset, expected, replacement in BOOK_AUDIO_SHIM_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("book-audio-shim")

    if audiobook_launcher_genre:
        for offset, expected, replacement in AUDIOBOOK_LAUNCHER_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        if audiobook_private_direct_route:
            for offset, expected, replacement in AUDIOBOOK_PRIVATE_DIRECT_ROUTE_PATCHES:
                patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-launcher-genre")
        if audiobook_private_direct_route:
            applied.append("audiobook-private-direct-route")
    elif audiobook_private_direct_route:
        raise SystemExit("--audiobook-private-direct-route requires --audiobook-launcher-genre")

    if audiobook_native_hub_title_row:
        for offset, expected, replacement in AUDIOBOOK_NATIVE_HUB_TITLE_ROW_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-native-hub-title-row")

    if audiobook_native_hub_launcher:
        for offset, expected, replacement in AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-native-hub-launcher")

    if audiobook_native_hub_folder_rows:
        for offset, expected, replacement in AUDIOBOOK_NATIVE_HUB_FOLDER_ROWS_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-native-hub-folder-rows")

    if audiobook_native_hub_view_rows:
        for offset, expected, replacement in AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-native-hub-view-rows")

    if audiobook_direct_open:
        for offset, expected, replacement in AUDIOBOOK_DIRECT_OPEN_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-direct-open")

    if audiobook_title_autostart_marker:
        for offset, expected, replacement in AUDIOBOOK_TITLE_MARKER_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-title-autostart-marker")

    if audiobook_explorer_marker:
        for offset, expected, replacement in AUDIOBOOK_EXPLORER_MARKER_PATCHES:
            patch_bytes(data, offset, expected, replacement, skip_existing=skip_existing)
        applied.append("audiobook-explorer-marker")

    if select_dispatch:
        patch_bytes(data, *SELECT_DISPATCH_PATCH, skip_existing=skip_existing)
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
        "--audiobook-private-direct-route",
        action="store_true",
        help=(
            "With --audiobook-launcher-genre, point the Audiobooks launcher at a private "
            "Albums-of-Genre route record instead of the stock global genre-chain record. "
            "Experimental and off by default."
        ),
    )
    parser.add_argument(
        "--audiobook-native-hub-title-row",
        action="store_true",
        help=(
            "Keep the stock Books/Audiobooks hub and redirect its title row to the existing "
            "audiobook title route. Known unsafe after live testing rebooted the R1; "
            "off by default and intended only for controlled flash-test builds."
        ),
    )
    parser.add_argument(
        "--audiobook-native-hub-launcher",
        action="store_true",
        help=(
            "With --audiobook-native-hub-title-row, make the launcher always open the native "
            "Audiobooks hub root instead of restoring the previous subpage."
        ),
    )
    parser.add_argument(
        "--audiobook-native-hub-folder-rows",
        action="store_true",
        help=(
            "With --audiobook-native-hub-title-row, redirect the native hub Favorites/Files "
            "rows to the stock explorer rooted at /Audiobooks. Experimental and off by default."
        ),
    )
    parser.add_argument(
        "--audiobook-native-hub-view-rows",
        action="store_true",
        help=(
            "With --audiobook-native-hub-launcher, route the native Audiobooks hub "
            "Titles/Authors/Series/Folders rows to generated /Audiobooks/_views folders. "
            "Experimental and off by default."
        ),
    )
    parser.add_argument(
        "--audiobook-direct-open",
        action="store_true",
        help=(
            "Patch the native title-open wrapper at 0x540a80 to short-circuit "
            "Audiobooks taps directly into shared_media_open. The saved track "
            "index is read from 0x8E4400 when present and otherwise defaults "
            "to track 0. Experimental and off by default."
        ),
    )
    parser.add_argument(
        "--skip-existing-patches",
        action="store_true",
        help=(
            "Skip patches that are already applied (for re-patching an already-patched binary). "
            "Also relaxes the stock MD5/SHA256 check."
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
    parser.add_argument(
        "--audiobook-explorer-marker",
        action="store_true",
        help=(
            "Extended autostart marker: hooks the m3u callback at 0x4efe00 so "
            "the resume daemon detects .m3u title taps from the explorer view. "
            "Uses the same marker address (0x8E4000) as the title autostart marker."
        ),
    )
    args = parser.parse_args()

    apply_patches(
        input_path=args.input,
        output_path=args.output,
        scan_skip=args.scan_skip,
        book_audio_shim=args.book_audio_shim,
        audiobook_launcher_genre=args.audiobook_launcher_genre,
        audiobook_private_direct_route=args.audiobook_private_direct_route,
        audiobook_native_hub_title_row=args.audiobook_native_hub_title_row,
        audiobook_native_hub_launcher=args.audiobook_native_hub_launcher,
        audiobook_native_hub_folder_rows=args.audiobook_native_hub_folder_rows,
        audiobook_native_hub_view_rows=args.audiobook_native_hub_view_rows,
        audiobook_direct_open=args.audiobook_direct_open,
        audiobook_title_autostart_marker=args.audiobook_title_autostart_marker,
        audiobook_explorer_marker=args.audiobook_explorer_marker,
        select_dispatch=args.select_dispatch_branch,
        skip_existing=args.skip_existing_patches,
    )


if __name__ == "__main__":
    main()
