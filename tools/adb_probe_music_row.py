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
    parse_memory_maps,
    ptrace_write,
    read_memory_bytes,
    shell,
)
from patch_hiby_player import STOCK_MD5  # noqa: E402


REMOTE_DIR = "/usr/data/codex_row_probe"
SCRATCH = 0x8E4600
PROBE = 0x75DCEC
ALBUM_SCRATCH = 0x8E4000
ALBUM_PROBE = 0x75DE00
PLAY_OPEN_SCRATCH = 0x8E4400
# 0x75df00 looked tempting during early tracing, but live testing showed it can
# reboot the R1. It is in/near the rodata mapping used by the player, so keep it
# documented as a known-bad cave and refuse to arm that tracer until a safer
# executable scratch area is found.
KNOWN_BAD_PLAY_OPEN_PROBES = {0x75DF00}
KNOWN_AUDIOBOOK_PLAYER_MD5S = {
    # Early audiobook development package.
    "09997a636c94112ff76c85a6d4a8d0ff",
    # Public 1.6.15-audiobook release.
    "dac7b58717097ef2a75ae5887478ef16",
    # 1.6.28-sd-ready-dev is the current dev build used for route/listview work.
    "c161af12bd050aca6f3fc2f67979d792",
    # 1.6.16.8-private-route-dev, used only for route diagnostics.
    "c168c57b3f22e8bfc4ee5fccb1b9455a",
}

# Normal file/list selection dispatch in stock 1.6. We intercept a few
# instructions before the playback call and then jump to the handler's normal
# success epilogue, avoiding playback and post-play UI side effects.
MUSIC_PROBE_JUMP = 0x4A1004
MUSIC_PROBE_ORIGINAL = bytes.fromhex("2538200225308002")
MUSIC_SUCCESS_EPILOGUE = 0x4A1030

# Shared media-open path used by several list routes. The probe records the
# incoming arguments, replays the overwritten prologue, then continues stock.
PLAY_OPEN_JUMP = 0x49E200
PLAY_OPEN_ORIGINAL = bytes.fromhex("20f3bd27d40cb7af")
PLAY_OPEN_CONTINUE = 0x49E208

# Shared Genre -> Album list opener. The caller has already resolved the album
# title pointer in $a1; this hook records it, then executes the original prologue
# and continues stock navigation.
ALBUM_OPEN_MARKER_JUMP = 0x49FE40
ALBUM_OPEN_MARKER_ORIGINAL = bytes.fromhex("c8fdbd272c02b2af")
ALBUM_OPEN_MARKER_CONTINUE = 0x49FE48

RESERVED_PROBE_RANGES = (
    # Flashed audiobook launcher helper and strings.
    (0x75DAEC, 0x75DC80, "audiobook launcher cave"),
    # Flashed title marker helper.
    (0x75DE00, 0x75DE80, "audiobook title marker cave"),
    # Runtime music row probe.
    (PROBE, PROBE + 0x100, "music row probe cave"),
    # Runtime album marker probe. This overlaps the flashed title marker cave
    # on current development builds.
    (ALBUM_PROBE, ALBUM_PROBE + 0x100, "album marker probe cave"),
)


def assemble(addr: int, text: str) -> bytes:
    return bytes(Ks(KS_ARCH_MIPS, KS_MODE_MIPS32 + KS_MODE_LITTLE_ENDIAN).asm(text, addr=addr)[0])


def verify_known_player_binary(adb: str) -> None:
    out = shell(adb, "md5sum /usr/bin/hiby_player").strip()
    digest = out.split()[0].lower()
    if digest not in {STOCK_MD5, *KNOWN_AUDIOBOOK_PLAYER_MD5S}:
        raise RuntimeError(
            "/usr/bin/hiby_player is not a known stock/audiobook 1.6 binary: "
            f"{digest}"
        )


def li(reg: str, value: int) -> str:
    value &= 0xFFFFFFFF
    return f"lui ${reg}, 0x{(value >> 16) & 0xffff:x}\nori ${reg}, ${reg}, 0x{value & 0xffff:x}"


def jump_patch(from_addr: int, to_addr: int) -> bytes:
    patch = assemble(from_addr, f"j 0x{to_addr:x}")
    if len(patch) != 8:
        raise RuntimeError(f"unexpected jump patch length at 0x{from_addr:x}: {len(patch)}")
    return patch


def assert_probe_range_available(
    adb: str,
    pid: str,
    addr: int,
    size: int,
    local: Path,
    label: str,
) -> None:
    if addr in KNOWN_BAD_PLAY_OPEN_PROBES:
        raise RuntimeError(
            f"0x{addr:x} is a known-bad play-open probe address from live testing. "
            "Run tools/r1_hiby_player_cave_audit.py and pass a different executable cave."
        )
    for start, end, reason in RESERVED_PROBE_RANGES:
        if addr < end and addr + size > start:
            raise RuntimeError(
                f"probe range 0x{addr:x}-0x{addr + size:x} overlaps {reason} "
                f"(0x{start:x}-0x{end:x})"
            )

    mappings = parse_memory_maps(adb, pid)
    mapping = next((item for item in mappings if item.start <= addr and addr + size <= item.end), None)
    if mapping is None:
        raise RuntimeError(f"probe range 0x{addr:x}-0x{addr + size:x} is not inside one memory mapping")
    if "x" not in mapping.perms:
        raise RuntimeError(
            f"probe range 0x{addr:x}-0x{addr + size:x} is in non-executable mapping "
            f"{mapping.perms} {mapping.path}"
        )
    if "hiby_player" not in mapping.path:
        raise RuntimeError(
            f"probe range 0x{addr:x}-0x{addr + size:x} is executable but not in hiby_player: "
            f"{mapping.perms} {mapping.path}"
        )

    existing = read_memory_bytes(adb, pid, addr, size, REMOTE_DIR, local, label, "bin")
    if any(existing):
        nonzero = next(index for index, value in enumerate(existing) if value)
        raise RuntimeError(
            f"probe range 0x{addr:x}-0x{addr + size:x} is not empty; "
            f"first non-zero byte at +0x{nonzero:x}"
        )


def assert_writable_zero_range_available(
    adb: str,
    pid: str,
    addr: int,
    size: int,
    local: Path,
    label: str,
    *,
    stale_magic: int | None = None,
) -> None:
    mappings = parse_memory_maps(adb, pid)
    mapping = next((item for item in mappings if item.start <= addr and addr + size <= item.end), None)
    if mapping is None:
        raise RuntimeError(f"scratch range 0x{addr:x}-0x{addr + size:x} is not inside one memory mapping")
    if "w" not in mapping.perms:
        raise RuntimeError(
            f"scratch range 0x{addr:x}-0x{addr + size:x} is not writable: "
            f"{mapping.perms} {mapping.path}"
        )
    existing = read_memory_bytes(adb, pid, addr, size, REMOTE_DIR, local, label, "bin")
    if any(existing):
        found_magic = struct.unpack("<I", existing[:4])[0] if len(existing) >= 4 else None
        if stale_magic is not None and found_magic == stale_magic:
            return
        nonzero = next(index for index, value in enumerate(existing) if value)
        raise RuntimeError(
            f"scratch range 0x{addr:x}-0x{addr + size:x} is not empty; "
            f"first non-zero byte at +0x{nonzero:x}. Refusing to clear live player data."
        )


def arm_music(adb: str) -> None:
    verify_known_player_binary(adb)
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
    call_patch = jump_patch(MUSIC_PROBE_JUMP, PROBE)
    assert_writable_zero_range_available(
        adb,
        pid,
        SCRATCH,
        0x100,
        local,
        "music_probe_scratch_pre",
        stale_magic=0xC0DE4A10,
    )

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


def restore_music(adb: str) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")
    call_patch = jump_patch(MUSIC_PROBE_JUMP, PROBE)
    actual = read_memory_bytes(adb, pid, MUSIC_PROBE_JUMP, len(call_patch), REMOTE_DIR, local, "music_probe_restore_pre", "bin")
    if actual == MUSIC_PROBE_ORIGINAL:
        print("music row probe already restored")
        return
    if actual != call_patch:
        raise RuntimeError(
            f"music row probe is not in a known state at 0x{MUSIC_PROBE_JUMP:x}: "
            f"expected {call_patch.hex()} or {MUSIC_PROBE_ORIGINAL.hex()}, got {actual.hex()}"
        )
    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        "restore_music_row",
        [("music_probe_restore", MUSIC_PROBE_JUMP, MUSIC_PROBE_ORIGINAL)],
    )
    print("restored music row probe")


def arm_album(adb: str) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    call_patch = jump_patch(ALBUM_OPEN_MARKER_JUMP, ALBUM_PROBE)

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
    assert_writable_zero_range_available(
        adb,
        pid,
        ALBUM_SCRATCH,
        0x100,
        local,
        "album_probe_scratch_pre",
        stale_magic=0xC0DE4A17,
    )

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


def arm_play_open(adb: str, probe_addr: int, *, override_row_index: int | None = None) -> None:
    if override_row_index is not None and not (0 <= override_row_index <= 9999):
        raise RuntimeError(f"override row index is out of range: {override_row_index}")
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    call_patch = jump_patch(PLAY_OPEN_JUMP, probe_addr)
    actual = read_memory_bytes(adb, pid, PLAY_OPEN_JUMP, len(PLAY_OPEN_ORIGINAL), REMOTE_DIR, local, "play_open_pre", "bin")
    if actual == call_patch:
        print("play-open probe already armed")
        return
    if actual != PLAY_OPEN_ORIGINAL:
        raise RuntimeError(
            f"play-open function is not stock/probeable at 0x{PLAY_OPEN_JUMP:x}: "
            f"expected {PLAY_OPEN_ORIGINAL.hex()} or {call_patch.hex()}, got {actual.hex()}"
        )

    override_text = ""
    if override_row_index is not None:
        override_text = f"""
        {li('t3', override_row_index)}
        sw $t3, 68($t0)
        move $a2, $t3
        """

    probe = assemble(
        probe_addr,
        f"""
        {li('t0', PLAY_OPEN_SCRATCH)}
        {li('t1', 0xC0DE49E2)}
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
        {override_text}
        addiu $sp, $sp, -3296
        sw $s7, 3284($sp)
        j 0x{PLAY_OPEN_CONTINUE:x}
        nop
        """,
    )
    probe = probe + b"\x00" * ((len(probe) + 3) // 4 * 4 - len(probe))
    assert_probe_range_available(adb, pid, probe_addr, len(probe), local, "play_open_probe_cave_pre")
    assert_writable_zero_range_available(
        adb,
        pid,
        PLAY_OPEN_SCRATCH,
        0x120,
        local,
        "play_open_scratch_pre",
        stale_magic=0xC0DE49E2,
    )
    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        "arm_play_open",
        [
            ("play_open_scratch_clear", PLAY_OPEN_SCRATCH, bytes(0x120)),
            ("play_open_probe", probe_addr, probe),
            ("play_open_jump", PLAY_OPEN_JUMP, call_patch),
        ],
    )
    if override_row_index is None:
        print(f"armed play-open probe in pid {pid} at 0x{probe_addr:x}")
    else:
        print(
            f"armed play-open override probe in pid {pid} at 0x{probe_addr:x}; "
            f"a2 will be forced to {override_row_index}"
        )


def restore_play_open(adb: str, probe_addr: int) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")
    call_patch = jump_patch(PLAY_OPEN_JUMP, probe_addr)
    actual = read_memory_bytes(adb, pid, PLAY_OPEN_JUMP, len(PLAY_OPEN_ORIGINAL), REMOTE_DIR, local, "play_open_restore_pre", "bin")
    if actual == PLAY_OPEN_ORIGINAL:
        print("play-open probe already restored")
        return
    if actual != call_patch:
        raise RuntimeError(
            f"play-open probe is not in a known state at 0x{PLAY_OPEN_JUMP:x}: "
            f"expected {call_patch.hex()} or {PLAY_OPEN_ORIGINAL.hex()}, got {actual.hex()}"
        )
    ptrace_write(
        adb,
        pid,
        REMOTE_DIR,
        local,
        "restore_play_open",
        [
            ("play_open_restore", PLAY_OPEN_JUMP, PLAY_OPEN_ORIGINAL),
            ("play_open_probe_clear", probe_addr, bytes(0x120)),
        ],
    )
    print(f"restored play-open probe and cleared 0x{probe_addr:x}")


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


def dump_nested_words(adb: str, pid: str, local: Path, label: str, addr: int, *, words: int = 48) -> None:
    if not (0x1000 <= addr < 0x80000000):
        return
    try:
        data = read_memory_bytes(adb, pid, addr, words * 4, REMOTE_DIR, local, f"{label}_{addr:x}_nested", "bin")
    except Exception as exc:
        print(f"{label}@0x{addr:08x}: nested read failed: {exc}")
        return
    values = struct.unpack("<" + "I" * words, data)
    for index, value in enumerate(values):
        if 0x1000 <= value < 0x80000000:
            dump_ptr(adb, pid, local, f"{label}[0x{index * 4:02x}]", value, 0x160)


def read_probe(adb: str) -> None:
    verify_known_player_binary(adb)
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
    album_ptr = vals[6] if len(vals) > 6 else 0
    dump_nested_words(adb, pid, local, "album_ptr", album_ptr)


def read_album_probe(adb: str) -> None:
    verify_known_player_binary(adb)
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
    album_ptr = vals[6] if len(vals) > 6 else 0
    dump_nested_words(adb, pid, local, "album_ptr", album_ptr, words=64)


def read_play_open_probe(adb: str) -> None:
    verify_known_player_binary(adb)
    pid = find_stock_player_pid(adb)
    local = Path("work/row-probe")
    local.mkdir(parents=True, exist_ok=True)
    shell(adb, f"mkdir -p '{REMOTE_DIR}'")

    data = read_memory_bytes(adb, pid, PLAY_OPEN_SCRATCH, 0x120, REMOTE_DIR, local, "play_open_scratch", "bin")
    vals = struct.unpack("<" + "I" * (len(data) // 4), data)
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
        "override_a2",
    ]
    for label, value in zip(labels, vals):
        print(f"{label}=0x{value:08x} ({value})")
    for label, value in zip(labels[2:16], vals[2:16]):
        dump_ptr(adb, pid, local, label, value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument(
        "--play-open-probe-addr",
        type=lambda value: int(value, 0),
        help="Audited executable zero-cave runtime address for arm/restore play-open.",
    )
    parser.add_argument(
        "--override-row-index",
        type=int,
        help="Zero-based row index to force in arm-play-open-override.",
    )
    parser.add_argument(
        "action",
        choices=[
            "arm-music",
            "restore-music",
            "arm-album",
            "arm-play-open",
            "arm-play-open-override",
            "restore-play-open",
            "read",
            "read-album",
            "read-play-open",
        ],
    )
    args = parser.parse_args()

    if args.action == "arm-music":
        arm_music(args.adb)
    elif args.action == "restore-music":
        restore_music(args.adb)
    elif args.action == "arm-album":
        arm_album(args.adb)
    elif args.action == "arm-play-open":
        if args.play_open_probe_addr is None:
            parser.error("arm-play-open requires --play-open-probe-addr from the cave audit")
        arm_play_open(args.adb, args.play_open_probe_addr)
    elif args.action == "arm-play-open-override":
        if args.play_open_probe_addr is None:
            parser.error("arm-play-open-override requires --play-open-probe-addr from the cave audit")
        if args.override_row_index is None:
            parser.error("arm-play-open-override requires --override-row-index")
        arm_play_open(args.adb, args.play_open_probe_addr, override_row_index=args.override_row_index)
    elif args.action == "restore-play-open":
        if args.play_open_probe_addr is None:
            parser.error("restore-play-open requires --play-open-probe-addr used to arm the probe")
        restore_play_open(args.adb, args.play_open_probe_addr)
    elif args.action == "read-play-open":
        read_play_open_probe(args.adb)
    elif args.action == "read-album":
        read_album_probe(args.adb)
    else:
        read_probe(args.adb)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
