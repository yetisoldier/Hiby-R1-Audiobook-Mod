#!/usr/bin/env python3
"""Deploy the C resume daemon to the R1 in shadow mode (runtime-only, no flash).

This pushes the MIPS binary to /tmp/ (UBIFS on /usr/data doesn't allow execution) and starts it
alongside the existing shell daemon. The shell daemon continues to act
as before. The C daemon only logs what it *would* do — no side effects.

Usage:
    python3 tools/deploy_shadow_mode.py [--adb PATH] [--device SERIAL]
                                        [--binary PATH] [--start]
                                        [--stop] [--status] [--remove]
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

log = logging.getLogger("deploy_shadow")

# ── Config ────────────────────────────────────────────────────────────────

REMOTE_DIR = "/tmp"  # /usr/data is UBIFS (noexec), /tmp is tmpfs (exec allowed)
REMOTE_BINARY = f"{REMOTE_DIR}/r1_audiobook_resume_daemon_c"
REMOTE_PID = "/usr/data/audiobooks/resume-daemon-c.pid"
REMOTE_LOG = "/usr/data/audiobooks/resume-daemon-c.log"
LOCAL_BINARY_DEFAULT = "build/r1_audiobook_resume_daemon"


def resolve_adb(arg: str | None) -> str:
    if arg:
        return arg
    path = shutil.which("adb")
    if path:
        return path
    fallback = "/home/yetisoldier/.local/bin/adb"
    if Path(fallback).exists():
        return fallback
    raise FileNotFoundError("adb not found. Install with: apt install android-tools-adb")


def adb_shell(adb: str, cmd: str, device: str | None = None) -> str:
    args = [adb]
    if device:
        args += ["-s", device]
    args += ["shell", cmd]
    r = subprocess.run(args, capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        log.warning("adb shell failed: %s (stderr: %s)", cmd, r.stderr.strip()[:200])
    return r.stdout.strip()


def adb_push(adb: str, local: str, remote: str, device: str | None = None) -> bool:
    args = [adb]
    if device:
        args += ["-s", device]
    args += ["push", local, remote]
    r = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        log.error("adb push failed: %s (stderr: %s)", r.stderr.strip()[:200])
        return False
    return True


def md5_file(path: str) -> str:
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(message)s",
        datefmt="%H:%M:%S",
    )
    parser = argparse.ArgumentParser(
        description="Deploy C resume daemon to R1 in shadow mode (runtime-only, no flash)."
    )
    parser.add_argument("--adb", default=None, help="Path to adb binary")
    parser.add_argument("--device", default=None, help="Target device serial")
    parser.add_argument("--binary", default=LOCAL_BINARY_DEFAULT, help="Path to MIPS binary")
    parser.add_argument("--start", action="store_true", help="Push and start in shadow mode")
    parser.add_argument("--stop", action="store_true", help="Stop the C daemon if running")
    parser.add_argument("--status", action="store_true", help="Check C daemon status")
    parser.add_argument("--remove", action="store_true", help="Stop and remove C daemon from R1")
    parser.add_argument("--collect-logs", default=None, help="Pull shadow logs to local path")
    args = parser.parse_args()

    try:
        adb = resolve_adb(args.adb)
    except FileNotFoundError as e:
        print(f"Error: {e}")
        return 1

    dev = args.device

    # ── Status ──────────────────────────────────────────────────────────
    if args.status:
        pid = adb_shell(adb, f"cat {REMOTE_PID} 2>/dev/null || echo NONE", dev)
        if pid == "NONE" or not pid:
            print("C daemon: NOT RUNNING")
            return 0
        running = adb_shell(adb, f"kill -0 {pid} 2>/dev/null && echo YES || echo NO", dev)
        if running == "YES":
            print(f"C daemon: RUNNING (pid={pid})")
            logsize = adb_shell(adb, f"wc -c < {REMOTE_LOG} 2>/dev/null || echo 0", dev)
            print(f"Shadow log: {REMOTE_LOG} ({logsize} bytes)")
        else:
            print(f"C daemon: STALE PID (pid={pid} not running)")
        # Also check shell daemon
        shell_pid = adb_shell(adb, "cat /usr/data/audiobooks/resume-daemon.pid 2>/dev/null || echo NONE", dev)
        if shell_pid != "NONE" and shell_pid:
            shell_running = adb_shell(adb, f"kill -0 {shell_pid} 2>/dev/null && echo YES || echo NO", dev)
            print(f"Shell daemon: {'RUNNING' if shell_running == 'YES' else 'NOT RUNNING'} (pid={shell_pid})")
        else:
            print("Shell daemon: NOT RUNNING")
        return 0

    # ── Stop ─────────────────────────────────────────────────────────────
    if args.stop:
        pid = adb_shell(adb, f"cat {REMOTE_PID} 2>/dev/null || echo NONE", dev)
        if pid != "NONE" and pid:
            adb_shell(adb, f"kill {pid} 2>/dev/null || true", dev)
            time.sleep(1)
            still = adb_shell(adb, f"kill -0 {pid} 2>/dev/null && echo YES || echo NO", dev)
            if still == "YES":
                adb_shell(adb, f"kill -9 {pid} 2>/dev/null || true", dev)
            print(f"Stopped C daemon (pid={pid})")
        else:
            print("C daemon was not running")
        adb_shell(adb, f"rm -f {REMOTE_PID} 2>/dev/null", dev)
        return 0

    # ── Remove ───────────────────────────────────────────────────────────
    if args.remove:
        # Stop first
        pid = adb_shell(adb, f"cat {REMOTE_PID} 2>/dev/null || echo NONE", dev)
        if pid != "NONE" and pid:
            adb_shell(adb, f"kill {pid} 2>/dev/null || true", dev)
            time.sleep(1)
            adb_shell(adb, f"kill -9 {pid} 2>/dev/null || true", dev)
        adb_shell(adb, f"rm -f {REMOTE_PID} {REMOTE_BINARY} {REMOTE_LOG} 2>/dev/null", dev)
        print("Removed C daemon from R1")
        return 0

    # ── Collect logs ─────────────────────────────────────────────────────
    if args.collect_logs:
        r = subprocess.run(
            [adb] + (["-s", dev] if dev else []) + ["pull", REMOTE_LOG, args.collect_logs],
            capture_output=True, text=True, timeout=30,
        )
        if r.returncode == 0:
            print(f"Pulled shadow logs to {args.collect_logs}")
        else:
            print(f"Could not pull logs: {r.stderr.strip()}")
        return 0

    # ── Start (push + launch) ────────────────────────────────────────────
    if not args.start:
        parser.print_help()
        return 0

    # Check device is connected
    r = subprocess.run([adb, "devices"], capture_output=True, text=True, timeout=10)
    if "device" not in r.stdout:
        print("Error: No ADB device connected")
        return 1

    # Check binary exists
    binary = Path(args.binary)
    if not binary.exists():
        print(f"Error: Binary not found: {binary}")
        print("Build it first: python3 tools/build_resume_daemon.py --target mips")
        return 1

    # Verify it's a MIPS binary
    r = subprocess.run(["file", str(binary)], capture_output=True, text=True, timeout=5)
    if "MIPS" not in r.stdout:
        print(f"Error: Binary is not MIPS: {r.stdout.strip()}")
        return 1

    local_md5 = md5_file(str(binary))
    local_size = binary.stat().st_size
    log.info("Local binary: %s (%d bytes, md5=%s)", binary.name, local_size, local_md5)

    # Create remote directory
    adb_shell(adb, f"mkdir -p {REMOTE_DIR} 2>/dev/null || true", dev)

    # Stop existing C daemon if running
    pid = adb_shell(adb, f"cat {REMOTE_PID} 2>/dev/null || echo NONE", dev)
    if pid != "NONE" and pid:
        log.info("Stopping existing C daemon (pid=%s)", pid)
        adb_shell(adb, f"kill {pid} 2>/dev/null || true", dev)
        time.sleep(1)
        adb_shell(adb, f"kill -9 {pid} 2>/dev/null || true", dev)
        adb_shell(adb, f"rm -f {REMOTE_PID} 2>/dev/null", dev)

    # Push binary
    log.info("Pushing binary to R1...")
    if not adb_push(adb, str(binary), REMOTE_BINARY, dev):
        print("Error: Failed to push binary")
        return 1

    # Verify push
    remote_size = adb_shell(adb, f"wc -c < {REMOTE_BINARY} 2>/dev/null || echo 0", dev)
    remote_md5 = adb_shell(adb, f"md5sum {REMOTE_BINARY} 2>/dev/null | awk '{{print $1}}' || echo FAIL", dev)
    log.info("Remote binary: %s bytes, md5=%s", remote_size, remote_md5)

    if str(local_size) != remote_size:
        print(f"Error: Size mismatch (local={local_size}, remote={remote_size})")
        return 1
    if local_md5 != remote_md5:
        print(f"Error: MD5 mismatch (local={local_md5}, remote={remote_md5})")
        return 1

    # Make executable
    adb_shell(adb, f"chmod 755 {REMOTE_BINARY}", dev)
    log.info("Binary pushed and verified ✓")

    # Start in shadow mode
    log.info("Starting C daemon in shadow mode...")
    start_cmd = (
        f"AUDIOBOOK_RESUME_LOG={REMOTE_LOG} AUDIOBOOK_RESUME_PID={REMOTE_PID}"
        f" nohup {REMOTE_BINARY} --shadow --base-dir /usr/data/audiobooks > /dev/null 2>&1 &"
        f" PID=$!; echo $PID > {REMOTE_PID}"
    )
    adb_shell(adb, start_cmd, dev)
    time.sleep(2)

    # Verify it's running
    pid = adb_shell(adb, f"cat {REMOTE_PID} 2>/dev/null || echo NONE", dev)
    if pid == "NONE" or not pid:
        print("Error: C daemon failed to start")
        logs = adb_shell(adb, f"cat {REMOTE_LOG} 2>/dev/null | tail -20", dev)
        if logs:
            print(f"Last log lines:\n{logs}")
        return 1

    running = adb_shell(adb, f"kill -0 {pid} 2>/dev/null && echo YES || echo NO", dev)
    if running != "YES":
        print(f"Error: C daemon started but exited immediately (pid={pid})")
        logs = adb_shell(adb, f"cat {REMOTE_LOG} 2>/dev/null | tail -20", dev)
        if logs:
            print(f"Last log lines:\n{logs}")
        return 1

    # Show first log lines
    log_lines = adb_shell(adb, f"head -5 {REMOTE_LOG} 2>/dev/null", dev)
    print(f"\n✅ C daemon running in shadow mode (pid={pid})")
    print(f"   Binary: {REMOTE_BINARY} ({local_size} bytes)")
    print(f"   Log: {REMOTE_LOG}")
    print(f"   Shell daemon: still running (untouched)")
    print(f"\nFirst log lines:")
    print(f"   {log_lines}")
    print(f"\nTo check status: python3 tools/deploy_shadow_mode.py --status")
    print(f"To stop:        python3 tools/deploy_shadow_mode.py --stop")
    print(f"To remove:      python3 tools/deploy_shadow_mode.py --remove")
    print(f"To pull logs:    python3 tools/deploy_shadow_mode.py --collect-logs shadow.log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
