#!/usr/bin/env python3
"""Temporarily patch the running stock HiBy R1 player process in RAM.

This does not write rootfs or flash firmware. A reboot discards the changes.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from patch_hiby_player import (  # noqa: E402
    AUDIOBOOK_LAUNCHER_CODE,
    AUDIOBOOK_LAUNCHER_PATCHES,
    AUDIOBOOK_LAUNCHER_ROUTE,
    AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE,
    AUDIOBOOK_LAUNCHER_SELECTED_GENRE,
    AUDIOBOOK_TITLE_MARKER_PATCHES,
    BOOK_AUDIO_SHIM_PATCHES,
    SCAN_SKIP_OFFSET,
    SCAN_SKIP_ORIGINAL,
    SCAN_SKIP_PATCHED,
    STOCK_MD5,
)


ELF_BASE = 0x400000
DATA_FILE_OFFSET = 0x43CD60
DATA_VADDR = 0x84CD60
DEFAULT_ADB = r"C:\Program Files\Software Fix\adb.exe"
LAST_REMOTE_FILE = Path("work/audiobook-firmware/last-runtime-patch-remote.txt")
PAGE_SIZE = 4096
HEAP_SCAN_CHUNK_SIZE = 64 * 1024
KNOWN_AUDIOBOOK_PLAYER_MD5S = {
    # Early audiobook launcher/resume builds.
    "09997a636c94112ff76c85a6d4a8d0ff",
    # Public 1.6.15-audiobook release.
    "dac7b58717097ef2a75ae5887478ef16",
    # 1.6.28-sd-ready-dev route/listview build.
    "c161af12bd050aca6f3fc2f67979d792",
    # 1.6.16.8-private-route-dev, used only for route diagnostics.
    "c168c57b3f22e8bfc4ee5fccb1b9455a",
}

AUDIOBOOK_LAUNCHER_HEAP_LABEL = b"launcher_apps_vg_ebook"
AUDIOBOOK_LAUNCHER_HEAP_CALLBACK_DELTA = 0x148
AUDIOBOOK_LAUNCHER_CALLBACK_OLD = AUDIOBOOK_LAUNCHER_PATCHES[3][1]
AUDIOBOOK_LAUNCHER_CALLBACK_NEW = AUDIOBOOK_LAUNCHER_PATCHES[3][2]


@dataclass(frozen=True)
class MemoryMap:
    start: int
    end: int
    perms: str
    path: str


@dataclass(frozen=True)
class LiveLauncherCallback:
    label_addr: int
    callback_addr: int
    actual: bytes


def run_adb(adb: str, args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        [adb, *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"adb {' '.join(args)} failed with {proc.returncode}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc


def shell(adb: str, command: str, *, check: bool = True) -> str:
    proc = run_adb(adb, ["shell", command], check=check)
    return proc.stdout


def find_stock_player_pid(adb: str) -> str:
    command = (
        "ps | sed -n "
        "'/\\/usr\\/bin\\/hiby_player$/ { /\\/bin\\/sh -c/d; /grep/d; "
        "s/^ *\\([0-9][0-9]*\\).*/\\1/p; q; }'"
    )
    pid = shell(adb, command).strip()
    if not pid:
        raise RuntimeError("Could not find running stock /usr/bin/hiby_player process.")
    return pid


def verify_known_binary(adb: str) -> str:
    out = shell(adb, "md5sum /usr/bin/hiby_player").strip()
    digest = out.split()[0].lower()
    if digest not in {STOCK_MD5, *KNOWN_AUDIOBOOK_PLAYER_MD5S}:
        raise RuntimeError(f"/usr/bin/hiby_player is not a known stock/audiobook 1.6 binary: {digest}")
    return digest


def file_offset_to_runtime_addr(file_offset: int) -> int:
    if file_offset >= DATA_FILE_OFFSET:
        return DATA_VADDR + (file_offset - DATA_FILE_OFFSET)
    return ELF_BASE + file_offset


def patch_specs(patch_set: str) -> list[tuple[str, int, bytes, bytes]]:
    if patch_set == "audiobook-launcher":
        ordered_patches = [
            # Write helpers before replacing branches/callback pointers that jump to them.
            ("audiobook_launcher_cave", AUDIOBOOK_LAUNCHER_PATCHES[0]),
            ("audiobook_book_open_root", AUDIOBOOK_LAUNCHER_PATCHES[1]),
            ("audiobook_title_marker_cave", AUDIOBOOK_TITLE_MARKER_PATCHES[0]),
            ("audiobook_book_open_hook", AUDIOBOOK_LAUNCHER_PATCHES[2]),
            ("audiobook_title_marker_hook", AUDIOBOOK_TITLE_MARKER_PATCHES[1]),
            ("audiobook_launcher_callback", AUDIOBOOK_LAUNCHER_PATCHES[3]),
        ]
        return [
            (name, file_offset_to_runtime_addr(file_offset), old, new)
            for name, (file_offset, old, new) in ordered_patches
        ]

    if patch_set != "book-shim":
        raise RuntimeError(f"unknown patch set: {patch_set}")

    hook_patch, dialog_patch, cave_patch = BOOK_AUDIO_SHIM_PATCHES
    ordered_patches = [
        ("scan_skip", (SCAN_SKIP_OFFSET, SCAN_SKIP_ORIGINAL, SCAN_SKIP_PATCHED)),
        # Write the code cave before enabling the branch that jumps to it.
        ("book_cave", cave_patch),
        ("book_dialog_nop", dialog_patch),
        ("book_hook", hook_patch),
    ]
    return [
        (name, file_offset_to_runtime_addr(file_offset), old, new)
        for name, (file_offset, old, new) in ordered_patches
    ]


def write_local_patch_files(root: Path, specs: list[tuple[str, int, bytes, bytes]]) -> dict[str, Path]:
    root.mkdir(parents=True, exist_ok=True)
    paths: dict[str, Path] = {}
    for name, _addr, _old, new in specs:
        path = root / f"{name}.bin"
        path.write_bytes(new)
        paths[name] = path
    return paths


def remote_md5(adb: str, path: str) -> str:
    return shell(adb, f"md5sum '{path}'").split()[0].lower()


def read_memory_bytes(
    adb: str,
    pid: str,
    addr: int,
    size: int,
    remote_dir: str,
    local_dir: Path,
    name: str,
    suffix: str,
) -> bytes:
    remote_path = f"{remote_dir}/{name}.{suffix}"
    local_path = local_dir / f"{name}.{suffix}"
    read_memory_to_remote(adb, pid, addr, size, remote_path)
    run_adb(adb, ["pull", remote_path, str(local_path)])
    return local_path.read_bytes()


def read_memory_to_remote(adb: str, pid: str, addr: int, size: int, remote_path: str) -> None:
    last_status = ""
    if addr % PAGE_SIZE == 0 and size % PAGE_SIZE == 0:
        dd_args = f"bs={PAGE_SIZE} skip={addr // PAGE_SIZE} count={size // PAGE_SIZE}"
    else:
        dd_args = f"bs=1 skip={addr} count={size}"
    for _attempt in range(3):
        status = shell(
            adb,
            f"rm -f '{remote_path}'; "
            f"dd if=/proc/{pid}/mem of='{remote_path}' {dd_args} 2>&1; "
            f"echo __SIZE__:$(wc -c < '{remote_path}' 2>/dev/null || echo missing)",
        )
        last_status = status
        for line in status.splitlines():
            if line.startswith("__SIZE__:"):
                value = line.split(":", 1)[1].strip()
                if value == str(size):
                    return
        time.sleep(0.2)
    raise RuntimeError(
        f"Could not read {size} bytes from /proc/{pid}/mem at 0x{addr:x} into {remote_path}.\n"
        f"Last status:\n{last_status}"
    )


def parse_memory_maps(adb: str, pid: str) -> list[MemoryMap]:
    maps: list[MemoryMap] = []
    for raw in shell(adb, f"cat /proc/{pid}/maps").splitlines():
        line = raw.strip().rstrip("\r")
        if not line:
            continue
        parts = line.split(None, 5)
        if len(parts) < 2 or "-" not in parts[0]:
            continue
        start_text, end_text = parts[0].split("-", 1)
        path = parts[5] if len(parts) >= 6 else ""
        maps.append(
            MemoryMap(
                start=int(start_text, 16),
                end=int(end_text, 16),
                perms=parts[1],
                path=path,
            )
        )
    return maps


def read_mapping_bytes(
    adb: str,
    pid: str,
    mapping: MemoryMap,
    remote_dir: str,
    local_dir: Path,
    name: str,
) -> bytes:
    local_dir.mkdir(parents=True, exist_ok=True)
    remote_path = f"{remote_dir}/{name}.bin"
    local_path = local_dir / f"{name}.bin"
    size = mapping.end - mapping.start
    if mapping.start % PAGE_SIZE == 0 and size % PAGE_SIZE == 0:
        command = (
            f"rm -f '{remote_path}'; "
            f"dd if=/proc/{pid}/mem of='{remote_path}' bs={PAGE_SIZE} "
            f"skip={mapping.start // PAGE_SIZE} count={size // PAGE_SIZE} 2>&1; "
            f"echo __SIZE__:$(wc -c < '{remote_path}' 2>/dev/null || echo missing)"
        )
    else:
        command = (
            f"rm -f '{remote_path}'; "
            f"dd if=/proc/{pid}/mem of='{remote_path}' bs=1 skip={mapping.start} "
            f"count={size} 2>&1; "
            f"echo __SIZE__:$(wc -c < '{remote_path}' 2>/dev/null || echo missing)"
        )

    status = shell(adb, command)
    for line in status.splitlines():
        if line.startswith("__SIZE__:") and line.split(":", 1)[1].strip() == str(size):
            run_adb(adb, ["pull", remote_path, str(local_path)])
            return local_path.read_bytes()
    raise RuntimeError(
        f"Could not read mapping 0x{mapping.start:x}-0x{mapping.end:x} into {remote_path}.\n"
        f"Status:\n{status}"
    )


def find_live_launcher_callbacks(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
) -> list[LiveLauncherCallback]:
    callbacks: list[LiveLauncherCallback] = []
    seen_addrs: set[int] = set()
    overlap = len(AUDIOBOOK_LAUNCHER_HEAP_LABEL) + AUDIOBOOK_LAUNCHER_HEAP_CALLBACK_DELTA + len(AUDIOBOOK_LAUNCHER_CALLBACK_OLD)
    heap_maps = [
        item
        for item in parse_memory_maps(adb, pid)
        if "r" in item.perms and "w" in item.perms and "[heap]" in item.path
    ]
    for index, mapping in enumerate(heap_maps):
        offset = 0
        previous_tail = b""
        while mapping.start + offset < mapping.end:
            remaining = mapping.end - (mapping.start + offset)
            size = min(HEAP_SCAN_CHUNK_SIZE, remaining)
            data = read_memory_bytes(
                adb,
                pid,
                mapping.start + offset,
                size,
                remote_dir,
                local_dir,
                f"heap-{index}-{offset:x}",
                "scan",
            )
            combined_base = mapping.start + offset - len(previous_tail)
            combined = previous_tail + data
            start = 0
            while True:
                label_index = combined.find(AUDIOBOOK_LAUNCHER_HEAP_LABEL, start)
                if label_index < 0:
                    break
                callback_index = label_index + AUDIOBOOK_LAUNCHER_HEAP_CALLBACK_DELTA
                actual = combined[callback_index : callback_index + len(AUDIOBOOK_LAUNCHER_CALLBACK_OLD)]
                callback_addr = combined_base + callback_index
                if (
                    len(actual) == len(AUDIOBOOK_LAUNCHER_CALLBACK_OLD)
                    and actual in (AUDIOBOOK_LAUNCHER_CALLBACK_OLD, AUDIOBOOK_LAUNCHER_CALLBACK_NEW)
                    and callback_addr not in seen_addrs
                ):
                    seen_addrs.add(callback_addr)
                    callbacks.append(
                        LiveLauncherCallback(
                            label_addr=combined_base + label_index,
                            callback_addr=callback_addr,
                            actual=actual,
                        )
                    )
                start = label_index + 1
            previous_tail = combined[-overlap:]
            offset += size
    return callbacks


def describe_live_launcher_state(callbacks: list[LiveLauncherCallback]) -> str:
    if not callbacks:
        return "none"
    states = []
    for callback in callbacks:
        if callback.actual == AUDIOBOOK_LAUNCHER_CALLBACK_OLD:
            states.append("stock")
        elif callback.actual == AUDIOBOOK_LAUNCHER_CALLBACK_NEW:
            states.append("patched")
        else:
            states.append("unknown")
    if all(state == states[0] for state in states):
        return states[0]
    return ",".join(states)


def patch_state(name: str, actual: bytes, old: bytes, new: bytes) -> str:
    if actual == old:
        return "stock"
    if actual == new:
        return "patched"
    if name == "audiobook_launcher_cave":
        route_start = AUDIOBOOK_LAUNCHER_ROUTE_OFFSET_IN_CAVE
        route_end = route_start + len(AUDIOBOOK_LAUNCHER_ROUTE)
        selected_end = route_end + len(AUDIOBOOK_LAUNCHER_SELECTED_GENRE)
        if (
            actual.startswith(AUDIOBOOK_LAUNCHER_CODE)
            and actual[route_start:route_end] == AUDIOBOOK_LAUNCHER_ROUTE
            and actual[route_end:selected_end] == AUDIOBOOK_LAUNCHER_SELECTED_GENRE
        ):
            return "patched"
    return "unknown"


def patch_live_launcher_callbacks(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    *,
    patched: bool,
) -> bool:
    shell(adb, f"mkdir -p '{remote_dir}'")
    callbacks = find_live_launcher_callbacks(adb, pid, remote_dir, local_dir)
    desired = AUDIOBOOK_LAUNCHER_CALLBACK_NEW if patched else AUDIOBOOK_LAUNCHER_CALLBACK_OLD
    source = AUDIOBOOK_LAUNCHER_CALLBACK_OLD if patched else AUDIOBOOK_LAUNCHER_CALLBACK_NEW
    print(f"live launcher callback instances: {len(callbacks)} ({describe_live_launcher_state(callbacks)})")
    writes = [
        (f"launcher_heap_{callback.callback_addr:x}", callback.callback_addr, desired)
        for callback in callbacks
        if callback.actual == source
    ]
    if not writes:
        return False
    ptrace_write(adb, pid, remote_dir, local_dir, "live-launcher", writes)
    for name, addr, _data in writes:
        actual = read_memory_bytes(
            adb,
            pid,
            addr,
            len(desired),
            remote_dir,
            local_dir,
            name,
            "verify",
        )
        if actual != desired:
            raise RuntimeError(f"Live launcher callback verification failed for {name} at 0x{addr:x}")
    return True


def memory_state(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    specs: list[tuple[str, int, bytes, bytes]],
) -> str:
    states: list[str] = []
    for name, addr, old, new in specs:
        actual = read_memory_bytes(adb, pid, addr, len(old), remote_dir, local_dir, name, "state")
        states.append(patch_state(name, actual, old, new))
    if all(state == "stock" for state in states):
        return "stock"
    if all(state == "patched" for state in states):
        return "patched"
    return ",".join(states)


def backup_and_verify(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    specs: list[tuple[str, int, bytes, bytes]],
) -> None:
    local_dir.mkdir(parents=True, exist_ok=True)
    for name, addr, old, _new in specs:
        remote_backup = f"{remote_dir}/{name}.orig"
        read_memory_to_remote(adb, pid, addr, len(old), remote_backup)
        local_backup = local_dir / f"{name}.orig"
        run_adb(adb, ["pull", remote_backup, str(local_backup)])
        actual = local_backup.read_bytes()
        if actual != old:
            raise RuntimeError(
                f"Memory precheck failed for {name} at 0x{addr:x}: "
                f"expected {hashlib.md5(old).hexdigest()}, got {hashlib.md5(actual).hexdigest()}"
            )


def add_li(lines: list[str], reg: str, value: int) -> None:
    value &= 0xFFFFFFFF
    lines.append(f"lui ${reg}, 0x{(value >> 16) & 0xFFFF:x}")
    lines.append(f"ori ${reg}, ${reg}, 0x{value & 0xFFFF:x}")


def build_ptrace_writer(path: Path, pid: str, writes: list[tuple[str, int, bytes]]) -> None:
    deps = Path(__file__).resolve().parents[1] / ".deps" / "python"
    if deps.exists():
        sys.path.insert(0, str(deps))
    try:
        from keystone import KS_ARCH_MIPS, KS_MODE_LITTLE_ENDIAN, KS_MODE_MIPS32, Ks
    except ImportError as exc:
        raise RuntimeError(
            "keystone-engine is required to generate the MIPS ptrace helper. "
            "Install it into .deps/python or run from the prepared workspace."
        ) from exc

    pid_int = int(pid)
    lines: list[str] = [
        "addiu $sp, $sp, -16",
        "addiu $a0, $zero, 16",
    ]
    add_li(lines, "a1", pid_int)
    lines += [
        "move $a2, $zero",
        "move $a3, $zero",
        "ori $v0, $zero, 4026",
        "syscall",
        "bne $a3, $zero, fail_attach",
        "nop",
    ]
    add_li(lines, "a0", pid_int)
    lines += [
        "move $a1, $sp",
        "move $a2, $zero",
        "ori $v0, $zero, 4007",
        "syscall",
        "bne $a3, $zero, fail_wait",
        "nop",
    ]

    for name, addr, data in writes:
        if len(data) % 4:
            raise RuntimeError(f"{name} length is not word aligned: {len(data)}")
        for i in range(0, len(data), 4):
            word = int.from_bytes(data[i : i + 4], "little")
            lines += ["addiu $a0, $zero, 4"]
            add_li(lines, "a1", pid_int)
            add_li(lines, "a2", addr + i)
            add_li(lines, "a3", word)
            lines += [
                "ori $v0, $zero, 4026",
                "syscall",
                "bne $a3, $zero, fail_write",
                "nop",
            ]

    lines += [
        "b detach_ok",
        "nop",
        "fail_attach:",
        "addiu $s0, $zero, 11",
        "b exit_status",
        "nop",
        "fail_wait:",
        "addiu $s0, $zero, 12",
        "b detach_with_status",
        "nop",
        "fail_write:",
        "addiu $s0, $zero, 21",
        "b detach_with_status",
        "nop",
        "detach_ok:",
        "move $s0, $zero",
        "detach_with_status:",
        "addiu $a0, $zero, 17",
    ]
    add_li(lines, "a1", pid_int)
    lines += [
        "move $a2, $zero",
        "move $a3, $zero",
        "ori $v0, $zero, 4026",
        "syscall",
        "move $a0, $s0",
        "b exit_now",
        "nop",
        "exit_status:",
        "move $a0, $s0",
        "exit_now:",
        "ori $v0, $zero, 4001",
        "syscall",
    ]

    base = 0x400000
    entry_offset = 84
    entry = base + entry_offset
    asm = "\n".join(lines)
    code = bytes(Ks(KS_ARCH_MIPS, KS_MODE_MIPS32 + KS_MODE_LITTLE_ENDIAN).asm(asm, addr=entry)[0])
    filesz = entry_offset + len(code)
    e_ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + bytes(8)
    ehdr = struct.pack(
        "<16sHHIIIIIHHHHHH",
        e_ident,
        2,
        8,
        1,
        entry,
        52,
        0,
        0x70001007,
        52,
        32,
        1,
        0,
        0,
        0,
    )
    phdr = struct.pack("<IIIIIIII", 1, 0, base, base, filesz, filesz, 5, 0x10000)
    path.write_bytes(ehdr + phdr + code)


def ptrace_write(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    label: str,
    writes: list[tuple[str, int, bytes]],
) -> None:
    helper = local_dir / f"mips_ptrace_{label}_{pid}"
    build_ptrace_writer(helper, pid, writes)
    remote_helper = f"{remote_dir}/{helper.name}"
    run_adb(adb, ["push", str(helper), remote_helper])
    out = shell(adb, f"chmod 755 '{remote_helper}'; '{remote_helper}'; echo __EXIT__:$?")
    exit_lines = [line for line in out.splitlines() if line.startswith("__EXIT__:")]
    if not exit_lines:
        raise RuntimeError(f"Could not determine ptrace helper exit status.\nOutput:\n{out}")
    status = int(exit_lines[-1].split(":", 1)[1])
    if status != 0:
        raise RuntimeError(f"ptrace helper failed with exit status {status}.\nOutput:\n{out}")


def verify_expected(
    adb: str,
    pid: str,
    remote_dir: str,
    local_dir: Path,
    specs: list[tuple[str, int, bytes, bytes]],
    *,
    patched: bool,
) -> None:
    suffix = "patched" if patched else "reverted"
    for name, addr, old, new in specs:
        expected = new if patched else old
        actual = read_memory_bytes(
            adb, pid, addr, len(expected), remote_dir, local_dir, name, suffix
        )
        if actual != expected:
            raise RuntimeError(f"Memory {suffix} verification failed for {name} at 0x{addr:x}")


def apply_patch(adb: str, pid: str, remote_dir: str, local_dir: Path, patch_set: str) -> bool:
    specs = patch_specs(patch_set)
    shell(adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)
    state = memory_state(adb, pid, remote_dir, local_dir, specs)
    if state == "patched":
        print("runtime patches are already present")
        return False
    if state != "stock":
        raise RuntimeError(f"Refusing to apply over mixed/unknown memory state: {state}")
    backup_and_verify(adb, pid, remote_dir, local_dir, specs)

    ptrace_write(adb, pid, remote_dir, local_dir, "apply", [(n, a, new) for n, a, _old, new in specs])
    verify_expected(adb, pid, remote_dir, local_dir, specs, patched=True)

    LAST_REMOTE_FILE.parent.mkdir(parents=True, exist_ok=True)
    LAST_REMOTE_FILE.write_text(remote_dir, encoding="ascii")
    return True


def revert_patch(adb: str, pid: str, remote_dir: str, patch_set: str) -> None:
    specs = patch_specs(patch_set)
    local_dir = Path("work/runtime-patch-files") / f"revert-{time.strftime('%Y%m%d-%H%M%S')}"
    shell(adb, f"mkdir -p '{remote_dir}'")
    local_dir.mkdir(parents=True, exist_ok=True)
    state = memory_state(adb, pid, remote_dir, local_dir, specs)
    if state == "stock":
        print("runtime patches are already absent")
        return
    if state != "patched":
        raise RuntimeError(f"Refusing to revert mixed/unknown memory state: {state}")
    ptrace_write(adb, pid, remote_dir, local_dir, "revert", [(n, a, old) for n, a, old, _new in specs])
    verify_expected(adb, pid, remote_dir, local_dir, specs, patched=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--revert", action="store_true")
    parser.add_argument(
        "--patch-set",
        choices=("book-shim", "audiobook-launcher"),
        default="book-shim",
        help="Runtime patch set to inspect/apply/revert.",
    )
    parser.add_argument("--remote-dir")
    parser.add_argument(
        "--i-understand-this-writes-process-memory",
        action="store_true",
        help="Required with --apply or --revert.",
    )
    args = parser.parse_args()

    if args.apply and args.revert:
        parser.error("choose only one of --apply or --revert")

    player_md5 = verify_known_binary(args.adb)
    pid = find_stock_player_pid(args.adb)
    print(f"player pid: {pid}")
    print(f"player md5: {player_md5}")
    print(f"patch set: {args.patch_set}")

    if not args.apply and not args.revert:
        if args.patch_set == "audiobook-launcher":
            stamp = time.strftime("%Y%m%d-%H%M%S")
            remote_dir = args.remote_dir or f"/usr/data/codex_runtime_patch_dryrun_{stamp}"
            local_dir = Path("work/runtime-patch-files") / f"dryrun-{stamp}"
            shell(args.adb, f"mkdir -p '{remote_dir}'")
            local_dir.mkdir(parents=True, exist_ok=True)
            static_state = memory_state(args.adb, pid, remote_dir, local_dir, patch_specs(args.patch_set))
            callbacks = find_live_launcher_callbacks(args.adb, pid, remote_dir, local_dir)
            print(f"static patch state: {static_state}")
            print(
                "live launcher callback instances: "
                f"{len(callbacks)} ({describe_live_launcher_state(callbacks)})"
            )
        print("dry run only; no process memory was written")
        return 0

    if not args.i_understand_this_writes_process_memory:
        parser.error("--apply/--revert requires --i-understand-this-writes-process-memory")

    if args.apply:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        remote_dir = args.remote_dir or f"/usr/data/codex_runtime_patch_{stamp}"
        local_dir = Path("work/runtime-patch-files") / stamp
        changed = apply_patch(args.adb, pid, remote_dir, local_dir, args.patch_set)
        if args.patch_set == "audiobook-launcher":
            changed = (
                patch_live_launcher_callbacks(
                    args.adb,
                    pid,
                    remote_dir,
                    local_dir,
                    patched=True,
                )
                or changed
            )
        if changed:
            print(f"applied runtime patches to pid {pid}")
        else:
            print(f"left pid {pid} unchanged")
        print(f"remote backup dir: {remote_dir}")
        return 0

    remote_dir = args.remote_dir
    if not remote_dir:
        if not LAST_REMOTE_FILE.exists():
            raise RuntimeError("No remote dir supplied and no previous runtime patch state exists.")
        remote_dir = LAST_REMOTE_FILE.read_text(encoding="ascii").strip()
    local_dir = Path("work/runtime-patch-files") / f"revert-{time.strftime('%Y%m%d-%H%M%S')}"
    revert_patch(args.adb, pid, remote_dir, args.patch_set)
    if args.patch_set == "audiobook-launcher":
        patch_live_launcher_callbacks(
            args.adb,
            pid,
            remote_dir,
            local_dir,
            patched=False,
        )
    print(f"reverted runtime patches for pid {pid}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
