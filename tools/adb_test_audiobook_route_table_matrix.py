#!/usr/bin/env python3
"""RAM-only field matrix for the Audiobooks genre-chain route record.

The released launcher passes the stock genre-chain route record at
``0x007870a0`` while directly calling the simple route callback. Earlier tests
changed several route fields at once, which proved the direct route was not
shippable but did not isolate which field caused each regression. This helper
patches named field combinations in process memory only; rebooting the R1 also
discards every change.
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


REMOTE_DIR_BASE = "/usr/data/codex_route_table_matrix"

GENRE_CHAIN_RECORD = 0x007870A0
FIELD_OFFSETS = {
    "view": 0x08,
    "child": 0x0C,
    "next": 0x10,
    "callback": 0x14,
}

VG_LISTVIEW_GENRE = 0x0077BBF0
VG_LISTVIEW_ALBUMS_OF_A_GENRE = 0x0077F284
VG_LISTVIEW_SONGS_OF_A_GENRE = 0x00783380
VG_LISTVIEW_SONGS_OF_AN_ALBUM_AND_A_GENRE = 0x0077F2A4

EMPTY_STRING = 0x007DF654
CALLBACK_CHAIN = 0x004EFFC0
CALLBACK_SIMPLE = 0x004F01C0

CURRENT_FIELDS = {
    "view": VG_LISTVIEW_GENRE,
    "child": VG_LISTVIEW_ALBUMS_OF_A_GENRE,
    "next": VG_LISTVIEW_SONGS_OF_AN_ALBUM_AND_A_GENRE,
    "callback": CALLBACK_CHAIN,
}

VARIANTS = {
    "callback-simple-only": {
        **CURRENT_FIELDS,
        "callback": CALLBACK_SIMPLE,
    },
    "next-empty-only": {
        **CURRENT_FIELDS,
        "next": EMPTY_STRING,
    },
    "next-empty-simple": {
        **CURRENT_FIELDS,
        "next": EMPTY_STRING,
        "callback": CALLBACK_SIMPLE,
    },
    "songs-of-genre-simple": {
        **CURRENT_FIELDS,
        "child": VG_LISTVIEW_SONGS_OF_A_GENRE,
        "next": EMPTY_STRING,
        "callback": CALLBACK_SIMPLE,
    },
    "direct-view-only": {
        **CURRENT_FIELDS,
        "view": VG_LISTVIEW_ALBUMS_OF_A_GENRE,
    },
    "direct-view-empty-next": {
        **CURRENT_FIELDS,
        "view": VG_LISTVIEW_ALBUMS_OF_A_GENRE,
        "next": EMPTY_STRING,
    },
    "direct-view-child-simple": {
        **CURRENT_FIELDS,
        "view": VG_LISTVIEW_ALBUMS_OF_A_GENRE,
        "child": VG_LISTVIEW_SONGS_OF_AN_ALBUM_AND_A_GENRE,
        "next": EMPTY_STRING,
        "callback": CALLBACK_SIMPLE,
    },
}


def pack_word(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def field_address(name: str) -> int:
    return GENRE_CHAIN_RECORD + FIELD_OFFSETS[name]


def read_word(adb: str, pid: str, addr: int, remote_dir: str, local_dir: Path, label: str) -> int:
    data = read_memory_bytes(adb, pid, addr, 4, remote_dir, local_dir, label, "state")
    return struct.unpack("<I", data)[0]


def read_fields(adb: str, pid: str, remote_dir: str, local_dir: Path) -> dict[str, int]:
    return {
        name: read_word(adb, pid, field_address(name), remote_dir, local_dir, name)
        for name in FIELD_OFFSETS
    }


def state_for(values: dict[str, int]) -> str:
    if values == CURRENT_FIELDS:
        return "current"
    for name, fields in VARIANTS.items():
        if values == fields:
            return name
    detail = ", ".join(f"{name}=0x{value:08x}" for name, value in values.items())
    return f"unknown({detail})"


def write_fields(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    label: str,
    fields: dict[str, int],
) -> None:
    ptrace_write(
        adb,
        pid,
        remote_dir,
        local_dir,
        label,
        [(name, field_address(name), pack_word(value)) for name, value in fields.items()],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--variant", choices=sorted(VARIANTS), default="callback-simple-only")
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
    local_dir = Path("work/runtime-patch-files") / f"route-table-matrix-{stamp}"
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    values = read_fields(args.adb, pid, remote_dir, local_dir)
    current_state = state_for(values)
    print(f"player pid: {pid}")
    print(f"route matrix state: {current_state}")
    for name in FIELD_OFFSETS:
        print(f"  {name}: 0x{values[name]:08x}")

    if not args.apply and not args.revert:
        print("dry run only; no process memory was written")
        return 0

    if args.apply:
        if current_state == args.variant:
            print(f"{args.variant} is already active")
            return 0
        if current_state != "current":
            raise RuntimeError(f"refusing to apply over non-current state: {current_state}")
        target = VARIANTS[args.variant]
        write_fields(args.adb, pid, remote_dir, local_dir, f"apply_{args.variant}", target)
        if read_fields(args.adb, pid, remote_dir, local_dir) != target:
            raise RuntimeError(f"failed to verify variant {args.variant}")
        print(f"applied route matrix variant {args.variant}")
        return 0

    if current_state == "current":
        print("route matrix is already current")
        return 0
    if current_state not in VARIANTS:
        raise RuntimeError(f"refusing to revert unknown state: {current_state}")
    write_fields(args.adb, pid, remote_dir, local_dir, "revert_current", CURRENT_FIELDS)
    if read_fields(args.adb, pid, remote_dir, local_dir) != CURRENT_FIELDS:
        raise RuntimeError("failed to verify route matrix revert")
    print("reverted route matrix to current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
