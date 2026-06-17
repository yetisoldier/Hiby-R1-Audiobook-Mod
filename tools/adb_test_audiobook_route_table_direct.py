#!/usr/bin/env python3
"""RAM-only test for opening Audiobooks through a direct genre-album route.

The released Audiobooks launcher uses the stock ``genre\\Audiobook`` route.
That is stable, but it leaves the stock Genres parent on the Back stack. This
helper temporarily edits the running ``hiby_player`` route table so the existing
``genre`` route record opens the genre's albums/books list directly. It writes
only process memory; rebooting the R1 discards the change.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from adb_runtime_patch_hiby_player import (  # noqa: E402
    DEFAULT_ADB,
    find_stock_player_pid,
    ptrace_write,
    read_memory_bytes,
    shell,
)


REMOTE_DIR_BASE = "/usr/data/codex_route_table_direct"

GENRE_CHAIN_RECORD = 0x007870A0
GENRE_CHAIN_VIEW = GENRE_CHAIN_RECORD + 0x08
GENRE_CHAIN_CHILD = GENRE_CHAIN_RECORD + 0x0C
GENRE_CHAIN_NEXT = GENRE_CHAIN_RECORD + 0x10
GENRE_CHAIN_CALLBACK = GENRE_CHAIN_RECORD + 0x14

VG_LISTVIEW_GENRE = 0x0077BBF0
VG_LISTVIEW_ALBUMS_OF_A_GENRE = 0x0077F284
VG_LISTVIEW_SONGS_OF_AN_ALBUM_AND_A_GENRE = 0x0077F2A4

CALLBACK_CHAIN = 0x004EFFC0
CALLBACK_SIMPLE = 0x004F01C0

CURRENT_FIELDS = {
    "genre_record_view": (GENRE_CHAIN_VIEW, VG_LISTVIEW_GENRE),
    "genre_record_child": (GENRE_CHAIN_CHILD, VG_LISTVIEW_ALBUMS_OF_A_GENRE),
    "genre_record_next": (GENRE_CHAIN_NEXT, VG_LISTVIEW_SONGS_OF_AN_ALBUM_AND_A_GENRE),
    "genre_record_callback": (GENRE_CHAIN_CALLBACK, CALLBACK_CHAIN),
}

DIRECT_FIELDS = {
    "genre_record_view": (GENRE_CHAIN_VIEW, VG_LISTVIEW_ALBUMS_OF_A_GENRE),
    "genre_record_child": (GENRE_CHAIN_CHILD, VG_LISTVIEW_SONGS_OF_AN_ALBUM_AND_A_GENRE),
    "genre_record_next": (GENRE_CHAIN_NEXT, 0),
    "genre_record_callback": (GENRE_CHAIN_CALLBACK, CALLBACK_SIMPLE),
}


def pack_word(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def read_word(adb: str, pid: str, addr: int, remote_dir: str, local_dir: Path, label: str) -> int:
    data = read_memory_bytes(adb, pid, addr, 4, remote_dir, local_dir, label, "state")
    return struct.unpack("<I", data)[0]


def state(adb: str, pid: str, remote_dir: str, local_dir: Path) -> str:
    values = {
        name: read_word(adb, pid, addr, remote_dir, local_dir, name)
        for name, (addr, _expected) in CURRENT_FIELDS.items()
    }
    if all(values[name] == expected for name, (_addr, expected) in CURRENT_FIELDS.items()):
        return "current"
    if all(values[name] == expected for name, (_addr, expected) in DIRECT_FIELDS.items()):
        return "direct-genre-albums"
    detail = ", ".join(f"{name}=0x{value:08x}" for name, value in values.items())
    return f"unknown({detail})"


def verify(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    expected_fields: dict[str, tuple[int, int]],
) -> None:
    for name, (addr, expected) in expected_fields.items():
        actual = read_word(adb, pid, addr, remote_dir, local_dir, f"{name}_verify")
        if actual != expected:
            raise RuntimeError(f"{name} expected 0x{expected:08x}, got 0x{actual:08x}")


def write_fields(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    label: str,
    fields: dict[str, tuple[int, int]],
) -> None:
    ptrace_write(
        adb,
        pid,
        remote_dir,
        local_dir,
        label,
        [(name, addr, pack_word(value)) for name, (addr, value) in fields.items()],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--revert", action="store_true")
    parser.add_argument(
        "--i-understand-this-writes-process-memory",
        action="store_true",
        help="Required with --apply or --revert.",
    )
    args = parser.parse_args()

    if args.apply and args.revert:
        parser.error("choose only one of --apply or --revert")
    if (args.apply or args.revert) and not args.i_understand_this_writes_process_memory:
        parser.error("--apply/--revert requires --i-understand-this-writes-process-memory")

    pid = find_stock_player_pid(args.adb)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    remote_dir = f"{REMOTE_DIR_BASE}_{stamp}"
    local_dir = Path("work/runtime-patch-files") / f"route-table-direct-{stamp}"
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    current_state = state(args.adb, pid, remote_dir, local_dir)
    print(f"player pid: {pid}")
    print(f"route table state: {current_state}")
    if not args.apply and not args.revert:
        print("dry run only; no process memory was written")
        return 0

    if args.apply:
        if current_state == "direct-genre-albums":
            print("direct genre-albums route table patch already active")
            return 0
        if current_state != "current":
            raise RuntimeError(f"refusing to apply over non-current state: {current_state}")
        write_fields(args.adb, pid, remote_dir, local_dir, "apply_direct_genre_albums", DIRECT_FIELDS)
        verify(args.adb, pid, remote_dir, local_dir, DIRECT_FIELDS)
        print("applied direct genre-albums route table patch")
        return 0

    if current_state == "current":
        print("route table is already current")
        return 0
    if current_state != "direct-genre-albums":
        raise RuntimeError(f"refusing to revert non-direct state: {current_state}")
    write_fields(args.adb, pid, remote_dir, local_dir, "revert_direct_genre_albums", CURRENT_FIELDS)
    verify(args.adb, pid, remote_dir, local_dir, CURRENT_FIELDS)
    print("reverted direct genre-albums route table patch")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
