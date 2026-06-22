#!/usr/bin/env python3
"""Static xref report for HiBy R1 ``hiby_player``.

This is a read-only helper for firmware research. It scans an extracted
``hiby_player`` ELF for direct MIPS callsites, keyword-matched strings, and
plain 32-bit pointer references. It is intentionally conservative; dynamic
runtime probes are still required before promoting any finding into a patch.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".deps" / "python"))

from capstone import CS_ARCH_MIPS, CS_MODE_32, CS_MODE_LITTLE_ENDIAN, Cs  # noqa: E402

from r1_hiby_player_cave_audit import (  # noqa: E402
    Section,
    Segment,
    parse_elf,
    section_for_offset,
    segment_for_offset,
)


DEFAULT_BINARY = Path(
    "work/audiobook-firmware-1.6.28-sd-ready-dev/"
    "squashfs-root/usr/bin/hiby_player"
)

DEFAULT_TARGETS = {
    "shared_listview_handler": 0x490BE0,
    "shared_media_open": 0x49E200,
    "album_genre_open": 0x49FE40,
    "route_callback_chain": 0x4EFFC0,
    "route_callback_simple": 0x4F01C0,
    "music_selection_handler": 0x4A1004,
    "books_text_open_callsite": 0x540B0C,
    "audiobook_launcher_cave": 0x75DAEC,
}

DEFAULT_KEYWORDS = (
    "vg_listview",
    "songs_of_an_album",
    "songs_of_an_album_and_a_genre",
    "artist",
    "album",
    "genre",
    "search",
    "Audiobook",
    "vg_listview_book",
    "bookmark",
    "music",
    "playlist",
    "usb_working_mode",
)


@dataclass(frozen=True)
class Instruction:
    address: int
    offset: int
    word: int
    mnemonic: str
    op_str: str


@dataclass(frozen=True)
class StringHit:
    offset: int
    vaddr: int
    kind: str
    text: str
    section: str


@dataclass(frozen=True)
class ListviewHandler:
    vaddr: int
    name: str
    handler: int


@dataclass(frozen=True)
class RouteRecord:
    vaddr: int
    key: str
    label: str
    listview: str
    child: str
    next_child: str
    callback: int


def safe_text(value: str, *, limit: int = 160) -> str:
    clipped = value[:limit]
    escaped = clipped.encode("unicode_escape", errors="replace").decode("ascii", errors="replace")
    if len(value) > limit:
        escaped += "..."
    return escaped


def offset_for_vaddr(segments: list[Segment], vaddr: int) -> int | None:
    segment = next((item for item in segments if item.contains_vaddr(vaddr)), None)
    if segment is None:
        return None
    return segment.vaddr_to_offset(vaddr)


def string_at(data: bytes, segments: list[Segment], vaddr: int) -> str:
    offset = offset_for_vaddr(segments, vaddr)
    if offset is None or offset < 0 or offset >= len(data):
        return ""

    end = data.find(b"\x00", offset)
    if end >= 0:
        raw = data[offset:end]
        if raw and all(32 <= byte < 127 for byte in raw):
            return raw.decode("ascii", errors="replace")

    chars: list[str] = []
    index = offset
    while index + 1 < len(data):
        lo = data[index]
        hi = data[index + 1]
        if hi == 0 and 32 <= lo < 127:
            chars.append(chr(lo))
            index += 2
            continue
        break
    return "".join(chars)


def code_addr(segments: list[Segment], sections: list[Section], vaddr: int) -> bool:
    offset = offset_for_vaddr(segments, vaddr)
    if offset is None:
        return False
    section = section_for_offset(sections, offset)
    return bool(section and section.executable)


def parse_vaddr_range(value: str) -> tuple[int, int]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("range must be START:STOP")
    start_raw, stop_raw = value.split(":", 1)
    start = int(start_raw, 0)
    stop = int(stop_raw, 0)
    if stop <= start:
        raise argparse.ArgumentTypeError("range STOP must be greater than START")
    return start, stop


def executable_ranges(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    *,
    extra_vaddr_ranges: list[tuple[int, int]] | None = None,
) -> list[tuple[int, int, int]]:
    ranges: list[tuple[int, int, int]] = []
    for section in sections:
        if not section.executable or section.size <= 0:
            continue
        segment = segment_for_offset(segments, section.offset)
        if segment is None or not segment.executable:
            continue
        start = max(0, section.offset)
        end = min(len(data), section.end)
        if end > start:
            ranges.append((start, end, section.addr - section.offset))
    for start_vaddr, stop_vaddr in extra_vaddr_ranges or []:
        start_offset = offset_for_vaddr(segments, start_vaddr)
        stop_offset = offset_for_vaddr(segments, stop_vaddr - 1)
        if start_offset is None or stop_offset is None:
            continue
        start = max(0, start_offset)
        end = min(len(data), stop_offset + 1)
        if end > start:
            ranges.append((start, end, start_vaddr - start_offset))
    if ranges:
        return ranges

    for segment in segments:
        if not segment.executable:
            continue
        start = max(0, segment.offset)
        end = min(len(data), segment.file_end)
        if end > start:
            ranges.append((start, end, segment.vaddr - segment.offset))
    return ranges


def disassemble(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    *,
    extra_vaddr_ranges: list[tuple[int, int]] | None = None,
) -> list[Instruction]:
    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32_LITTLE())
    md.skipdata = True
    out: list[Instruction] = []
    for start, end, delta in executable_ranges(
        data,
        segments,
        sections,
        extra_vaddr_ranges=extra_vaddr_ranges,
    ):
        code = data[start:end]
        for insn in md.disasm(code, start + delta):
            if insn.size != 4:
                continue
            offset = insn.address - delta
            word = struct.unpack_from("<I", data, offset)[0]
            out.append(
                Instruction(
                    address=insn.address,
                    offset=offset,
                    word=word,
                    mnemonic=insn.mnemonic,
                    op_str=insn.op_str,
                )
            )
    out.sort(key=lambda item: item.address)
    return out


def CS_MODE_MIPS32_LITTLE() -> int:
    return CS_MODE_32 + CS_MODE_LITTLE_ENDIAN


def mips_jal_target(insn: Instruction) -> int | None:
    opcode = (insn.word >> 26) & 0x3F
    if opcode != 0x03:
        return None
    index = insn.word & 0x03FFFFFF
    return ((insn.address + 4) & 0xF0000000) | (index << 2)


def collect_calls(instructions: list[Instruction]) -> dict[int, list[Instruction]]:
    calls: dict[int, list[Instruction]] = {}
    for insn in instructions:
        target = mips_jal_target(insn)
        if target is None:
            continue
        calls.setdefault(target, []).append(insn)
    return calls


def context_for(instructions: list[Instruction], address: int, before: int, after: int) -> list[Instruction]:
    index = next((i for i, insn in enumerate(instructions) if insn.address == address), None)
    if index is None:
        return []
    return instructions[max(0, index - before) : min(len(instructions), index + after + 1)]


def read_c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\x00", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("utf-8", errors="replace")


def collect_ascii_strings(data: bytes, min_len: int) -> list[tuple[int, str]]:
    hits: list[tuple[int, str]] = []
    index = 0
    while index < len(data):
        if 32 <= data[index] < 127:
            start = index
            while index < len(data) and 32 <= data[index] < 127:
                index += 1
            if index - start >= min_len:
                hits.append((start, read_c_string(data, start)))
        else:
            index += 1
    return hits


def collect_utf16le_strings(data: bytes, min_len: int) -> list[tuple[int, str]]:
    hits: list[tuple[int, str]] = []
    index = 0
    while index < len(data) - 1:
        start = index
        chars: list[str] = []
        while index + 1 < len(data):
            lo = data[index]
            hi = data[index + 1]
            if hi == 0 and 32 <= lo < 127:
                chars.append(chr(lo))
                index += 2
            else:
                break
        if len(chars) >= min_len:
            hits.append((start, "".join(chars)))
            continue
        index = start + 2
    return hits


def string_vaddr(segments: list[Segment], offset: int) -> int | None:
    segment = segment_for_offset(segments, offset)
    if segment is None:
        return None
    return segment.offset_to_vaddr(offset)


def keyword_regex(keywords: list[str]) -> re.Pattern[str]:
    escaped = [re.escape(item) for item in keywords if item]
    if not escaped:
        return re.compile(r"$^")
    return re.compile("|".join(escaped), re.IGNORECASE)


def collect_string_hits(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    keywords: list[str],
    *,
    min_len: int,
) -> list[StringHit]:
    pattern = keyword_regex(keywords)
    hits: dict[tuple[int, str], StringHit] = {}
    for kind, source in (
        ("ascii", collect_ascii_strings(data, min_len)),
        ("utf16le", collect_utf16le_strings(data, min_len)),
    ):
        for offset, text in source:
            if not pattern.search(text):
                continue
            vaddr = string_vaddr(segments, offset)
            if vaddr is None:
                continue
            section = section_for_offset(sections, offset)
            hits[(offset, kind)] = StringHit(
                offset=offset,
                vaddr=vaddr,
                kind=kind,
                text=text,
                section=section.name if section else "<no section>",
            )
    return sorted(hits.values(), key=lambda item: (item.vaddr, item.kind))


def iter_words(data: bytes, segments: list[Segment]) -> list[tuple[int, int, int]]:
    words: list[tuple[int, int, int]] = []
    for segment in segments:
        start = max(0, segment.offset)
        end = min(len(data), segment.file_end)
        for offset in range(start, end - 3, 4):
            words.append((offset, segment.offset_to_vaddr(offset), struct.unpack_from("<I", data, offset)[0]))
    return words


def collect_listview_handlers(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    *,
    limit: int,
) -> list[ListviewHandler]:
    out: list[ListviewHandler] = []
    for offset, vaddr, value in iter_words(data, segments):
        name = string_at(data, segments, value)
        if not name.startswith("vg_listview_"):
            continue
        if offset + 8 > len(data):
            continue
        handler = struct.unpack_from("<I", data, offset + 4)[0]
        if not code_addr(segments, sections, handler):
            continue
        out.append(ListviewHandler(vaddr=vaddr, name=name, handler=handler))
        if len(out) >= limit:
            break
    return out


def collect_route_records(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    *,
    limit: int,
) -> list[RouteRecord]:
    out: list[RouteRecord] = []
    seen: set[int] = set()
    for offset, vaddr, _value in iter_words(data, segments):
        if offset + 24 > len(data):
            continue
        values = struct.unpack_from("<IIIIII", data, offset)
        listview = string_at(data, segments, values[2])
        if not listview.startswith("vg_listview_"):
            continue
        child = string_at(data, segments, values[3])
        next_child = string_at(data, segments, values[4])
        if not (child.startswith("vg_listview_") or next_child.startswith("vg_listview_")):
            continue
        key = string_at(data, segments, values[0])
        label = string_at(data, segments, values[1])
        if not code_addr(segments, sections, values[5]):
            continue
        if not key or not label or vaddr in seen:
            continue
        seen.add(vaddr)
        out.append(
            RouteRecord(
                vaddr=vaddr,
                key=key,
                label=label,
                listview=listview,
                child=child,
                next_child=next_child,
                callback=values[5],
            )
        )
        if len(out) >= limit:
            break
    return out


def pointer_refs(data: bytes, segments: list[Segment], target: int, *, limit: int) -> list[tuple[int, int, str]]:
    needle = struct.pack("<I", target & 0xFFFFFFFF)
    refs: list[tuple[int, int, str]] = []
    start = 0
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            break
        if offset % 4 == 0:
            vaddr = string_vaddr(segments, offset)
            if vaddr is not None:
                segment = segment_for_offset(segments, offset)
                refs.append((offset, vaddr, "X" if segment and segment.executable else "-"))
                if len(refs) >= limit:
                    break
        start = offset + 1
    return refs


def immediate_refs(instructions: list[Instruction], target: int, *, window: int = 6) -> list[Instruction]:
    hi = ((target + 0x8000) >> 16) & 0xFFFF
    lo = target & 0xFFFF
    refs: list[Instruction] = []
    lui_candidates: list[tuple[int, str]] = []
    reg_pattern = re.compile(r"\$(?:[a-z][a-z0-9]*|\d+)")

    for index, insn in enumerate(instructions):
        if insn.mnemonic != "lui":
            continue
        if f"0x{hi:x}" not in insn.op_str.lower():
            continue
        regs = reg_pattern.findall(insn.op_str)
        if not regs:
            continue
        lui_candidates.append((index, regs[0]))

    for index, reg in lui_candidates:
        for next_insn in instructions[index + 1 : min(len(instructions), index + 1 + window)]:
            if next_insn.mnemonic not in {"addiu", "ori", "lw", "sw"}:
                continue
            if reg not in next_insn.op_str:
                continue
            lower = next_insn.op_str.lower()
            if f"0x{lo:x}" in lower or f"{lo if lo < 0x8000 else lo - 0x10000}" in lower:
                refs.append(instructions[index])
                break
    return refs


def format_instruction(insn: Instruction, marker: str = " ") -> str:
    return f"{marker}0x{insn.address:08x}  {insn.mnemonic:<8} {insn.op_str}".rstrip()


def parse_target(value: str) -> tuple[str, int]:
    if "=" in value:
        name, raw_addr = value.split("=", 1)
        return name.strip() or raw_addr.strip(), int(raw_addr, 0)
    addr = int(value, 0)
    return f"target_0x{addr:x}", addr


def build_report(args: argparse.Namespace) -> str:
    data = args.binary.read_bytes()
    segments, sections = parse_elf(data)
    instructions = disassemble(data, segments, sections, extra_vaddr_ranges=args.extra_range)
    calls = collect_calls(instructions)

    targets = dict(DEFAULT_TARGETS)
    for item in args.target or []:
        name, addr = parse_target(item)
        targets[name] = addr

    keywords = list(DEFAULT_KEYWORDS)
    keywords.extend(args.keyword or [])
    strings = collect_string_hits(
        data,
        segments,
        sections,
        keywords,
        min_len=args.min_string_len,
    )
    listview_handlers = collect_listview_handlers(data, segments, sections, limit=args.max_listviews)
    route_records = collect_route_records(data, segments, sections, limit=args.max_routes)

    lines: list[str] = []
    lines.append("# HiBy Player Static Xref Report")
    lines.append("")
    lines.append(f"- Binary: `{args.binary}`")
    lines.append(f"- Size: `0x{len(data):x}` bytes")
    lines.append(f"- Instructions decoded: `{len(instructions)}`")
    if args.extra_range:
        rendered_ranges = ", ".join(f"0x{start:08x}:0x{stop:08x}" for start, stop in args.extra_range)
        lines.append(f"- Extra disassembly ranges: `{rendered_ranges}`")
    lines.append("")

    lines.append("## Direct Calls")
    lines.append("")
    for name, addr in sorted(targets.items(), key=lambda item: item[1]):
        callsites = calls.get(addr, [])
        lines.append(f"### `{name}` `0x{addr:08x}`")
        lines.append("")
        if not callsites:
            lines.append("- No direct `jal` callsites found.")
            lines.append("")
            continue
        for call in callsites[: args.max_calls]:
            lines.append(f"- Callsite `0x{call.address:08x}` file `0x{call.offset:06x}`")
            lines.append("")
            lines.append("```text")
            for ctx in context_for(instructions, call.address, args.context, args.context):
                lines.append(format_instruction(ctx, ">" if ctx.address == call.address else " "))
            lines.append("```")
            lines.append("")
        if len(callsites) > args.max_calls:
            lines.append(f"- ... {len(callsites) - args.max_calls} more callsites omitted.")
            lines.append("")

    lines.append("## Keyword Strings")
    lines.append("")
    if not strings:
        lines.append("- No keyword strings found.")
    for hit in strings[: args.max_strings]:
        lines.append(
            f"- `0x{hit.vaddr:08x}` file `0x{hit.offset:06x}` {hit.kind} "
            f"`{hit.section}`: `{safe_text(hit.text)}`"
        )
        ptrs = pointer_refs(data, segments, hit.vaddr, limit=args.max_refs)
        imm_refs = immediate_refs(instructions, hit.vaddr)[: args.max_refs]
        if ptrs:
            ptr_text = ", ".join(f"`0x{vaddr:08x}`" for _offset, vaddr, _exec in ptrs)
            lines.append(f"  pointer refs: {ptr_text}")
        if imm_refs:
            imm_text = ", ".join(f"`0x{insn.address:08x}`" for insn in imm_refs)
            lines.append(f"  immediate refs: {imm_text}")
    if len(strings) > args.max_strings:
        lines.append(f"- ... {len(strings) - args.max_strings} more strings omitted.")
    lines.append("")

    lines.append("## Listview Handler Registry")
    lines.append("")
    if not listview_handlers:
        lines.append("- No listview handler pairs found.")
    for item in listview_handlers:
        lines.append(f"- `0x{item.vaddr:08x}` `{item.name}` -> handler `0x{item.handler:08x}`")
    lines.append("")

    lines.append("## Route/Listview Records")
    lines.append("")
    if not route_records:
        lines.append("- No route/listview records found.")
    for item in route_records:
        child_text = item.child or "-"
        next_child_text = item.next_child or "-"
        callback_text = (
            f"`0x{item.callback:08x}`"
            if code_addr(segments, sections, item.callback)
            else f"`0x{item.callback:08x}`?"
        )
        lines.append(
            f"- `0x{item.vaddr:08x}` key `{safe_text(item.key, limit=48)}` "
            f"label `{safe_text(item.label, limit=48)}` view `{item.listview}` "
            f"child `{child_text}` next `{next_child_text}` callback {callback_text}"
        )
    lines.append("")

    if args.extra_range:
        lines.append("## Extra Disassembly Ranges")
        lines.append("")
        for start, stop in args.extra_range:
            lines.append(f"### `0x{start:08x}:0x{stop:08x}`")
            lines.append("")
            lines.append("```text")
            for insn in instructions:
                if start <= insn.address < stop:
                    lines.append(format_instruction(insn, " "))
            lines.append("```")
            lines.append("")

    lines.append("## Notes")
    lines.append("")
    lines.append("- Direct `jal` calls are reliable static xrefs.")
    lines.append(
        "- Pointer/immediate references are hints only; the player is partly PIC and "
        "some runtime objects are built dynamically."
    )
    lines.append(
        "- Promote findings through RAM-only ADB probes before changing firmware."
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--target", action="append", help="Add target as name=0xADDR or 0xADDR.")
    parser.add_argument("--keyword", action="append", help="Add string keyword.")
    parser.add_argument("--min-string-len", type=int, default=4)
    parser.add_argument("--max-calls", type=int, default=12)
    parser.add_argument("--max-strings", type=int, default=120)
    parser.add_argument("--max-refs", type=int, default=8)
    parser.add_argument("--max-listviews", type=int, default=80)
    parser.add_argument("--max-routes", type=int, default=80)
    parser.add_argument("--context", type=int, default=4)
    parser.add_argument(
        "--extra-range",
        action="append",
        type=parse_vaddr_range,
        default=[],
        metavar="START:STOP",
        help="Also disassemble a virtual-address range, useful for patched code caves.",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    if not args.binary.exists():
        raise SystemExit(f"binary not found: {args.binary}")

    report = build_report(args)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
        print(f"wrote: {args.output}")
    else:
        print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
