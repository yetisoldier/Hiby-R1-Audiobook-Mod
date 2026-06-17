#!/usr/bin/env python3
"""RAM-only tests for the Audiobooks launcher's route-record pointer.

The Audiobooks launcher cave calls the stock simple route callback at
``0x004f01c0`` with a hardcoded route-record pointer in ``$a1``. The released
firmware uses the stock chained genre record at ``0x007870a0``. This helper
temporarily points the launcher at nearby stock route records so their behavior
can be tested without flashing.
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


REMOTE_DIR_BASE = "/usr/data/codex_launcher_record"
LAUNCHER_A1_ADDIU = 0x0075DB2C
BOOK_OPEN_A1_ADDIU = 0x0075DBEC
CALLSITES = (
    ("launcher_record_addiu", LAUNCHER_A1_ADDIU),
    ("book_open_record_addiu", BOOK_OPEN_A1_ADDIU),
)

RECORD_PRESETS = {
    "album": 0x00787040,
    "artist-simple": 0x00787058,
    "artist-chain": 0x00787070,
    "genre-simple": 0x00787088,
    "genre-chain": 0x007870A0,
    "m3u": 0x007870D0,
    "format": 0x007870E8,
}


def addiu_a1_low(record_addr: int) -> bytes:
    if (record_addr >> 16) != 0x78:
        raise ValueError(f"record address is not reachable with current lui $a1,0x78: 0x{record_addr:x}")
    word = 0x24A50000 | (record_addr & 0xFFFF)
    return struct.pack("<I", word)


CURRENT = addiu_a1_low(RECORD_PRESETS["genre-chain"])


def read_word(adb: str, pid: str, addr: int, remote_dir: str, local_dir: Path, label: str) -> bytes:
    return read_memory_bytes(adb, pid, addr, 4, remote_dir, local_dir, label, "state")


def decode_state_value(raw: bytes) -> int | None:
    if len(raw) != 4:
        return None
    word = struct.unpack("<I", raw)[0]
    if (word & 0xFFFF0000) != 0x24A50000:
        return None
    low = word & 0xFFFF
    return 0x00780000 | low


def state(adb: str, pid: str, remote_dir: str, local_dir: Path) -> str:
    values = [read_word(adb, pid, addr, remote_dir, local_dir, name) for name, addr in CALLSITES]
    decoded = [decode_state_value(value) for value in values]
    if decoded[0] is not None and decoded[0] == decoded[1]:
        for name, addr in RECORD_PRESETS.items():
            if decoded[0] == addr:
                return name
        return f"record-0x{decoded[0]:08x}"
    return ",".join(value.hex() for value in values)


def write_record(adb: str, pid: str, remote_dir: str, local_dir: Path, label: str, record_addr: int) -> None:
    value = addiu_a1_low(record_addr)
    ptrace_write(
        adb,
        pid,
        remote_dir,
        local_dir,
        label,
        [(name, addr, value) for name, addr in CALLSITES],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--preset", choices=sorted(RECORD_PRESETS), default="genre-chain")
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
    local_dir = Path("work/runtime-patch-files") / f"launcher-record-{stamp}"
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    current_state = state(args.adb, pid, remote_dir, local_dir)
    target = "genre-chain" if args.revert else args.preset
    print(f"player pid: {pid}")
    print(f"launcher record state: {current_state}")
    print(f"target record: {target} 0x{RECORD_PRESETS[target]:08x}")
    if not args.apply and not args.revert:
        print("dry run only; no process memory was written")
        return 0

    if args.apply:
        if current_state == args.preset:
            print(f"{args.preset} record already active")
            return 0
        if current_state != "genre-chain":
            raise RuntimeError(f"refusing to apply over non-current record state: {current_state}")
        write_record(args.adb, pid, remote_dir, local_dir, f"apply_{args.preset}", RECORD_PRESETS[args.preset])
        print(f"applied launcher record preset {args.preset}")
        return 0

    if current_state == "genre-chain":
        print("launcher record already restored")
        return 0
    write_record(args.adb, pid, remote_dir, local_dir, "revert_genre_chain", RECORD_PRESETS["genre-chain"])
    print("reverted launcher record to genre-chain")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
