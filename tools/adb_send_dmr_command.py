#!/usr/bin/env python3
"""
Send an arbitrary command to the R1's /data/dmr_streamer socket.

This is a test tool. It builds a tiny MIPS program that writes argv[1] to the
Unix socket, pushes it to /usr/data/audiobooks/bin, briefly opens the same
player-memory gate used by r1_audiobook_resume_helper, sends the command, then
closes the gate again.
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
import time
from pathlib import Path


ADB = r"C:\Program Files\Software Fix\adb.exe"
REMOTE_WRITER = "/usr/data/audiobooks/bin/r1_unix_socket_write"
REMOTE_SOCKET = "/data/dmr_streamer"
GATE_OFFSET = 12798612


def run(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=check,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    return run([adb, "shell", command], check=check).stdout


def shell_quote(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def add_li(lines: list[str], reg: str, value: int) -> None:
    value &= 0xFFFFFFFF
    lines.append(f"lui ${reg}, 0x{(value >> 16) & 0xFFFF:x}")
    lines.append(f"ori ${reg}, ${reg}, 0x{value & 0xFFFF:x}")


def assemble_writer(output: Path, socket_path: str) -> None:
    deps = Path(__file__).resolve().parents[1] / ".deps" / "python"
    if deps.exists():
        sys.path.insert(0, str(deps))
    from keystone import KS_ARCH_MIPS, KS_MODE_LITTLE_ENDIAN, KS_MODE_MIPS32, Ks

    base = 0x400000
    entry_offset = 84
    entry = base + entry_offset
    sockaddr_len = 110
    sockaddr = struct.pack("<H", 1) + socket_path.encode("ascii") + b"\x00"
    sockaddr = sockaddr.ljust(sockaddr_len, b"\x00")

    def build(data_addr: int) -> bytes:
        lines: list[str] = [
            "lw $s1, 8($sp)",
            "beqz $s1, fail_arg",
            "nop",
            "addiu $a0, $zero, 1",
            "addiu $a1, $zero, 2",
            "move $a2, $zero",
            "ori $v0, $zero, 4183",
            "syscall",
            "bne $a3, $zero, fail_socket",
            "nop",
            "move $s0, $v0",
            "move $a0, $s0",
        ]
        add_li(lines, "a1", data_addr)
        lines += [
            f"addiu $a2, $zero, {sockaddr_len}",
            "ori $v0, $zero, 4170",
            "syscall",
            "bne $a3, $zero, fail_connect",
            "nop",
            "move $t0, $s1",
            "move $a2, $zero",
            "len_loop:",
            "lbu $t1, 0($t0)",
            "beqz $t1, len_done",
            "nop",
            "addiu $t0, $t0, 1",
            "addiu $a2, $a2, 1",
            "b len_loop",
            "nop",
            "len_done:",
            "move $a0, $s0",
            "move $a1, $s1",
            "ori $v0, $zero, 4004",
            "syscall",
            "bne $a3, $zero, fail_write",
            "nop",
            "move $a0, $s0",
            "ori $v0, $zero, 4006",
            "syscall",
            "move $a0, $zero",
            "b exit_now",
            "nop",
            "fail_arg:",
            "addiu $a0, $zero, 10",
            "b exit_now",
            "nop",
            "fail_socket:",
            "addiu $a0, $zero, 20",
            "b exit_now",
            "nop",
            "fail_connect:",
            "addiu $a0, $zero, 30",
            "b exit_now",
            "nop",
            "fail_write:",
            "addiu $a0, $zero, 40",
            "exit_now:",
            "ori $v0, $zero, 4001",
            "syscall",
        ]
        asm = "\n".join(lines)
        return bytes(Ks(KS_ARCH_MIPS, KS_MODE_MIPS32 + KS_MODE_LITTLE_ENDIAN).asm(asm, addr=entry)[0])

    first_code = build(0)
    data_addr = entry + len(first_code)
    code = build(data_addr)
    data_addr = entry + len(code)
    code = build(data_addr)
    filesz = entry_offset + len(code) + len(sockaddr)
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
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(ehdr + phdr + code + sockaddr)


def current_path(adb: str) -> str:
    raw = adb_shell(
        adb,
        "dd if=/usr/data/user.ini bs=1 skip=40 count=512 2>/dev/null | xxd -p -c 512",
    )
    data = bytes.fromhex("".join(raw.split()))
    chars = []
    for index in range(0, len(data) - 1, 2):
        lo = data[index]
        hi = data[index + 1]
        if lo == 0 and hi == 0:
            if not chars:
                continue
            break
        chars.append(chr(lo) if hi == 0 else "?")
    path = "".join(chars)
    if path.startswith(":\\"):
        path = "a" + path
    elif path.startswith("\\Audiobooks\\"):
        path = "a:" + path
    return path


def position(adb: str) -> int | None:
    output = adb_shell(
        adb,
        "/usr/data/audiobooks/bin/r1_audiobook_resume_helper position 2>/dev/null",
        check=False,
    )
    match = re.search(r"position_ms=(\d+)", output)
    return int(match.group(1)) if match else None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("command")
    parser.add_argument("--adb", default=ADB)
    parser.add_argument("--remote-writer", default=REMOTE_WRITER)
    parser.add_argument("--socket", default=REMOTE_SOCKET)
    parser.add_argument("--sleep", type=float, default=1.5)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    local_writer = Path("work/dmr-socket-writer/r1_unix_socket_write")
    assemble_writer(local_writer, args.socket)

    before_path = current_path(args.adb)
    before_pos = position(args.adb)
    print(f"command:       {args.command}")
    print(f"before_path:   {before_path}")
    print(f"before_pos_ms: {before_pos}")
    print(run([args.adb, "push", str(local_writer), args.remote_writer], check=True).stdout, end="")
    adb_shell(args.adb, f"chmod 755 {args.remote_writer}")

    quoted_command = shell_quote(args.command)
    script = f"""
pid=
for p in /proc/[0-9]*; do
  [ "$(cat "$p/comm" 2>/dev/null)" = hiby_player ] || continue
  pid=${{p##*/}}
  break
done
[ -n "$pid" ] || exit 91
printf '\\001\\000\\000\\000' | dd of=/proc/$pid/mem bs=1 seek={GATE_OFFSET} count=4 conv=notrunc 2>/dev/null
{args.remote_writer} {quoted_command}
status=$?
printf '\\000\\000\\000\\000' | dd of=/proc/$pid/mem bs=1 seek={GATE_OFFSET} count=4 conv=notrunc 2>/dev/null
exit $status
"""
    result = adb_shell(args.adb, script, check=False)
    if result.strip():
        print("--- remote output ---")
        print(result.rstrip())
    time.sleep(args.sleep)
    after_path = current_path(args.adb)
    after_pos = position(args.adb)
    print(f"after_path:    {after_path}")
    print(f"after_pos_ms:  {after_pos}")
    print(f"path_changed:  {before_path != after_path}")


if __name__ == "__main__":
    main()
