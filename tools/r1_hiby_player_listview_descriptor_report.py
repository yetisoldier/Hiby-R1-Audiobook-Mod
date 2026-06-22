#!/usr/bin/env python3
"""Report HiBy R1 native listview descriptors from ``hiby_player``.

The R1 UI is heavily table driven.  Each listview descriptor stores the layout
path, native view name, row generator, select handler, and several shared input
callbacks.  This read-only helper makes those table entries explicit so deeper
UI patches can target native screens instead of relying on framebuffer/UI
workarounds.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from r1_hiby_player_static_xrefs import (  # noqa: E402
    offset_for_vaddr,
    parse_elf,
    string_at,
)


DEFAULT_BINARY = Path("work/rootfs/usr/bin/hiby_player")

LAYOUT_PREFIX = b"z:\\layout\\theme1\\listview\\"
LAYOUT_SUFFIX = b".listview"

CALLBACK_OFFSETS = {
    -0x08: "generator",
    -0x04: "init/common",
    0x40: "event/back",
    0x44: "event/key",
    0x48: "select/tap",
    0x4C: "event/focus",
    0x50: "aux0",
    0x54: "event/scroll",
    0x58: "aux1",
    0x5C: "aux2",
    0x60: "touch/gesture",
    0x64: "draw/update",
}

COMMON_FUNCTIONS = {
    0x0049EA60: "common_back",
    0x0049EC40: "common_key",
    0x0049EEE0: "common_focus",
    0x0049F040: "common_scroll",
    0x0049F7E0: "common_init",
    0x0049F960: "common_touch",
    0x0049FC80: "common_draw",
    0x00540B60: "book_hub_generator",
    0x00540CA0: "book_hub_select",
    0x005408A0: "book_list_generator",
    0x00540A80: "book_list_select",
    0x00540A40: "book_recent_generator",
    0x00540A60: "book_collect_generator",
    0x00540F00: "book_recent_select",
    0x00540EE0: "book_collect_select",
    0x0053FAE0: "txt_book_generator",
    0x0053FCA0: "txt_book_select",
    0x0053FD20: "txt_book_back",
    0x0053FF80: "txt_book_key",
    0x00540180: "txt_book_close",
}


@dataclass(frozen=True)
class Descriptor:
    path_vaddr: int
    view_vaddr: int
    path: str
    view: str
    callbacks: dict[int, int]


def is_ascii_printable(raw: bytes) -> bool:
    return bool(raw) and all(32 <= byte < 127 for byte in raw)


def read_c_ascii(data: bytes, offset: int, *, limit: int = 256) -> str:
    end = data.find(b"\x00", offset, min(len(data), offset + limit))
    if end < 0:
        return ""
    raw = data[offset:end]
    if not is_ascii_printable(raw):
        return ""
    return raw.decode("ascii", errors="replace")


def vaddr_for_offset(segments, offset: int) -> int | None:
    for segment in segments:
        if segment.contains_offset(offset):
            return segment.offset_to_vaddr(offset)
    return None


def read_u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        return 0
    return struct.unpack_from("<I", data, offset)[0]


def collect_descriptors(data: bytes, segments) -> list[Descriptor]:
    descriptors: list[Descriptor] = []
    start = 0
    while True:
        path_offset = data.find(LAYOUT_PREFIX, start)
        if path_offset < 0:
            break
        start = path_offset + 1
        path = read_c_ascii(data, path_offset)
        if not path.endswith(LAYOUT_SUFFIX.decode("ascii")):
            continue
        path_vaddr = vaddr_for_offset(segments, path_offset)
        if path_vaddr is None:
            continue

        view_offset = path_offset + 0x10C
        view = read_c_ascii(data, view_offset, limit=96)
        if not view.startswith("vg_"):
            continue
        view_vaddr = vaddr_for_offset(segments, view_offset)
        if view_vaddr is None:
            continue

        callbacks = {
            rel: read_u32(data, view_offset + rel)
            for rel in CALLBACK_OFFSETS
        }
        descriptors.append(Descriptor(path_vaddr, view_vaddr, path, view, callbacks))

    return sorted(descriptors, key=lambda item: item.view)


def describe_pointer(data: bytes, segments, ptr: int) -> str:
    if ptr == 0:
        return ""
    label = COMMON_FUNCTIONS.get(ptr, "")
    text = string_at(data, segments, ptr)
    parts = [f"`0x{ptr:08x}`"]
    if label:
        parts.append(label)
    elif text:
        parts.append(f"`{text}`")
    return " ".join(parts)


def build_report(binary: Path, *, name_filter: str) -> str:
    data = binary.read_bytes()
    segments, _sections = parse_elf(data)
    descriptors = collect_descriptors(data, segments)
    if name_filter:
        needle = name_filter.lower()
        descriptors = [
            item for item in descriptors
            if needle in item.view.lower() or needle in item.path.lower()
        ]

    lines: list[str] = []
    lines.append("# HiBy Player Listview Descriptor Report")
    lines.append("")
    lines.append(f"- Binary: `{binary}`")
    lines.append(f"- Descriptors: `{len(descriptors)}`")
    if name_filter:
        lines.append(f"- Filter: `{name_filter}`")
    lines.append("")

    for item in descriptors:
        lines.append(f"## `{item.view}`")
        lines.append("")
        lines.append(f"- Layout: `{item.path}`")
        lines.append(f"- Path vaddr: `0x{item.path_vaddr:08x}`")
        lines.append(f"- View-name vaddr: `0x{item.view_vaddr:08x}`")
        lines.append("")
        lines.append("| Slot | Relative | Target |")
        lines.append("|---|---:|---|")
        for rel, slot_name in CALLBACK_OFFSETS.items():
            ptr = item.callbacks.get(rel, 0)
            target = describe_pointer(data, segments, ptr)
            if not target:
                target = "`0x00000000`"
            lines.append(f"| {slot_name} | `{rel:+#04x}` | {target} |")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--filter", default="")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    report = build_report(args.binary, name_filter=args.filter)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
        print(f"wrote: {args.output}")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
