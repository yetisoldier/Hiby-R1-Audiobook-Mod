#!/usr/bin/env python3
"""Audit executable code-cave candidates in the R1 ``hiby_player`` ELF.

The R1 patches use runtime virtual addresses that generally map to ELF file
offset + 0x400000. This tool reads the ELF headers directly so we can stop
guessing which zero-filled regions are executable before trying RAM-only probes
on the device.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


PT_LOAD = 1
PF_X = 1
PF_W = 2
PF_R = 4
SHF_EXECINSTR = 0x4


@dataclass(frozen=True)
class Segment:
    index: int
    offset: int
    vaddr: int
    filesz: int
    memsz: int
    flags: int
    align: int

    @property
    def file_end(self) -> int:
        return self.offset + self.filesz

    @property
    def vaddr_end(self) -> int:
        return self.vaddr + self.memsz

    @property
    def executable(self) -> bool:
        return bool(self.flags & PF_X)

    def contains_offset(self, offset: int) -> bool:
        return self.offset <= offset < self.file_end

    def contains_vaddr(self, vaddr: int) -> bool:
        return self.vaddr <= vaddr < self.vaddr_end

    def offset_to_vaddr(self, offset: int) -> int:
        return self.vaddr + (offset - self.offset)

    def vaddr_to_offset(self, vaddr: int) -> int:
        return self.offset + (vaddr - self.vaddr)


@dataclass(frozen=True)
class Section:
    name: str
    offset: int
    addr: int
    size: int
    flags: int

    @property
    def end(self) -> int:
        return self.offset + self.size

    @property
    def executable(self) -> bool:
        return bool(self.flags & SHF_EXECINSTR)

    def contains_offset(self, offset: int) -> bool:
        return self.offset <= offset < self.end


@dataclass(frozen=True)
class Cave:
    offset: int
    vaddr: int
    size: int
    segment: Segment
    section: Section | None


KNOWN_AREAS = (
    ("audiobook launcher cave", "offset", 0x35DAEC, 0xD4),
    ("audiobook book-open helper", "offset", 0x35DBC0, 0x64),
    ("audiobook title marker cave", "offset", 0x35DE00, 0x54),
    ("old play-open probe cave, known bad", "vaddr", 0x75DF00, 0x100),
    ("music row probe RAM cave", "vaddr", 0x75DCEC, 0x80),
    ("album marker RAM cave", "vaddr", 0x75DE00, 0x80),
    ("shared media-open function", "vaddr", 0x49E200, 0x20),
    ("music selection handler", "vaddr", 0x4A1004, 0x20),
)


def read_c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\x00", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("ascii", errors="replace")


def flag_text(flags: int) -> str:
    return "".join(
        (
            "R" if flags & PF_R else "-",
            "W" if flags & PF_W else "-",
            "X" if flags & PF_X else "-",
        )
    )


def section_flag_text(flags: int) -> str:
    return "X" if flags & SHF_EXECINSTR else "-"


def unpack_from(fmt: str, data: bytes, offset: int) -> tuple[int, ...]:
    return struct.unpack_from(fmt, data, offset)


def parse_elf(data: bytes) -> tuple[list[Segment], list[Section]]:
    if data[:4] != b"\x7fELF":
        raise SystemExit("Input is not an ELF file")
    if data[4] != 1:
        raise SystemExit("Only ELF32 is supported")
    if data[5] == 1:
        endian = "<"
    elif data[5] == 2:
        endian = ">"
    else:
        raise SystemExit("Unknown ELF data encoding")

    (
        _e_type,
        _e_machine,
        _e_version,
        _e_entry,
        e_phoff,
        e_shoff,
        _e_flags,
        _e_ehsize,
        e_phentsize,
        e_phnum,
        e_shentsize,
        e_shnum,
        e_shstrndx,
    ) = unpack_from(endian + "HHIIIIIHHHHHH", data, 0x10)

    segments: list[Segment] = []
    for index in range(e_phnum):
        off = e_phoff + index * e_phentsize
        (
            p_type,
            p_offset,
            p_vaddr,
            _p_paddr,
            p_filesz,
            p_memsz,
            p_flags,
            p_align,
        ) = unpack_from(endian + "IIIIIIII", data, off)
        if p_type == PT_LOAD:
            segments.append(
                Segment(
                    index=index,
                    offset=p_offset,
                    vaddr=p_vaddr,
                    filesz=p_filesz,
                    memsz=p_memsz,
                    flags=p_flags,
                    align=p_align,
                )
            )

    raw_sections: list[tuple[int, int, int, int, int]] = []
    for index in range(e_shnum):
        off = e_shoff + index * e_shentsize
        (
            sh_name,
            _sh_type,
            sh_flags,
            sh_addr,
            sh_offset,
            sh_size,
            _sh_link,
            _sh_info,
            _sh_addralign,
            _sh_entsize,
        ) = unpack_from(endian + "IIIIIIIIII", data, off)
        raw_sections.append((sh_name, sh_flags, sh_addr, sh_offset, sh_size))

    shstr = b""
    if 0 <= e_shstrndx < len(raw_sections):
        _, _, _, shstr_offset, shstr_size = raw_sections[e_shstrndx]
        shstr = data[shstr_offset : shstr_offset + shstr_size]

    sections: list[Section] = []
    for sh_name, sh_flags, sh_addr, sh_offset, sh_size in raw_sections:
        name = read_c_string(shstr, sh_name) if shstr and sh_name < len(shstr) else ""
        sections.append(
            Section(
                name=name or "<unnamed>",
                offset=sh_offset,
                addr=sh_addr,
                size=sh_size,
                flags=sh_flags,
            )
        )

    return segments, sections


def segment_for_offset(segments: list[Segment], offset: int) -> Segment | None:
    return next((segment for segment in segments if segment.contains_offset(offset)), None)


def segment_for_vaddr(segments: list[Segment], vaddr: int) -> Segment | None:
    return next((segment for segment in segments if segment.contains_vaddr(vaddr)), None)


def section_for_offset(sections: list[Section], offset: int) -> Section | None:
    candidates = [section for section in sections if section.contains_offset(offset)]
    if not candidates:
        return None
    return min(candidates, key=lambda section: section.size)


def align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    return (value + alignment - 1) // alignment * alignment


def find_zero_caves(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    *,
    min_size: int,
    alignment: int,
) -> list[Cave]:
    caves: list[Cave] = []
    for segment in segments:
        if not segment.executable:
            continue
        start = max(segment.offset, 0)
        end = min(segment.file_end, len(data))
        index = start
        while index < end:
            if data[index] != 0:
                index += 1
                continue
            run_start = index
            while index < end and data[index] == 0:
                index += 1
            run_end = index
            aligned = align_up(run_start, alignment)
            if run_end - aligned >= min_size:
                caves.append(
                    Cave(
                        offset=aligned,
                        vaddr=segment.offset_to_vaddr(aligned),
                        size=run_end - aligned,
                        segment=segment,
                        section=section_for_offset(sections, aligned),
                    )
                )
    caves.sort(key=lambda cave: (cave.offset, -cave.size))
    return caves


def describe_known_area(
    data: bytes,
    segments: list[Segment],
    sections: list[Section],
    label: str,
    kind: str,
    value: int,
    size: int,
) -> str:
    if kind == "offset":
        offset = value
        segment = segment_for_offset(segments, offset)
        vaddr = segment.offset_to_vaddr(offset) if segment else None
    else:
        vaddr = value
        segment = segment_for_vaddr(segments, vaddr)
        offset = segment.vaddr_to_offset(vaddr) if segment else None

    if segment is None or offset is None or vaddr is None:
        return f"- {label}: {kind}=0x{value:x} is not in a file-backed LOAD segment"

    section = section_for_offset(sections, offset)
    block = data[offset : min(offset + size, len(data))]
    zero_prefix = 0
    for byte in block:
        if byte != 0:
            break
        zero_prefix += 1
    all_zero = len(block) == size and zero_prefix == size
    status = "all zero" if all_zero else f"zero prefix {zero_prefix}/{size}"
    section_text = section.name if section else "<no section>"
    section_exec = section_flag_text(section.flags) if section else "-"
    return (
        f"- {label}: file 0x{offset:06x}, vaddr 0x{vaddr:08x}, size 0x{size:x}, "
        f"seg {flag_text(segment.flags)}, section {section_text}({section_exec}), {status}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Find executable zero-filled code caves in an extracted R1 hiby_player binary."
    )
    parser.add_argument("--binary", type=Path, required=True, help="Path to extracted usr/bin/hiby_player")
    parser.add_argument("--min-size", type=int, default=128, help="Minimum aligned zero-run size to report")
    parser.add_argument("--alignment", type=int, default=4, help="Required file/vaddr alignment")
    parser.add_argument("--limit", type=int, default=32, help="Maximum cave candidates to print")
    args = parser.parse_args()

    data = args.binary.read_bytes()
    segments, sections = parse_elf(data)
    caves = find_zero_caves(
        data,
        segments,
        sections,
        min_size=args.min_size,
        alignment=args.alignment,
    )

    print(f"Binary: {args.binary}")
    print(f"Size:   0x{len(data):x} bytes")
    print()
    print("LOAD segments:")
    for segment in segments:
        print(
            f"- #{segment.index}: file 0x{segment.offset:06x}-0x{segment.file_end:06x}, "
            f"vaddr 0x{segment.vaddr:08x}-0x{segment.vaddr_end:08x}, "
            f"flags {flag_text(segment.flags)}, align 0x{segment.align:x}"
        )

    print()
    print("Known areas:")
    for label, kind, value, size in KNOWN_AREAS:
        print(describe_known_area(data, segments, sections, label, kind, value, size))

    print()
    print(f"Executable zero-filled candidates >= {args.min_size} bytes:")
    if not caves:
        print("- none found")
        return 0
    for cave in caves[: args.limit]:
        section = cave.section.name if cave.section else "<no section>"
        section_exec = section_flag_text(cave.section.flags) if cave.section else "-"
        print(
            f"- file 0x{cave.offset:06x}, vaddr 0x{cave.vaddr:08x}, "
            f"size 0x{cave.size:x}, seg {flag_text(cave.segment.flags)}, "
            f"section {section}({section_exec})"
        )
    if len(caves) > args.limit:
        print(f"- ... {len(caves) - args.limit} more omitted by --limit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
