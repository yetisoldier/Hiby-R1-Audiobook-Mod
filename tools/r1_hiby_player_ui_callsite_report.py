#!/usr/bin/env python3
"""Report native HiBy R1 UI creation/open callsites.

This helper is intentionally static and read-only.  It scans a ``hiby_player``
ELF for calls to the native listview/subpage creator and prints the nearby
constant string arguments when they can be recovered with simple MIPS register
tracking.  The output is meant to guide RAM-only probes before any firmware
patch is promoted.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from r1_hiby_player_static_xrefs import (  # noqa: E402
    Instruction,
    collect_calls,
    context_for,
    disassemble,
    mips_jal_target,
    offset_for_vaddr,
    parse_elf,
    parse_vaddr_range,
)


DEFAULT_BINARY = Path("work/rootfs/usr/bin/hiby_player")
DEFAULT_TARGET = 0x4961E0
DEFAULT_TARGETS = [DEFAULT_TARGET]

REG_RE = re.compile(r"\$(?:[a-z][a-z0-9]*|\d+)")
IMM_RE = re.compile(r"(-?0x[0-9a-f]+|-?\d+)", re.IGNORECASE)


@dataclass(frozen=True)
class RegValue:
    value: int
    source: str


def parse_regs(op_str: str) -> list[str]:
    return REG_RE.findall(op_str)


def parse_imm(op_str: str) -> int | None:
    matches = IMM_RE.findall(op_str)
    if not matches:
        return None
    raw = matches[-1]
    return int(raw, 0)


def normalize_u32(value: int) -> int:
    return value & 0xFFFFFFFF


def update_reg_values(values: dict[str, RegValue], insn: Instruction) -> None:
    regs = parse_regs(insn.op_str)
    if insn.mnemonic == "lui" and len(regs) >= 1:
        imm = parse_imm(insn.op_str)
        if imm is not None:
            values[regs[0]] = RegValue(normalize_u32(imm << 16), f"lui at 0x{insn.address:x}")
        return

    if insn.mnemonic in {"addiu", "ori"} and len(regs) >= 2:
        dest, src = regs[0], regs[1]
        imm = parse_imm(insn.op_str)
        if imm is None or src not in values:
            values.pop(dest, None)
            return
        base = values[src].value
        if insn.mnemonic == "addiu":
            values[dest] = RegValue(normalize_u32(base + imm), f"{insn.mnemonic} at 0x{insn.address:x}")
        else:
            values[dest] = RegValue(normalize_u32(base | imm), f"{insn.mnemonic} at 0x{insn.address:x}")
        return

    if insn.mnemonic == "move" and len(regs) >= 2:
        dest, src = regs[0], regs[1]
        if src in values:
            values[dest] = RegValue(values[src].value, f"move at 0x{insn.address:x}")
        else:
            values.pop(dest, None)
        return

    # Loads, arithmetic, and calls usually make the register unknown for this
    # simple constant tracker.
    if insn.mnemonic in {"lw", "lb", "lbu", "lh", "lhu", "jal", "jalr"} and regs:
        values.pop(regs[0], None)


def values_before_call(context: list[Instruction], call_addr: int) -> dict[str, RegValue]:
    values: dict[str, RegValue] = {}
    for insn in context:
        if insn.address == call_addr:
            continue
        if insn.address > call_addr + 4:
            break
        update_reg_values(values, insn)
    return values


def best_string_at(data: bytes, segments: list, vaddr: int) -> str:
    offset = offset_for_vaddr(segments, vaddr)
    if offset is None or offset < 0 or offset >= len(data):
        return ""

    utf16_chars: list[str] = []
    index = offset
    while index + 1 < len(data):
        lo = data[index]
        hi = data[index + 1]
        if lo == 0 and hi == 0:
            break
        if hi != 0 or lo < 32 or lo >= 127:
            utf16_chars = []
            break
        utf16_chars.append(chr(lo))
        index += 2
    if len(utf16_chars) >= 2:
        return "".join(utf16_chars)

    end = data.find(b"\x00", offset)
    if end >= 0:
        raw = data[offset:end]
        if len(raw) >= 2 and all(32 <= byte < 127 for byte in raw):
            return raw.decode("ascii", errors="replace")
    return ""


def render_arg(data: bytes, segments: list, reg_values: dict[str, RegValue], reg: str) -> str:
    item = reg_values.get(reg)
    if item is None:
        return "?"
    text = best_string_at(data, segments, item.value)
    if text:
        return f"0x{item.value:08x} `{text}`"
    offset = offset_for_vaddr(segments, item.value)
    if offset is None:
        return f"0x{item.value:08x}"
    return f"0x{item.value:08x} file 0x{offset:x}"


def build_report(
    binary: Path,
    target: int,
    *,
    before: int,
    after: int,
    extra_ranges: list[tuple[int, int]] | None = None,
) -> str:
    data = binary.read_bytes()
    segments, sections = parse_elf(data)
    instructions = disassemble(data, segments, sections, extra_vaddr_ranges=extra_ranges)
    calls = collect_calls(instructions).get(target, [])

    lines: list[str] = []
    lines.append("# HiBy Player UI Callsite Report")
    lines.append("")
    lines.append(f"- Binary: `{binary}`")
    lines.append(f"- Target: `0x{target:08x}`")
    lines.append(f"- Calls found: `{len(calls)}`")
    if extra_ranges:
        rendered = ", ".join(f"0x{start:08x}:0x{stop:08x}" for start, stop in extra_ranges)
        lines.append(f"- Extra disassembly ranges: `{rendered}`")
    lines.append("")

    for call in calls:
        ctx = context_for(instructions, call.address, before, after)
        values = values_before_call(ctx, call.address)
        lines.append(f"## Call `0x{call.address:08x}`")
        lines.append("")
        lines.append(f"- `$a0`: {render_arg(data, segments, values, '$a0')}")
        lines.append(f"- `$a1`: {render_arg(data, segments, values, '$a1')}")
        lines.append(f"- `$a2`: {render_arg(data, segments, values, '$a2')}")
        lines.append("")
        lines.append("```text")
        for insn in ctx:
            target_addr = mips_jal_target(insn)
            marker = ">" if insn.address == call.address else " "
            suffix = f" -> 0x{target_addr:08x}" if target_addr is not None else ""
            lines.append(f"{marker}0x{insn.address:08x}  {insn.mnemonic:<8} {insn.op_str}{suffix}")
        lines.append("```")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--target", type=lambda value: int(value, 0), action="append")
    parser.add_argument("--before", type=int, default=12)
    parser.add_argument("--after", type=int, default=3)
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

    reports = [
        build_report(
            args.binary,
            target,
            before=args.before,
            after=args.after,
            extra_ranges=args.extra_range,
        )
        for target in (args.target or DEFAULT_TARGETS)
    ]
    report = "\n".join(part.rstrip() for part in reports) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
        print(f"wrote: {args.output}")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
