#!/usr/bin/env python3
"""RAM-only probes and markers for selected HiBy R1 music/book rows.

The music probe is deliberately non-playing: it records registers and returns
through the normal success epilogue. The album marker preserves the stock album
open path while recording a small marker the resume daemon can consume. A reboot
clears all changes.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".deps" / "python"))

from keystone import KS_ARCH_MIPS, KS_MODE_LITTLE_ENDIAN, KS_MODE_MIPS32, Ks  # noqa: E402

from adb_runtime_patch_hiby_player import (  # noqa: E402
    DEFAULT_ADB,
    find_stock_player_pid,
    ptrace_write,
    read_memory_bytes,
    shell,
    verify_stock_binary,
)


REMOTE_DIR = "/usr/data/codex_row_probe"
SCRATCH = 0x8B1F00
PROBE = 0x75DCEC
ALBUM_SCRATCH = 0x8E4000
ALBUM_PROBE = 0x75DE00

# Normal file/list selection dispatch in stock 1.6. We intercept a few
# instructions before the playback call and then jump to the handler's normal
# success epilogue, avoiding playback and post-play UI side effects.
MUSIC_PROBE_JUMP = 0x4A1004
MUSIC_PROBE_ORIGINAL = bytes.fromhex("2538200225308002")
MUSIC_SUCCESS_EPILOGUE = 0x4A1030

# Shared Genre -> Album list opener. The caller has already resolved the album
# title pointer in $a1; this hook records it, then executes the original prologue
# and continues stock navigation.
ALBUM_OPEN_MARKER_JUMP = 0x49FE40
ALBUM_OPEN_MARKER_ORIGINAL = bytes.fromhex("c8fdbd272c02b2af")
ALBUM_OPEN_MARKER_CONTINUE = 0x49FE48


def assemble(addr: int, text: str) -> bytes:
    return bytes(Ks(KS_ARCH_MIPS, KS_MODE_MIPS32 + KS_MODE_LITTLE_ENDIAN).asm(text, addr=addr)[0])


def li(reg: str, value: int) -> str:
    value &= 0xFFFFFFFF
    return f"lui ${reg}, 0x{(value >> 16) & 0xffff:x}\nori ${reg}, ${reg}, 0x{value & 0xffff:x}"


def arm_music(adb: str) -> None:
    verify_stock_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    actual = read_memory_bytes(adb, pid, MUSIC_PROBE_JUMP, len(MUSIC_PROBE_ORIGINAL), REMOTE_DIR, local, "music_probe_pre", "bin")
    if actual != MUSIC_PROBE_ORIGINAL:
        raise RuntimeError(
            f"music selection handler is not stock at 0x{MUSIC_PROBE_JUMP:x}: "
            f"expected {MUSIC_PROBE_ORIGINAL.hex()}, got {actual.hex()}"
        )

    probe = assemble(
        PROBE,
        f"""
        {li('t0', SCRATCH)}
        {li('t1', 0xC0DE4A10)}
        sw $t1, 0($t0)
        sw $ra, 4($t0)
        sw $s0, 8($t0)
        sw $s1, 12($t0)
        sw $s2, 16($t0)
        sw $s3, 20($t0)
        sw $s4, 24($t0)
        sw $a0, 28($t0)
        sw $a1, 32($t0)
        sw $a2, 36($t0)
        sw $a3, 40($t0)
        sw $sp, 44($t0)
        j 0x{MUSIC_SUCCESS_EPILOGUE:x}
        nop
        """,
    )
    probe = probe + b"\x00" * ((len(probe) + 3) // 4 * 4 - len(probe))
    call_patch = assemble(MUSIC_PROBE_JUMP, f"j 0x{PROBE:x}")
    if len(call_patch) != len(MUSIC_PROBE_ORIGINAL):
        raise RuntimeError(f"unexpected jump patch length: {len(call_patch)}")

    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        "arm_music_row",
        [
            ("scratch_clear", SCRATCH, bytes(0x100)),
            ("music_row_probe", PROBE, probe),
            ("music_probe_jump", MUSIC_PROBE_JUMP, call_patch),
        ],
    )
    verify = read_memory_bytes(adb, pid, MUSIC_PROBE_JUMP, len(call_patch), REMOTE_DIR, local, "music_probe_post", "bin")
    print(f"armed music row probe in pid {pid}; handler jump now {verify.hex()}")


def arm_album(adb: str) -> None:
    verify_stock_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    call_patch = assemble(ALBUM_OPEN_MARKER_JUMP, f"j 0x{ALBUM_PROBE:x}")
    if len(call_patch) != len(ALBUM_OPEN_MARKER_ORIGINAL):
        raise RuntimeError(f"unexpected album jump patch length: {len(call_patch)}")

    actual = read_memory_bytes(
        adb,
        pid,
        ALBUM_OPEN_MARKER_JUMP,
        len(ALBUM_OPEN_MARKER_ORIGINAL),
        REMOTE_DIR,
        local,
        "album_probe_pre",
        "bin",
    )
    if actual not in (ALBUM_OPEN_MARKER_ORIGINAL, call_patch):
        raise RuntimeError(
            f"album opener is not stock at 0x{ALBUM_OPEN_MARKER_JUMP:x}: "
            f"expected {ALBUM_OPEN_MARKER_ORIGINAL.hex()} or {call_patch.hex()}, got {actual.hex()}"
        )

    probe = assemble(
        ALBUM_PROBE,
        f"""
        {li('t0', ALBUM_SCRATCH)}
        {li('t1', 0xC0DE4A17)}
        sw $t1, 0($t0)
        sw $ra, 4($t0)
        sw $a0, 8($t0)
        sw $a1, 12($t0)
        sw $a2, 16($t0)
        sw $a3, 20($t0)
        sw $a1, 24($t0)
        sw $s0, 28($t0)
        sw $s1, 32($t0)
        sw $s2, 36($t0)
        sw $s3, 40($t0)
        sw $s4, 44($t0)
        sw $s5, 48($t0)
        sw $s6, 52($t0)
        sw $s7, 56($t0)
        sw $sp, 60($t0)
        sw $fp, 64($t0)
        lw $t6, 68($t0)
        addiu $t6, $t6, 1
        sw $t6, 68($t0)
        addiu $sp, $sp, -0x238
        sw $s2, 0x22c($sp)
        j 0x{ALBUM_OPEN_MARKER_CONTINUE:x}
        """,
    )
    probe = probe + b"\x00" * ((len(probe) + 3) // 4 * 4 - len(probe))

    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        "arm_album_row",
        [
            ("album_scratch_clear", ALBUM_SCRATCH, bytes(0x100)),
            ("album_row_probe", ALBUM_PROBE, probe),
            ("album_probe_jump", ALBUM_OPEN_MARKER_JUMP, call_patch),
        ],
    )
    verify = read_memory_bytes(
        adb,
        pid,
        ALBUM_OPEN_MARKER_JUMP,
        len(call_patch),
        REMOTE_DIR,
        local,
        "album_probe_post",
        "bin",
    )
    print(f"armed album open marker in pid {pid}; opener jump now {verify.hex()}")


def read_utf16(data: bytes) -> str:
    try:
        end = data.find(b"\x00\x00")
        if end >= 0:
            end += end % 2
            data = data[:end]
        return data.decode("utf-16le", errors="replace")
    except Exception:
        return ""


def safe_text(text: str) -> str:
    return text.encode("unicode_escape", errors="replace").decode("ascii", errors="replace")


def dump_ptr(adb: str, pid: str, local: Path, label: str, addr: int, size: int = 0x120) -> None:
    if not (0x1000 <= addr < 0x80000000):
        return
    try:
        data = read_memory_bytes(adb, pid, addr, size, REMOTE_DIR, local, f"{label}_{addr:x}", "bin")
    except Exception as exc:
        print(f"{label}@0x{addr:08x}: read failed: {exc}")
        return
    words = struct.unpack("<" + "I" * (min(len(data), 0x80) // 4), data[: min(len(data), 0x80)])
    ascii_prefix = data.split(b"\x00", 1)[0][:120]
    ascii_text = "".join(chr(b) if 32 <= b < 127 else "." for b in ascii_prefix)
    print(f"\n{label}@0x{addr:08x}")
    print("words:", " ".join(f"{w:08x}" for w in words))
    if ascii_text:
        print("ascii:", ascii_text)
    utf16 = read_utf16(data)
    if utf16:
        print("utf16:", safe_text(repr(utf16[:120])))


def read_probe(adb: str) -> None:
    verify_stock_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    data = read_memory_bytes(adb, pid, SCRATCH, 0x100, REMOTE_DIR, local, "scratch", "bin")
    vals = struct.unpack("<" + "I" * (len(data) // 4), data)
    labels = ["magic", "ra", "s0", "s1", "s2", "s3", "s4", "a0", "a1", "a2", "a3", "sp"]
    for label, value in zip(labels, vals):
        print(f"{label}=0x{value:08x} ({value})")
    for label, value in zip(labels[2:], vals[2:]):
        dump_ptr(adb, pid, local, label, value)


def read_album_probe(adb: str) -> None:
    verify_stock_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    data = read_memory_bytes(adb, pid, ALBUM_SCRATCH, 0x100, REMOTE_DIR, local, "album_scratch", "bin")
    vals = struct.unpack("<" + "I" * (len(data) // 4), data)
    labels = [
        "magic",
        "ra",
        "a0",
        "a1_album",
        "a2",
        "a3",
        "album_ptr",
        "s0",
        "s1",
        "s2",
        "s3",
        "s4",
        "s5",
        "s6",
        "s7",
        "sp",
        "fp",
        "seq",
    ]
    for label, value in zip(labels, vals):
        print(f"{label}=0x{value:08x} ({value})")
    for label, value in zip(labels[2:], vals[2:]):
        dump_ptr(adb, pid, local, label, value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("action", choices=["arm-music", "arm-album", "read", "read-album"])
    args = parser.parse_args()

    if args.action == "arm-music":
        arm_music(args.adb)
    elif args.action == "arm-album":
        arm_album(args.adb)
    elif args.action == "read-album":
        read_album_probe(args.adb)
    else:
        read_probe(args.adb)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
