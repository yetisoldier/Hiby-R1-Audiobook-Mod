#!/usr/bin/env python3
"""RAM-only test for a direct Audiobooks album route.

The flashed Audiobooks launcher currently opens the stock Genre -> Albums route
for ``genre\\Audiobook``. That works, but leaves a Genres parent view on the
back stack. This helper temporarily rewrites the running helper code in
``hiby_player`` to try a direct filtered album route. It does not edit rootfs or
flash firmware; rebooting the device discards the change.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from adb_runtime_patch_hiby_player import (  # noqa: E402
    DEFAULT_ADB,
    find_stock_player_pid,
    file_offset_to_runtime_addr,
    ptrace_write,
    read_memory_bytes,
    run_adb,
    shell,
)
from patch_hiby_player import (  # noqa: E402
    AUDIOBOOK_BOOK_OPEN_ROOT_CODE,
    AUDIOBOOK_BOOK_OPEN_ROOT_OFFSET,
    AUDIOBOOK_LAUNCHER_CAVE_OFFSET,
    AUDIOBOOK_LAUNCHER_CODE,
    AUDIOBOOK_LAUNCHER_ROUTE,
    AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE,
    AUDIOBOOK_LAUNCHER_SELECTED_GENRE,
)


REMOTE_DIR_BASE = "/usr/data/codex_direct_filter_route"

DIRECT_FILTER_LAUNCHER = bytes.fromhex(
    "d0ffbd272c00bfaf2800b1af2400b0af5000b08c1200001200000000"
    "2800048ee0381c0c000000000d004010000000007600113c80db3126"
    "7800053c84f2a524253020027858120c252000020400401000000000"
    "2530000060fc130c25302002010002242c00bf8f2800b18f2400b08f"
    "0800e0033000bd27"
)
DIRECT_FILTER_ROOT = bytes.fromhex(
    "d0ffbd272c00bfaf2800b1af2400b0af258080000d00001200000000"
    "7600113c80db31267800053c84f2a524253020027858120c25200002"
    "04004010000000002520400018bf130c25282002010002242c00bf8f"
    "2800b18f2400b08f0800e0033000bd27"
)
DIRECT_FILTER_ROUTE = (
    "Audiobook".encode("utf-16le") + b"\x00\x00"
).ljust(len(AUDIOBOOK_LAUNCHER_ROUTE), b"\x00")


def current_launcher_prefix() -> bytes:
    return AUDIOBOOK_LAUNCHER_CODE[: len(DIRECT_FILTER_LAUNCHER)]


def current_root_prefix() -> bytes:
    return AUDIOBOOK_BOOK_OPEN_ROOT_CODE[: len(DIRECT_FILTER_ROOT)]


def route_addr() -> int:
    return file_offset_to_runtime_addr(
        AUDIOBOOK_LAUNCHER_CAVE_OFFSET + AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE
    )


def specs() -> list[tuple[str, int, bytes, bytes]]:
    return [
        (
            "audiobook_launcher_direct_filter",
            file_offset_to_runtime_addr(AUDIOBOOK_LAUNCHER_CAVE_OFFSET),
            current_launcher_prefix(),
            DIRECT_FILTER_LAUNCHER,
        ),
        (
            "audiobook_route_plain_filter",
            route_addr(),
            AUDIOBOOK_LAUNCHER_ROUTE,
            DIRECT_FILTER_ROUTE,
        ),
        (
            "audiobook_book_open_direct_filter",
            file_offset_to_runtime_addr(AUDIOBOOK_BOOK_OPEN_ROOT_OFFSET),
            current_root_prefix(),
            DIRECT_FILTER_ROOT,
        ),
    ]


def classify(actual: bytes, current: bytes, direct: bytes) -> str:
    if actual == current:
        return "current"
    if actual == direct:
        return "direct-filter"
    return "unknown"


def memory_state(adb: str, pid: str, remote_dir: str, local_dir: Path) -> str:
    states: list[str] = []
    for name, addr, current, direct in specs():
        actual = read_memory_bytes(adb, pid, addr, len(current), remote_dir, local_dir, name, "state")
        states.append(classify(actual, current, direct))
    if all(item == "current" for item in states):
        return "current"
    if all(item == "direct-filter" for item in states):
        return "direct-filter"
    return ",".join(states)


def verify(adb: str, pid: str, remote_dir: str, local_dir: Path, *, direct: bool) -> None:
    suffix = "direct" if direct else "current"
    for name, addr, current, direct_bytes in specs():
        expected = direct_bytes if direct else current
        actual = read_memory_bytes(adb, pid, addr, len(expected), remote_dir, local_dir, name, suffix)
        if actual != expected:
            raise RuntimeError(f"verification failed for {name} at 0x{addr:x}")


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
    local_dir = Path("work/runtime-patch-files") / f"direct-filter-test-{stamp}"
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    state = memory_state(args.adb, pid, remote_dir, local_dir)
    print(f"player pid: {pid}")
    print(f"direct-filter route state: {state}")
    if not args.apply and not args.revert:
        print("dry run only; no process memory was written")
        return 0

    if args.apply:
        if state == "direct-filter":
            print("direct-filter route already active")
            return 0
        if state != "current":
            raise RuntimeError(f"refusing to apply over mixed/unknown state: {state}")
        ptrace_write(
            args.adb,
            pid,
            remote_dir,
            local_dir,
            "direct_filter_apply",
            [(name, addr, direct) for name, addr, _current, direct in specs()],
        )
        verify(args.adb, pid, remote_dir, local_dir, direct=True)
        print("applied direct-filter route to running hiby_player")
        return 0

    if state == "current":
        print("current route already active")
        return 0
    if state != "direct-filter":
        raise RuntimeError(f"refusing to revert mixed/unknown state: {state}")
    ptrace_write(
        args.adb,
        pid,
        remote_dir,
        local_dir,
        "direct_filter_revert",
        [(name, addr, current) for name, addr, current, _direct in specs()],
    )
    verify(args.adb, pid, remote_dir, local_dir, direct=False)
    print("reverted running hiby_player to current route")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
