#!/usr/bin/env python3
"""RAM-only patch for the native Audiobooks hub view rows.

This applies the same native-hub-view-row patch set used by the dev firmware,
but only to the running stock ``/usr/bin/hiby_player`` process. Rebooting the
device discards the changes.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from adb_runtime_patch_hiby_player import (  # noqa: E402
    DEFAULT_ADB,
    describe_live_launcher_state,
    file_offset_to_runtime_addr,
    find_live_launcher_callbacks,
    find_stock_player_pid,
    patch_live_launcher_callbacks,
    ptrace_write,
    read_memory_bytes,
    shell,
    verify_known_binary,
)
from patch_hiby_player import (  # noqa: E402
    AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES,
    AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_PATCHES,
)


REMOTE_DIR_BASE = "/usr/data/codex_native_hub_view_rows"
LOCAL_DIR_BASE = Path("work/runtime-patch-files/native-hub-view-rows")
EXTRA_KNOWN_PLAYER_MD5S = {
    # Current extracted stock/player baseline used in this workspace.
    "cd4d2812ab3425174b52925766424d2b",
    # Flashed native-hub-view-rows audiobook build before bookmark-open fix.
    "0321d7669c0525e9740faac926ecd583",
}


def verify_player_binary(adb: str) -> str:
    try:
        return verify_known_binary(adb)
    except RuntimeError as exc:
        digest = shell(adb, "md5sum /usr/bin/hiby_player").strip().split()[0].lower()
        if digest in EXTRA_KNOWN_PLAYER_MD5S:
            return digest
        raise exc


def specs() -> list[tuple[str, int, bytes, bytes]]:
    patch_map = [
        ("launcher_cave", AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES[0]),
        ("launcher_title_key", AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES[1]),
        ("launcher_title_lui", AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES[2]),
        ("launcher_title_addiu", AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES[3]),
        ("launcher_callback", AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES[4]),
    ]
    for index, patch in enumerate(AUDIOBOOK_NATIVE_HUB_VIEW_ROWS_PATCHES):
        patch_map.append((f"view_rows_{index:02d}", patch))
    return [(name, offset, old, new) for name, (offset, old, new) in patch_map]


def read_state(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    patch_specs: list[tuple[str, int, bytes, bytes]],
    *,
    scan_heap_callbacks: bool,
) -> tuple[str, str]:
    states: list[str] = []
    for name, offset, old, new in patch_specs:
        actual = read_memory_bytes(
            adb,
            pid,
            file_offset_to_runtime_addr(offset),
            len(old),
            remote_dir,
            local_dir,
            name,
            "state",
        )
        if actual == old:
            states.append("stock")
        elif actual == new:
            states.append("patched")
        else:
            states.append(f"mixed:{name}")
            break
    callback_summary = "not-scanned"
    if scan_heap_callbacks:
        callbacks = find_live_launcher_callbacks(adb, pid, remote_dir, local_dir)
        callback_summary = describe_live_launcher_state(callbacks)
    return ("patched" if all(state == "patched" for state in states) else ",".join(states), callback_summary)


def writes_from_specs(
    patch_specs: list[tuple[str, int, bytes, bytes]],
    *,
    patched: bool,
) -> list[tuple[str, int, bytes]]:
    writes: list[tuple[str, int, bytes]] = []
    for name, offset, old, new in patch_specs:
        writes.append((name, file_offset_to_runtime_addr(offset), new if patched else old))
    return writes


def aligned_writes(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    writes: list[tuple[str, int, bytes]],
) -> list[tuple[str, int, bytes]]:
    aligned: list[tuple[str, int, bytes]] = []
    for name, addr, data in writes:
        start = addr & ~0x3
        end = (addr + len(data) + 3) & ~0x3
        current = read_memory_bytes(
            adb,
            pid,
            start,
            end - start,
            remote_dir,
            local_dir,
            f"{name}_align",
            "state",
        )
        merged = bytearray(current)
        offset = addr - start
        merged[offset : offset + len(data)] = data
        aligned.append((name, start, bytes(merged)))
    return aligned


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--apply", action="store_true", help="Apply the native hub view-row patch set.")
    parser.add_argument("--revert", action="store_true", help="Revert the native hub view-row patch set.")
    parser.add_argument("--scan-heap-callbacks", action="store_true")
    parser.add_argument("--patch-heap-callbacks", action="store_true")
    parser.add_argument(
        "--i-understand-this-writes-process-memory",
        action="store_true",
        help="Required with --apply or --revert. This only touches RAM.",
    )
    args = parser.parse_args()

    if args.apply and args.revert:
        parser.error("choose only one of --apply or --revert")
    if (args.apply or args.revert) and not args.i_understand_this_writes_process_memory:
        parser.error("memory writes require --i-understand-this-writes-process-memory")
    if args.patch_heap_callbacks:
        args.scan_heap_callbacks = True

    verify_player_binary(args.adb)
    pid = find_stock_player_pid(args.adb)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    remote_dir = f"{REMOTE_DIR_BASE}_{stamp}"
    local_dir = LOCAL_DIR_BASE / stamp
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    patch_specs = specs()
    before_static, before_heap = read_state(
        args.adb,
        pid,
        remote_dir,
        local_dir,
        patch_specs,
        scan_heap_callbacks=args.scan_heap_callbacks,
    )
    print(f"player pid: {pid}")
    print(f"before: static={before_static} heap_callbacks={before_heap}")

    if not args.apply and not args.revert:
        print("dry run only; no process memory was written")
        return 0

    ptrace_write(
        args.adb,
        pid,
        remote_dir,
        local_dir,
        "native_hub_view_rows",
        aligned_writes(
            args.adb,
            pid,
            remote_dir,
            local_dir,
            writes_from_specs(patch_specs, patched=args.apply),
        ),
    )
    if args.patch_heap_callbacks:
        patch_live_launcher_callbacks(
            args.adb,
            pid,
            remote_dir,
            local_dir,
            patched=args.revert is False,
        )
    after_static, after_heap = read_state(
        args.adb,
        pid,
        remote_dir,
        local_dir,
        patch_specs,
        scan_heap_callbacks=args.scan_heap_callbacks,
    )
    print(f"after:  static={after_static} heap_callbacks={after_heap}")
    print(f"remote dir: {remote_dir}")
    print(f"local dir: {local_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
