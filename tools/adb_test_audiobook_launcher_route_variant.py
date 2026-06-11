#!/usr/bin/env python3
"""RAM-only test for alternate Audiobooks launcher routes.

The installed Audiobooks launcher currently opens the stock route
``genre\\Audiobook``. This helper edits only the route/selected strings in the
running ``hiby_player`` code cave so author/title route experiments can be
tested without flashing firmware. Rebooting the R1 discards the change.
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
    shell,
)
from patch_hiby_player import (  # noqa: E402
    AUDIOBOOK_LAUNCHER_CAVE_OFFSET,
    AUDIOBOOK_LAUNCHER_CODE,
    AUDIOBOOK_LAUNCHER_ROUTE,
    AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE,
    AUDIOBOOK_LAUNCHER_SELECTED_GENRE,
)


REMOTE_DIR_BASE = "/usr/data/codex_route_variant"
ROUTE_SIZE = len(AUDIOBOOK_LAUNCHER_ROUTE)
SELECTED_SIZE = len(AUDIOBOOK_LAUNCHER_SELECTED_GENRE)

ROUTE_PRESETS = {
    "title": ("genre\\Audiobook", "Audiobook"),
    "artist": ("artist\\", ""),
    "artist-all": ("artist_all\\", ""),
    "album": ("album\\", ""),
    "genre": ("genre\\", ""),
    "genre-all": ("genre_all\\", ""),
}


def encode_route(text: str, size: int, label: str) -> bytes:
    raw = text.encode("utf-16le") + b"\x00\x00"
    if len(raw) > size:
        raise ValueError(f"{label} is too long for the launcher field: {text!r}")
    return raw.ljust(size, b"\x00")


def route_addr() -> int:
    return file_offset_to_runtime_addr(
        AUDIOBOOK_LAUNCHER_CAVE_OFFSET + AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE
    )


def selected_addr() -> int:
    return route_addr() + ROUTE_SIZE


def code_addr() -> int:
    return file_offset_to_runtime_addr(AUDIOBOOK_LAUNCHER_CAVE_OFFSET)


def decode_utf16_field(data: bytes) -> str:
    for i in range(0, len(data) - 1, 2):
        if data[i : i + 2] == b"\x00\x00":
            return data[:i].decode("utf-16le", errors="replace")
    return data.decode("utf-16le", errors="replace").rstrip("\x00")


def read_state(adb: str, pid: str, remote_dir: str, local_dir: Path) -> tuple[str, str]:
    code = read_memory_bytes(
        adb, pid, code_addr(), len(AUDIOBOOK_LAUNCHER_CODE), remote_dir, local_dir, "code", "state"
    )
    if code != AUDIOBOOK_LAUNCHER_CODE:
        raise RuntimeError(
            "The Audiobooks launcher code cave is not active in the running player. "
            "Install the custom firmware or apply the audiobook-launcher runtime patch first."
        )
    route = read_memory_bytes(
        adb, pid, route_addr(), ROUTE_SIZE, remote_dir, local_dir, "route", "state"
    )
    selected = read_memory_bytes(
        adb, pid, selected_addr(), SELECTED_SIZE, remote_dir, local_dir, "selected", "state"
    )
    return decode_utf16_field(route), decode_utf16_field(selected)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument(
        "--preset",
        choices=sorted(ROUTE_PRESETS),
        help="Route preset to test. 'title' restores the normal Audiobooks title route.",
    )
    parser.add_argument("--route", help="Custom UTF-16 route string, such as 'artist\\'.")
    parser.add_argument("--selected", default=None, help="Optional selected-argument string.")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument(
        "--i-understand-this-writes-process-memory",
        action="store_true",
        help="Required with --apply. This is RAM-only and reboot-reversible.",
    )
    args = parser.parse_args()

    if args.apply and not args.i_understand_this_writes_process_memory:
        parser.error("--apply requires --i-understand-this-writes-process-memory")
    if args.preset and args.route:
        parser.error("choose either --preset or --route, not both")

    if args.preset:
        route_text, selected_text = ROUTE_PRESETS[args.preset]
    elif args.route:
        route_text = args.route
        selected_text = ""
    else:
        route_text, selected_text = ROUTE_PRESETS["title"]
    if args.selected is not None:
        selected_text = args.selected

    route_bytes = encode_route(route_text, ROUTE_SIZE, "route")
    selected_bytes = encode_route(selected_text, SELECTED_SIZE, "selected argument")

    pid = find_stock_player_pid(args.adb)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    remote_dir = f"{REMOTE_DIR_BASE}_{stamp}"
    local_dir = Path("work/runtime-patch-files") / f"route-variant-{stamp}"
    shell(args.adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)

    current_route, current_selected = read_state(args.adb, pid, remote_dir, local_dir)
    print(f"player pid: {pid}")
    print(f"current route:   {current_route!r}")
    print(f"current selected:{current_selected!r}")
    print(f"target route:    {route_text!r}")
    print(f"target selected: {selected_text!r}")
    if not args.apply:
        print("dry run only; no process memory was written")
        return 0

    ptrace_write(
        args.adb,
        pid,
        remote_dir,
        local_dir,
        "route_variant",
        [
            ("audiobook_route", route_addr(), route_bytes),
            ("audiobook_selected", selected_addr(), selected_bytes),
        ],
    )
    new_route, new_selected = read_state(args.adb, pid, remote_dir, local_dir)
    if (new_route, new_selected) != (route_text, selected_text):
        raise RuntimeError(
            f"Route verification failed: got {(new_route, new_selected)!r}, "
            f"expected {(route_text, selected_text)!r}"
        )
    print("applied route variant to running hiby_player")
    print("Tap Audiobooks on the R1 to test it. Rebooting restores the flashed route.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
