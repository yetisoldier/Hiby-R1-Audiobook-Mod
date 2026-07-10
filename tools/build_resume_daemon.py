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
TESTS_DIR = os.path.join(PROJECT_ROOT, "tests")
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
ZIG = os.environ.get("ZIG_CC", "/home/yetisoldier/tools/zig/zig")

# Source files for the daemon
SOURCES = [
    "main.c", "config.c", "log.c", "proc_mem.c", "helpers.c",
    "player.c", "catalog.c", "resume.c",
]

# Source files for the test binary (daemon sources + test file)
# test_resume_daemon.c provides main(); main.c's main() is excluded via source_only config
TEST_SOURCES = [
    "config.c", "log.c", "proc_mem.c", "helpers.c",
    "player.c", "catalog.c", "resume.c",
    "test_resume_daemon.c",  # from tests/ dir — provides main()
]

MIPS_TARGET = "mipsel-linux-musleabi"
LINUX_TARGET = "x86_64-linux-gnu"

MIPS_BINARY = os.path.join(BUILD_DIR, "r1_audiobook_resume_daemon")
LINUX_BINARY = os.path.join(BUILD_DIR, "r1_audiobook_resume_daemon_test")

MIPS_SIZE_BUDGET = 100 * 1024    # 100 KB stripped
LINUX_SIZE_BUDGET = 300 * 1024  # 300 KB (grown due to more modules)


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def run(cmd, cwd=None):
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, file=sys.stdout)
        print(result.stderr, file=sys.stderr)
        return False
    if result.stdout:
        print(result.stdout, end='', file=sys.stdout)
    return True


def build_mips(stripped=True):
    """Build the MIPS cross-compiled binary."""
    srcs = [os.path.join(SRC_DIR, f) for f in SOURCES]
    cmd = [ZIG, "cc", "-target", MIPS_TARGET, "-Oz",
           "-I", SRC_DIR,
           "-static", "-fno-stack-protector",
           "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
           "-ffunction-sections", "-fdata-sections",
           "-Wl,--gc-sections", "-Wl,--strip-all", "-Wl,--no-eh-frame-hdr"]

    if stripped:
        cmd.append("-s")

    cmd.extend(["-o", MIPS_BINARY])
    cmd.extend(srcs)

    print(f"\nBuilding {MIPS_TARGET} -> {os.path.basename(MIPS_BINARY)}")
    if not run(cmd):
        return False

    # Post-strip to remove non-loadable sections that inflate file size
    # (.pdr, .comment, .MIPS.abiflags, .reginfo)
    strip_bin = "mipsel-linux-gnu-strip"
    if shutil.which(strip_bin):
        strip_cmd = [strip_bin, "--strip-all",
                     "--remove-section=.pdr",
                     "--remove-section=.comment",
                     "--remove-section=.MIPS.abiflags",
                     "--remove-section=.reginfo",
                     MIPS_BINARY]
        print(f"  $ {' '.join(strip_cmd)}")
        subprocess.run(strip_cmd, capture_output=True)
    else:
        print(f"  (note: {strip_bin} not found, skipping extra strip)")

    size = os.path.getsize(MIPS_BINARY)
    status = "OK" if size <= MIPS_SIZE_BUDGET else "OVER BUDGET"
    print(f"  Binary size: {size:,} bytes (budget: {MIPS_SIZE_BUDGET:,}) [{status}]")

    return size <= MIPS_SIZE_BUDGET


def build_linux(stripped=True):
    """Build the Linux test binary with test suite."""
    srcs = [os.path.join(SRC_DIR, f) for f in TEST_SOURCES
            if f != "test_resume_daemon.c"]
    srcs.append(os.path.join(TESTS_DIR, "test_resume_daemon.c"))

    cmd = [ZIG, "cc", "-target", LINUX_TARGET, "-O2",
           "-I", SRC_DIR,
           "-ffunction-sections", "-fdata-sections",
           "-Wl,--gc-sections"]

    if stripped:
        cmd.append("-s")

    cmd.extend(["-o", LINUX_BINARY])
    cmd.extend(srcs)

    print(f"\nBuilding {LINUX_TARGET} -> {os.path.basename(LINUX_BINARY)}")
    if not run(cmd):
        return False

    size = os.path.getsize(LINUX_BINARY)
    status = "OK" if size <= LINUX_SIZE_BUDGET else "OVER BUDGET"
    print(f"  Binary size: {size:,} bytes (budget: {LINUX_SIZE_BUDGET:,}) [{status}]")

    return size <= LINUX_SIZE_BUDGET


def run_tests():
    """Run the Linux test binary to execute unit tests."""
    if not os.path.isfile(LINUX_BINARY):
        print("  Test binary not found, skipping tests")
        return False

    print("\nRunning unit tests...")
    result = subprocess.run([LINUX_BINARY], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='', file=sys.stderr)
    return result.returncode == 0


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
        "--run-tests", action="store_true",
        help="Run unit tests after building Linux target"
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
        if not build_mips(stripped=not args.no_strip):
            success = False
            print("  MIPS build FAILED")

    if args.target in ("linux", "both"):
        if not build_linux(stripped=not args.no_strip):
            success = False
            print("  Linux build FAILED")
        elif args.run_tests:
            if not run_tests():
                success = False
                print("  Tests FAILED")

    elapsed = time.time() - t0

    if success:
        print(f"\nAll builds succeeded in {elapsed:.1f}s")
        return 0
    else:
        print(f"\nSome builds FAILED (elapsed: {elapsed:.1f}s)")
        return 1


if __name__ == "__main__":
    sys.exit(main())