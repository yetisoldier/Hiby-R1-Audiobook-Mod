#!/usr/bin/env python3
"""RAM-only probes for HiBy R1 media route callbacks.

The stock media category table calls route callbacks through data pointers, so
static callsite searches do not show how the arguments are populated. This tool
temporarily hooks one callback at a time, records registers to writable scratch
memory, replays the overwritten prologue, and continues stock code. Rebooting
the R1 discards all changes.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".deps" / "python"))

from keystone import KS_ARCH_MIPS, KS_MODE_LITTLE_ENDIAN, KS_MODE_MIPS32, Ks  # noqa: E402

from adb_probe_music_row import (  # noqa: E402
    assert_probe_range_available,
    assert_writable_zero_range_available,
    dump_nested_words,
    dump_ptr,
    jump_patch,
    li,
    verify_known_player_binary,
)
from adb_runtime_patch_hiby_player import (  # noqa: E402
    DEFAULT_ADB,
    find_stock_player_pid,
    ptrace_write,
    read_memory_bytes,
    shell,
)


REMOTE_DIR = "/usr/data/codex_route_callback_probe"
PROBE_ADDR = 0x760708
SCRATCH_ADDR = 0x8E4800
SCRATCH_SIZE = 0x200


@dataclass(frozen=True)
class ProbeSpec:
    name: str
    hook_addr: int
    original: bytes
    continue_addr: int
    magic: int
    replay: str


PROBES: dict[str, ProbeSpec] = {
    "simple": ProbeSpec(
        name="simple",
        hook_addr=0x4F01C0,
        original=bytes.fromhex("c8fdbd272802b2af"),
        continue_addr=0x4F01C8,
        magic=0xC0DE4F01,
        replay="""
        addiu $sp, $sp, -0x238
        sw $s2, 0x228($sp)
        """,
    ),
    "chain": ProbeSpec(
        name="chain",
        hook_addr=0x4EFFC0,
        original=bytes.fromhex("b0fbbd273c04b4af"),
        continue_addr=0x4EFFC8,
        magic=0xC0DE4EFF,
        replay="""
        addiu $sp, $sp, -0x450
        sw $s4, 0x43c($sp)
        """,
    ),
}


def assemble(addr: int, text: str) -> bytes:
    return bytes(Ks(KS_ARCH_MIPS, KS_MODE_MIPS32 + KS_MODE_LITTLE_ENDIAN).asm(text, addr=addr)[0])


def build_probe(spec: ProbeSpec) -> bytes:
    probe = assemble(
        PROBE_ADDR,
        f"""
        {li('t0', SCRATCH_ADDR)}
        {li('t1', spec.magic)}
        sw $t1, 0($t0)
        sw $ra, 4($t0)
        sw $a0, 8($t0)
        sw $a1, 12($t0)
        sw $a2, 16($t0)
        sw $a3, 20($t0)
        sw $sp, 24($t0)
        sw $s0, 28($t0)
        sw $s1, 32($t0)
        sw $s2, 36($t0)
        sw $s3, 40($t0)
        sw $s4, 44($t0)
        sw $s5, 48($t0)
        sw $s6, 52($t0)
        sw $s7, 56($t0)
        sw $gp, 60($t0)
        lw $t2, 64($t0)
        addiu $t2, $t2, 1
        sw $t2, 64($t0)
        {spec.replay}
        j 0x{spec.continue_addr:x}
        nop
        """,
    )
    return probe + b"\x00" * ((len(probe) + 3) // 4 * 4 - len(probe))


def local_dir() -> Path:
    path = Path("work/route-callback-probe")
    path.mkdir(parents=True, exist_ok=True)
    return path


def read_hook(adb: str, pid: str, spec: ProbeSpec, label: str) -> bytes:
    return read_memory_bytes(adb, pid, spec.hook_addr, len(spec.original), REMOTE_DIR, local_dir(), label, "bin")


def armed_patch(spec: ProbeSpec) -> bytes:
    return jump_patch(spec.hook_addr, PROBE_ADDR)


def arm(adb: str, spec: ProbeSpec) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = local_dir()
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")
    patch = armed_patch(spec)
    actual = read_hook(adb, pid, spec, f"{spec.name}_pre")
    if actual == patch:
        print(f"{spec.name} route callback probe already armed")
        return
    if actual != spec.original:
        raise RuntimeError(
            f"{spec.name} callback is not stock/probeable at 0x{spec.hook_addr:x}: "
            f"expected {spec.original.hex()} or {patch.hex()}, got {actual.hex()}"
        )

    probe = build_probe(spec)
    assert_probe_range_available(adb, pid, PROBE_ADDR, len(probe), local, f"{spec.name}_probe_cave_pre")
    assert_writable_zero_range_available(
        adb,
        pid,
        SCRATCH_ADDR,
        SCRATCH_SIZE,
        local,
        f"{spec.name}_scratch_pre",
        stale_magic=spec.magic,
    )
    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        f"arm_route_callback_{spec.name}",
        [
            ("route_callback_scratch_clear", SCRATCH_ADDR, bytes(SCRATCH_SIZE)),
            ("route_callback_probe", PROBE_ADDR, probe),
            ("route_callback_jump", spec.hook_addr, patch),
        ],
    )
    print(f"armed {spec.name} route callback probe in pid {pid} at 0x{PROBE_ADDR:x}")


def restore(adb: str, spec: ProbeSpec) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = local_dir()
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")
    patch = armed_patch(spec)
    actual = read_hook(adb, pid, spec, f"{spec.name}_restore_pre")
    if actual == spec.original:
        print(f"{spec.name} route callback probe already restored")
        return
    if actual != patch:
        raise RuntimeError(
            f"{spec.name} callback is not in a known state at 0x{spec.hook_addr:x}: "
            f"expected {spec.original.hex()} or {patch.hex()}, got {actual.hex()}"
        )
    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        f"restore_route_callback_{spec.name}",
        [
            ("route_callback_restore", spec.hook_addr, spec.original),
            ("route_callback_probe_clear", PROBE_ADDR, bytes(0x180)),
        ],
    )
    print(f"restored {spec.name} route callback probe")


def read_probe(adb: str, *, compact: bool) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = local_dir()
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")
    data = read_memory_bytes(adb, pid, SCRATCH_ADDR, SCRATCH_SIZE, REMOTE_DIR, local, "route_callback_scratch", "bin")
    values = struct.unpack("<" + "I" * (len(data) // 4), data)
    labels = [
        "magic",
        "ra",
        "a0",
        "a1",
        "a2",
        "a3",
        "sp",
        "s0",
        "s1",
        "s2",
        "s3",
        "s4",
        "s5",
        "s6",
        "s7",
        "gp",
        "seq",
    ]
    for label, value in zip(labels, values):
        print(f"{label}=0x{value:08x} ({value})")
    if compact:
        return
    for label, value in zip(labels[2:16], values[2:16]):
        dump_ptr(adb, pid, local, label, value, 0x180)
    for label, value in zip(labels[2:16], values[2:16]):
        dump_nested_words(adb, pid, local, label, value, words=24)


def spec_from_arg(name: str) -> ProbeSpec:
    try:
        return PROBES[name]
    except KeyError as exc:
        raise RuntimeError(f"unknown probe {name!r}; choose one of {', '.join(PROBES)}") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("action", choices=["arm", "restore", "read"])
    parser.add_argument("--probe", choices=sorted(PROBES), default="chain")
    parser.add_argument("--compact", action="store_true", help="For read, print only saved registers.")
    args = parser.parse_args()

    spec = spec_from_arg(args.probe)
    if args.action == "arm":
        arm(args.adb, spec)
    elif args.action == "restore":
        restore(args.adb, spec)
    else:
        read_probe(args.adb, compact=args.compact)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
