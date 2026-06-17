#!/usr/bin/env python3
"""RAM-only test for the Audiobooks launcher callback target.

The current Audiobooks launcher cave passes the stock genre route record
(``0x007870a0``) but calls the simple route callback at ``0x004f01c0``
directly. The route record itself points at the chained callback
``0x004effc0``. This helper swaps only the two launcher cave `jal` instructions
between those callback targets so the behavior can be tested without flashing.
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


REMOTE_DIR_BASE = "/usr/data/codex_launcher_callback"
LAUNCHER_CALLBACK_CALL = 0x0075DB5C
BOOK_OPEN_CALLBACK_CALL = 0x0075DC1C
CALLSITES = (
    ("launcher_callback_call", LAUNCHER_CALLBACK_CALL),
    ("book_open_callback_call", BOOK_OPEN_CALLBACK_CALL),
)

CALLBACK_SIMPLE = 0x004F01C0
CALLBACK_CHAIN = 0x004EFFC0


def jal_bytes(addr: int) -> bytes:
    word = 0x0C000000 | ((addr >> 2) & 0x03FFFFFF)
    return struct.pack("<I", word)


SIMPLE_JAL = jal_bytes(CALLBACK_SIMPLE)
CHAIN_JAL = jal_bytes(CALLBACK_CHAIN)


def read_call(adb: str, pid: str, addr: int, remote_dir: str, local_dir: Path, name: str) -> bytes:
    return read_memory_bytes(adb, pid, addr, 4, remote_dir, local_dir, name, "state")


def state(adb: str, pid: str, remote_dir: str, local_dir: Path) -> str:
    actual = [read_call(adb, pid, addr, remote_dir, local_dir, name) for name, addr in CALLSITES]
    if all(item == SIMPLE_JAL for item in actual):
        return "simple"
    if all(item == CHAIN_JAL for item in actual):
        return "chain"
    return ",".join(item.hex() for item in actual)


def write_calls(adb: str, pid: str, remote_dir: str, local_dir: Path, label: str, value: bytes) -> None:
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
    parser.add_argument("--apply-chain", action="store_true")
    parser.add_argument("--revert-simple", action="store_true")
    parser.add_argument(
        "--i-understand-this-writes-process-memory",
        action="store_true",
        help="Required with --apply-chain or --revert-simple.",
    )
    args = parser.parse_args()

    if args.apply_chain and args.revert_simple:
        parser.error("choose only one of --apply-chain or --revert-simple")
    if (args.apply_chain or args.revert_simple) and not args.i_understand_this_writes_process_memory:
        parser.error("--apply-chain/--revert-simple requires --i-understand-this-writes-process-memory")

    pid = find_stock_player_pid(args.adb)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    remote_dir = f"{REMOTE_DIR_BASE}_{stamp}"
    local_dir = Path("work/runtime-patch-files") / f"launcher-callback-{stamp}"
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    current_state = state(args.adb, pid, remote_dir, local_dir)
    print(f"player pid: {pid}")
    print(f"launcher callback state: {current_state}")
    if not args.apply_chain and not args.revert_simple:
        print("dry run only; no process memory was written")
        return 0

    if args.apply_chain:
        if current_state == "chain":
            print("chain callback already active")
            return 0
        if current_state != "simple":
            raise RuntimeError(f"refusing to apply over non-simple state: {current_state}")
        write_calls(args.adb, pid, remote_dir, local_dir, "apply_chain_launcher_callback", CHAIN_JAL)
        print("applied chain callback target to launcher cave")
        return 0

    if current_state == "simple":
        print("simple callback already active")
        return 0
    if current_state != "chain":
        raise RuntimeError(f"refusing to revert non-chain state: {current_state}")
    write_calls(args.adb, pid, remote_dir, local_dir, "revert_simple_launcher_callback", SIMPLE_JAL)
    print("reverted launcher cave to simple callback target")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
