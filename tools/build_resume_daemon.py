#!/usr/bin/env python3
"""
build_resume_daemon.py — Build the C resume daemon for both MIPS and Linux targets.

Usage:
    python3 tools/build_resume_daemon.py              # Build both targets
    python3 tools/build_resume_daemon.py --target mips   # MIPS only
    python3 tools/build_resume_daemon.py --target linux  # Linux test only
    python3 tools/build_resume_daemon.py --help          # Show help
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(PROJECT_ROOT, "src")
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
ZIG = os.environ.get("ZIG_CC", "/home/yetisoldier/tools/zig/zig")

SOURCES = [
    "main.c", "config.c", "log.c", "proc_mem.c", "helpers.c"
]

MIPS_TARGET = "mipsel-linux-musleabi"
LINUX_TARGET = "x86_64-linux-gnu"

MIPS_BINARY = os.path.join(BUILD_DIR, "r1_audiobook_resume_daemon")
LINUX_BINARY = os.path.join(BUILD_DIR, "r1_audiobook_resume_daemon_test")

MIPS_SIZE_BUDGET = 100 * 1024   # 100 KB stripped
LINUX_SIZE_BUDGET = 200 * 1024  # 200 KB


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def run(cmd, cwd=None):
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, file=sys.stdout)
        print(result.stderr, file=sys.stderr)
        return False
    return True


def build_target(target, output, stripped=True):
    srcs = [os.path.join(SRC_DIR, f) for f in SOURCES]
    cmd = [ZIG, "cc", "-target", target, "-Os"]

    if target == MIPS_TARGET:
        cmd.extend(["-static", "-fno-stack-protector"])

    cmd.extend([
        "-ffunction-sections",
        "-fdata-sections",
        "-Wl,--gc-sections",
    ])

    if stripped:
        cmd.append("-s")

    cmd.extend(["-o", output])
    cmd.extend(srcs)

    print(f"\nBuilding {target} -> {os.path.basename(output)}")
    if not run(cmd):
        return False

    # Report size
    size = os.path.getsize(output)
    budget = MIPS_SIZE_BUDGET if target == MIPS_TARGET else LINUX_SIZE_BUDGET
    status = "OK" if size <= budget else "OVER BUDGET"
    print(f"  Binary size: {size:,} bytes (budget: {budget:,}) [{status}]")

    return size <= budget


def main():
    parser = argparse.ArgumentParser(
        description="Build the C resume daemon for MIPS (R1) and/or Linux (test)."
    )
    parser.add_argument(
        "--target", choices=["mips", "linux", "both"],
        default="both", help="Build target (default: both)"
    )
    parser.add_argument(
        "--no-strip", action="store_true",
        help="Don't strip debug symbols"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Show compiler output"
    )
    args = parser.parse_args()

    # Check Zig exists
    if not os.path.isfile(ZIG):
        print(f"Error: Zig compiler not found at {ZIG}")
        print("Set ZIG_CC environment variable to the zig binary path.")
        return 1

    ensure_dir(BUILD_DIR)

    success = True
    t0 = time.time()

    if args.target in ("mips", "both"):
        if not build_target(MIPS_TARGET, MIPS_BINARY, stripped=not args.no_strip):
            success = False
            print("  MIPS build FAILED")

    if args.target in ("linux", "both"):
        if not build_target(LINUX_TARGET, LINUX_BINARY, stripped=not args.no_strip):
            success = False
            print("  Linux build FAILED")

    elapsed = time.time() - t0

    if success:
        print(f"\nAll builds succeeded in {elapsed:.1f}s")
        return 0
    else:
        print(f"\nSome builds FAILED (elapsed: {elapsed:.1f}s)")
        return 1


if __name__ == "__main__":
    sys.exit(main())