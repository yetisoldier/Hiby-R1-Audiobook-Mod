#!/usr/bin/env python3
"""RAM-only probe for restoring the stock Books hub as an Audiobooks hub.

The released audiobook firmware bypasses the stock ``vg_listview_main_book``
hub and opens the title list directly.  This helper temporarily restores the
native hub entry points in the running ``hiby_player`` process so we can test
whether a real Audiobooks submenu is a better foundation for Title / Author /
Series work.  Rebooting also discards the changes.
"""

from __future__ import annotations

import argparse
import struct
import time
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from adb_runtime_patch_hiby_player import (  # noqa: E402
    AUDIOBOOK_LAUNCHER_CALLBACK_NEW,
    AUDIOBOOK_LAUNCHER_CALLBACK_OLD,
    describe_live_launcher_state,
    file_offset_to_runtime_addr,
    find_live_launcher_callbacks,
    find_stock_player_pid,
    patch_live_launcher_callbacks,
    ptrace_write,
    read_memory_bytes,
    shell,
)
from patch_hiby_player import AUDIOBOOK_LAUNCHER_CALLBACK_OFFSET  # noqa: E402
from r1_adb_control import resolve_adb  # noqa: E402


# Keep large transient process-memory reads off /usr/data. The R1's /usr/data
# partition is small and is also where the firmware stores user state.
REMOTE_DIR_BASE = "/tmp/codex_native_hub_probe"
LOCAL_DIR_BASE = Path("work/runtime-patch-files/native-hub")

BOOK_HUB_OPEN_ADDR = 0x00540F20
BOOK_HUB_STOCK_PREFIX = bytes.fromhex("7800063c7800053c")
BOOK_HUB_AUDIOBOOK_PREFIX = bytes.fromhex("f0761d0800000000")
LAUNCHER_CALLBACK_ADDR = file_offset_to_runtime_addr(AUDIOBOOK_LAUNCHER_CALLBACK_OFFSET)

MAIN_BOOK_ROW1_JUMP_ADDR = 0x0078D27C
MAIN_BOOK_ROW1_STOCK = (0x00540D08).to_bytes(4, "little")
MAIN_BOOK_ROW1_TITLE = (0x0075DE60).to_bytes(4, "little")
MAIN_BOOK_TITLE_CAVE_ADDR = 0x0075DE60
MAIN_BOOK_TITLE_CAVE_STOCK = b"\x00" * 20
MAIN_BOOK_TITLE_CAVE_CODE = bytes.fromhex(
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

MAIN_BOOK_ROW2_JUMP_ADDR = 0x0078D280
MAIN_BOOK_ROW2_STOCK = (0x00540E2C).to_bytes(4, "little")
MAIN_BOOK_ROW2_AUTHOR = (0x0075DE80).to_bytes(4, "little")
MAIN_BOOK_ROW2_FOLDER = (0x0075DEA0).to_bytes(4, "little")
MAIN_BOOK_AUTHOR_CAVE_ADDR = 0x0075DE80

MAIN_BOOK_ROW3_JUMP_ADDR = 0x0078D284
MAIN_BOOK_ROW3_STOCK = (0x00540D48).to_bytes(4, "little")
MAIN_BOOK_ROW3_FOLDER = (0x0075DEA0).to_bytes(4, "little")
MAIN_BOOK_FOLDER_CAVE_ADDR = 0x0075DEA0
MAIN_BOOK_FOLDER_PATH_ADDR = 0x0075DF00


def ins_j(addr: int) -> int:
    return (2 << 26) | ((addr >> 2) & 0x03FFFFFF)


def ins_jal(addr: int) -> int:
    return (3 << 26) | ((addr >> 2) & 0x03FFFFFF)


def ins_lui(rt: int, imm: int) -> int:
    return (15 << 26) | ((rt & 0x1F) << 16) | (imm & 0xFFFF)


def ins_addiu(rt: int, rs: int, imm: int) -> int:
    return (9 << 26) | ((rs & 0x1F) << 21) | ((rt & 0x1F) << 16) | (imm & 0xFFFF)


def ins_lw(rt: int, base: int, off: int) -> int:
    return (35 << 26) | ((base & 0x1F) << 21) | ((rt & 0x1F) << 16) | (off & 0xFFFF)


def pack_words(*words: int) -> bytes:
    return b"".join(struct.pack("<I", word & 0xFFFFFFFF) for word in words)


MAIN_BOOK_AUTHOR_CAVE_CODE = pack_words(
    # Use the same native open sequence as the stock Music -> Album Artist row,
    # adapted to the current Books-hub row context.
    ins_lw(4, 16, 0x00D8),  # lw a0, 0xd8(s0)
    ins_jal(0x0049AC80),
    0,
    ins_lw(4, 16, 0x00D8),  # lw a0, 0xd8(s0)
    ins_lui(6, 0x78),
    ins_addiu(6, 6, -0x0AB4),  # album_artist
    ins_lui(5, 0x78),
    ins_jal(0x004961E0),
    ins_addiu(5, 5, -0x4284),  # vg_listview_album_artist
    ins_lw(4, 16, 0x00D8),  # lw a0, 0xd8(s0)
    ins_lui(5, 0x78),
    ins_jal(0x004FEB80),
    ins_addiu(5, 5, -0x4284),  # vg_listview_album_artist
    ins_j(0x00540D2C),
    0,
)
MAIN_BOOK_AUTHOR_CAVE_STOCK = b"\x00" * len(MAIN_BOOK_AUTHOR_CAVE_CODE)

MAIN_BOOK_FOLDER_CAVE_CODE = pack_words(
    # Open the stock explorer list with a supplied root. This is the same
    # native call pattern as the stock Books -> Files row, but rooted at
    # a:\Audiobooks\* instead of a:\book\*.
    ins_lw(4, 16, 0x00D8),  # lw a0, 0xd8(s0)
    ins_lui(5, 0x0076),
    ins_addiu(5, 5, -0x3ECC),  # vg_listview_explorer at 0x0075c134
    ins_lui(6, 0x0076),
    ins_addiu(6, 6, -0x2100),  # a:\Audiobooks\* at 0x0075df00
    ins_jal(0x004961E0),
    0,
    ins_j(0x00540D2C),
    0,
)
MAIN_BOOK_FOLDER_CAVE_STOCK = b"\x00" * len(MAIN_BOOK_FOLDER_CAVE_CODE)
MAIN_BOOK_FOLDER_PATH_DATA = ("a:\\Audiobooks\\*".encode("utf-16le") + b"\x00\x00").ljust(0x40, b"\x00")
MAIN_BOOK_FOLDER_PATH_STOCK = b"\x00" * len(MAIN_BOOK_FOLDER_PATH_DATA)


def classify(actual: bytes, stock: bytes, audiobook: bytes) -> str:
    if actual == stock:
        return "stock"
    if actual == audiobook:
        return "audiobook"
    return f"unknown:{actual.hex()}"


def classify_named(actual: bytes, known: list[tuple[str, bytes]]) -> str:
    for name, expected in known:
        if actual == expected:
            return name
    return f"unknown:{actual.hex()}"


def process_mem_is_live(adb: str, pid: str) -> bool:
    status = shell(adb, f"if [ -r /proc/{pid}/mem ]; then echo live; else echo missing; fi", check=False)
    return status.strip().endswith("live")


def should_retry_process_read(exc: Exception) -> bool:
    text = str(exc)
    return (
        "/proc/" in text
        and (
            "No such file" in text
            or "__SIZE__:missing" in text
            or "can't open" in text
        )
    )


def read_state(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    *,
    scan_heap_callbacks: bool,
) -> tuple[str, str, str]:
    current_pid = pid
    pid_refreshes: list[str] = []

    def ensure_pid() -> str:
        nonlocal current_pid
        if process_mem_is_live(adb, current_pid):
            return current_pid
        old_pid = current_pid
        current_pid = find_stock_player_pid(adb)
        pid_refreshes.append(f"{old_pid}->{current_pid}")
        return current_pid

    def read_one(addr: int, size: int, name: str) -> bytes:
        nonlocal current_pid
        last_error: Exception | None = None
        for _attempt in range(2):
            ensure_pid()
            try:
                return read_memory_bytes(adb, current_pid, addr, size, remote_dir, local_dir, name, "state")
            except RuntimeError as exc:
                last_error = exc
                if not should_retry_process_read(exc):
                    raise
                current_pid = find_stock_player_pid(adb)
                pid_refreshes.append(f"retry->{current_pid}")
        assert last_error is not None
        raise last_error

    def find_callbacks_live() -> list:
        nonlocal current_pid
        last_error: Exception | None = None
        for _attempt in range(2):
            ensure_pid()
            try:
                return find_live_launcher_callbacks(adb, current_pid, remote_dir, local_dir)
            except RuntimeError as exc:
                last_error = exc
                if not should_retry_process_read(exc):
                    raise
                current_pid = find_stock_player_pid(adb)
                pid_refreshes.append(f"retry->{current_pid}")
        assert last_error is not None
        raise last_error

    hook = read_one(
        BOOK_HUB_OPEN_ADDR,
        len(BOOK_HUB_STOCK_PREFIX),
        "book_hub_open",
    )
    callback = read_one(
        LAUNCHER_CALLBACK_ADDR,
        len(AUDIOBOOK_LAUNCHER_CALLBACK_OLD),
        "launcher_callback_static",
    )
    callbacks = find_callbacks_live() if scan_heap_callbacks else []
    row1 = read_one(
        MAIN_BOOK_ROW1_JUMP_ADDR,
        len(MAIN_BOOK_ROW1_STOCK),
        "main_book_row1_jump",
    )
    row1_state = classify(row1, MAIN_BOOK_ROW1_STOCK, MAIN_BOOK_ROW1_TITLE)
    row2 = read_one(
        MAIN_BOOK_ROW2_JUMP_ADDR,
        len(MAIN_BOOK_ROW2_STOCK),
        "main_book_row2_jump",
    )
    row2_state = classify_named(
        row2,
        [
            ("stock", MAIN_BOOK_ROW2_STOCK),
            ("author", MAIN_BOOK_ROW2_AUTHOR),
            ("folder", MAIN_BOOK_ROW2_FOLDER),
        ],
    )
    row3 = read_one(
        MAIN_BOOK_ROW3_JUMP_ADDR,
        len(MAIN_BOOK_ROW3_STOCK),
        "main_book_row3_jump",
    )
    callback_summary = describe_live_launcher_state(callbacks) if scan_heap_callbacks else "not-scanned"
    if pid_refreshes:
        callback_summary = f"{callback_summary} pid_refreshes={','.join(pid_refreshes)}"
    row3_state = classify_named(
        row3,
        [
            ("stock", MAIN_BOOK_ROW3_STOCK),
            ("folder", MAIN_BOOK_ROW3_FOLDER),
        ],
    )
    return (
        classify(hook, BOOK_HUB_STOCK_PREFIX, BOOK_HUB_AUDIOBOOK_PREFIX),
        classify(callback, AUDIOBOOK_LAUNCHER_CALLBACK_OLD, AUDIOBOOK_LAUNCHER_CALLBACK_NEW),
        f"{callback_summary} row1={row1_state} row2={row2_state} row3={row3_state}",
    )


def write_mode(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    *,
    native_hub: bool,
    title_row: bool,
    author_row: bool,
    folder_rows: bool,
    patch_heap_callbacks: bool,
) -> None:
    hook = BOOK_HUB_STOCK_PREFIX if native_hub else BOOK_HUB_AUDIOBOOK_PREFIX
    callback = AUDIOBOOK_LAUNCHER_CALLBACK_OLD if native_hub else AUDIOBOOK_LAUNCHER_CALLBACK_NEW
    label = "native_hub" if native_hub else "audiobook_direct"
    row1 = MAIN_BOOK_ROW1_TITLE if title_row else MAIN_BOOK_ROW1_STOCK
    cave = MAIN_BOOK_TITLE_CAVE_CODE if title_row else MAIN_BOOK_TITLE_CAVE_STOCK
    row2 = MAIN_BOOK_ROW2_STOCK
    row3 = MAIN_BOOK_ROW3_STOCK
    if author_row:
        row2 = MAIN_BOOK_ROW2_AUTHOR
    if folder_rows:
        row2 = MAIN_BOOK_ROW2_FOLDER
        row3 = MAIN_BOOK_ROW3_FOLDER
    writes = [
        ("book_hub_open_prefix", BOOK_HUB_OPEN_ADDR, hook),
        ("launcher_callback_static", LAUNCHER_CALLBACK_ADDR, callback),
        ("main_book_title_row_cave", MAIN_BOOK_TITLE_CAVE_ADDR, cave),
        ("main_book_row1_jump", MAIN_BOOK_ROW1_JUMP_ADDR, row1),
        ("main_book_row2_jump", MAIN_BOOK_ROW2_JUMP_ADDR, row2),
        ("main_book_row3_jump", MAIN_BOOK_ROW3_JUMP_ADDR, row3),
    ]
    if author_row:
        writes.append(("main_book_author_row_cave", MAIN_BOOK_AUTHOR_CAVE_ADDR, MAIN_BOOK_AUTHOR_CAVE_CODE))
    elif folder_rows:
        writes.extend(
            [
                ("main_book_folder_rows_cave", MAIN_BOOK_FOLDER_CAVE_ADDR, MAIN_BOOK_FOLDER_CAVE_CODE),
                ("main_book_folder_rows_path", MAIN_BOOK_FOLDER_PATH_ADDR, MAIN_BOOK_FOLDER_PATH_DATA),
            ]
        )
    else:
        writes.extend(
            [
                ("main_book_author_row_cave", MAIN_BOOK_AUTHOR_CAVE_ADDR, MAIN_BOOK_AUTHOR_CAVE_STOCK),
                ("main_book_folder_rows_cave", MAIN_BOOK_FOLDER_CAVE_ADDR, MAIN_BOOK_FOLDER_CAVE_STOCK),
                ("main_book_folder_rows_path", MAIN_BOOK_FOLDER_PATH_ADDR, MAIN_BOOK_FOLDER_PATH_STOCK),
            ]
        )
    ptrace_write(
        adb,
        pid,
        remote_dir,
        local_dir,
        label,
        writes,
    )
    if patch_heap_callbacks:
        patch_live_launcher_callbacks(
            adb,
            pid,
            remote_dir,
            local_dir,
            patched=not native_hub,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default="")
    parser.add_argument("--apply-native-hub", action="store_true")
    parser.add_argument("--patch-title-row", action="store_true")
    parser.add_argument("--patch-author-row", action="store_true")
    parser.add_argument("--patch-folder-rows", action="store_true")
    parser.add_argument(
        "--scan-heap-callbacks",
        action="store_true",
        help="Also scan heap launcher objects. This can be memory-heavy on the R1.",
    )
    parser.add_argument(
        "--patch-heap-callbacks",
        action="store_true",
        help="Also patch current heap launcher objects. This implies a heap scan and can be memory-heavy on the R1.",
    )
    parser.add_argument("--restore-audiobook-direct", action="store_true")
    parser.add_argument(
        "--i-understand-this-writes-process-memory",
        action="store_true",
        help="Required with --apply-native-hub or --restore-audiobook-direct.",
    )
    args = parser.parse_args()

    if args.patch_title_row or args.patch_author_row or args.patch_folder_rows:
        args.apply_native_hub = True
    if args.patch_heap_callbacks:
        args.scan_heap_callbacks = True

    if args.apply_native_hub and args.restore_audiobook_direct:
        parser.error("choose only one mode change")
    if args.patch_author_row and args.patch_folder_rows:
        parser.error("--patch-author-row and --patch-folder-rows use overlapping code caves")
    if (args.apply_native_hub or args.restore_audiobook_direct) and not args.i_understand_this_writes_process_memory:
        parser.error("mode changes require --i-understand-this-writes-process-memory")

    adb = resolve_adb(args.adb)
    pid = find_stock_player_pid(adb)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    remote_dir = f"{REMOTE_DIR_BASE}_{stamp}"
    local_dir = LOCAL_DIR_BASE / stamp
    shell(adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    before = read_state(adb, pid, remote_dir, local_dir, scan_heap_callbacks=args.scan_heap_callbacks)
    print(f"player pid: {pid}")
    print(f"before: hub_open={before[0]} static_callback={before[1]} heap_callbacks={before[2]}")

    if not args.apply_native_hub and not args.restore_audiobook_direct:
        print("dry run only; no process memory was written")
        return 0

    write_mode(
        adb,
        pid,
        remote_dir,
        local_dir,
        native_hub=args.apply_native_hub,
        title_row=args.patch_title_row,
        author_row=args.patch_author_row,
        folder_rows=args.patch_folder_rows,
        patch_heap_callbacks=args.patch_heap_callbacks,
    )
    after = read_state(adb, pid, remote_dir, local_dir, scan_heap_callbacks=args.scan_heap_callbacks)
    print(f"after:  hub_open={after[0]} static_callback={after[1]} heap_callbacks={after[2]}")
    print(f"remote dir: {remote_dir}")
    print(f"local dir: {local_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
