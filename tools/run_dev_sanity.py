#!/usr/bin/env python3
"""Run local development sanity checks for the HiBy R1 audiobook firmware project.

Converted from tools/run_local_dev_sanity.ps1.

This script runs the following checks:
  1. Shell syntax checks (``sh -n``) on resume daemon, DB watcher, refresh script
  2. Python compile checks on all .py files in tools/
  3. Resume daemon logic tests
  4. DB watcher logic tests
  5. Native Linux DB helper fixture (built with ``zig cc``)
  6. QEMU MIPS DB helper fixture (using ``qemu-mipsel-static``)
  7. Git whitespace check

PowerShell parsing checks from the original are dropped (not relevant on Linux).
The Windows DB helper fixture is replaced with a native Linux build.
"""

from __future__ import annotations

import argparse
import logging
import os
import py_compile
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

log = logging.getLogger(__name__)

REPO_ROOT = Path(__file__).resolve().parents[1]

# Shell scripts to syntax-check.
SHELL_SCRIPTS = [
    "tools/r1_audiobook_resume_daemon.sh",
    "tools/r1_audiobook_db_watch.sh",
    "tools/r1_audiobook_refresh.sh",
    "tools/test_r1_resume_daemon_logic.sh",
    "tools/test_r1_db_watch_logic.sh",
]

# Python files to compile-check (relative to repo root).
PYTHON_FILES = [
    "tools/verify_r1_audiobook_build.py",
    "tools/write_audiobook_resume_catalog.py",
    "tools/check_audiobook_release_state.py",
    "tools/compare_binary_settings.py",
    "tools/test_r1_db_maint_local_fixture.py",
    "tools/adb_test_audiobook_launcher_route_variant.py",
    "tools/adb_test_audiobook_launcher_callback.py",
    "tools/adb_test_audiobook_launcher_record.py",
    "tools/adb_test_audiobook_route_table_direct.py",
    "tools/adb_test_audiobook_route_table_matrix.py",
    "tools/adb_probe_route_callback.py",
    "tools/adb_test_audiobook_ui_seek_fallback.py",
    "tools/adb_test_audiobook_seek_restore.py",
    "tools/adb_probe_music_row.py",
    "tools/adb_probe_native_audiobook_hub.py",
    "tools/adb_send_dmr_command.py",
    "tools/generate_audiobook_m3u_views.py",
    "tools/r1_adb_control.py",
    "tools/r1_audiobook_ui_route_lab.py",
    "tools/r1_hiby_player_cave_audit.py",
    "tools/r1_hiby_player_listview_descriptor_report.py",
    "tools/r1_hiby_player_ui_callsite_report.py",
    "tools/r1_hiby_player_static_xrefs.py",
    "tools/generate_audiobook_launcher_icons.py",
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class SanityError(Exception):
    """Raised when a sanity check fails."""


def step(name: str) -> None:
    log.info("")
    log.info("== %s ==", name)


def run_checked(cmd: list[str], name: str, *, cwd: Path | None = None) -> None:
    """Run *cmd* and raise SanityError on non-zero exit."""
    step(name)
    proc = subprocess.run(cmd, cwd=cwd)
    if proc.returncode != 0:
        raise SanityError(f"{name} failed with exit code {proc.returncode}")


def run_checked_text(cmd: list[str], name: str, *, cwd: Path | None = None) -> str:
    """Run *cmd*, capture output, and raise SanityError on non-zero exit."""
    step(name)
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        log.error(proc.stdout)
        log.error(proc.stderr)
        raise SanityError(f"{name} failed with exit code {proc.returncode}")
    return proc.stdout


# ---------------------------------------------------------------------------
# Individual checks
# ---------------------------------------------------------------------------

def check_shell_syntax() -> None:
    """Run ``sh -n`` on each shell script."""
    step("Shell syntax")
    for script in SHELL_SCRIPTS:
        path = REPO_ROOT / script
        if not path.exists():
            raise SanityError(f"Missing shell script: {script}")
        proc = subprocess.run(["sh", "-n", str(path)])
        if proc.returncode != 0:
            raise SanityError(f"Shell syntax error in {script}")
        log.info("OK   %s", script)


def check_python_compile() -> None:
    """Run ``py_compile`` on each Python file."""
    step("Python compile")
    for rel in PYTHON_FILES:
        path = REPO_ROOT / rel
        if not path.exists():
            raise SanityError(f"Missing Python file: {rel}")
        try:
            py_compile.compile(str(path), doraise=True)
        except py_compile.PyCompileError as exc:
            raise SanityError(f"Python compile error in {rel}:\n{exc}") from exc
        log.info("OK   %s", rel)


def check_resume_daemon_logic() -> None:
    """Run the resume daemon logic test script."""
    run_checked(
        ["sh", "tools/test_r1_resume_daemon_logic.sh"],
        "Resume daemon logic",
        cwd=REPO_ROOT,
    )


def check_db_watcher_logic() -> None:
    """Run the DB watcher logic test script."""
    run_checked(
        ["sh", "tools/test_r1_db_watch_logic.sh"],
        "DB watcher logic",
        cwd=REPO_ROOT,
    )


def build_native_helper(zig_cc: str, out_path: Path) -> None:
    """Build a native Linux test helper using ``zig cc``.

    This replaces the Windows DB helper fixture from the PowerShell version.
    The same C source is compiled for x86_64-linux-gnu so it can run directly.
    """
    source = REPO_ROOT / "tools" / "r1_audiobook_db_maint.c"
    if not source.exists():
        raise SanityError(f"Missing DB maint source: {source}")

    deps_sqlite = REPO_ROOT / ".deps" / "sqlite"
    sqlite_dirs = sorted(deps_sqlite.glob("sqlite-amalgamation-*"))
    if not sqlite_dirs:
        raise SanityError(
            f"SQLite amalgamation not found under {deps_sqlite}. "
            "Run the DB maint helper build first to populate .deps/sqlite."
        )
    sqlite_dir = sqlite_dirs[-1]
    sqlite_source = sqlite_dir / "sqlite3.c"

    out_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        zig_cc,
        "-target", "x86_64-linux-gnu",
        "-static",
        "-Os",
        "-s",
        "-I", str(sqlite_dir),
        "-DSQLITE_THREADSAFE=0",
        "-DSQLITE_OMIT_LOAD_EXTENSION",
        "-DSQLITE_DEFAULT_MEMSTATUS=0",
        "-DSQLITE_OMIT_DEPRECATED",
        str(source),
        str(sqlite_source),
        "-o", str(out_path),
    ]
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        raise SanityError(f"Native helper build failed (exit {proc.returncode})")
    log.info("OK   native helper built: %s", out_path)


def check_native_db_fixture(helper: Path) -> None:
    """Run the local DB maint fixture test with a native Linux helper."""
    run_checked(
        [
            sys.executable,
            "tools/test_r1_db_maint_local_fixture.py",
            "--helper", str(helper),
        ],
        "Native Linux DB helper fixture",
        cwd=REPO_ROOT,
    )


def check_qemu_mips_fixture(helper: Path, qemu: str) -> None:
    """Run the DB maint fixture test under QEMU MIPS emulation."""
    if not Path(qemu).exists():
        log.warning("SKIP QEMU MIPS DB helper fixture; %s not found", qemu)
        return
    if not helper.exists():
        log.warning("SKIP QEMU MIPS DB helper fixture; missing %s", helper)
        return

    # Verify QEMU can run the helper at all.
    proc = subprocess.run([qemu, str(helper), "--help"], capture_output=True)
    if proc.returncode != 0:
        raise SanityError(f"QEMU helper --help failed (exit {proc.returncode})")

    run_checked(
        [
            sys.executable,
            "tools/test_r1_db_maint_local_fixture.py",
            "--helper", str(helper),
            "--runner", qemu,
        ],
        "QEMU MIPS DB helper fixture",
        cwd=REPO_ROOT,
    )


def check_git_whitespace() -> None:
    """Run ``git diff --check`` to catch whitespace errors."""
    run_checked(
        ["git", "diff", "--check"],
        "Git diff whitespace",
        cwd=REPO_ROOT,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run local development sanity checks for the HiBy R1 audiobook firmware project.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--skip-db-fixtures",
        action="store_true",
        help="Skip DB helper fixture tests (native and QEMU).",
    )
    parser.add_argument(
        "--mips-helper",
        type=Path,
        default=REPO_ROOT / "work" / "native-db-maint" / "r1_audiobook_db_maint",
        help="Path to the MIPS DB maint helper binary for QEMU testing.",
    )
    parser.add_argument(
        "--qemu",
        default=shutil.which("qemu-mipsel-static") or "/usr/bin/qemu-mipsel-static",
        help="Path to qemu-mipsel-static (default: auto-detect).",
    )
    parser.add_argument(
        "--zig-cc",
        default=str(Path.home() / "tools" / "zig" / "zig"),
        help="Path to the zig executable for native helper builds (default: ~/tools/zig/zig).",
    )
    return parser


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    parser = build_parser()
    args = parser.parse_args()

    try:
        check_shell_syntax()
        check_python_compile()
        check_resume_daemon_logic()
        check_db_watcher_logic()

        if not args.skip_db_fixtures:
            # Build a native Linux test helper and run the fixture test.
            native_helper = REPO_ROOT / "work" / "native-db-maint" / "r1_audiobook_db_maint_linux_test"
            if Path(args.zig_cc).exists():
                step("Build native DB helper")
                build_native_helper(args.zig_cc, native_helper)
                check_native_db_fixture(native_helper)
            else:
                log.warning("SKIP Native Linux DB helper fixture; zig not found at %s", args.zig_cc)

            # QEMU MIPS fixture.
            check_qemu_mips_fixture(args.mips_helper, args.qemu)

        check_git_whitespace()

        log.info("")
        log.info("All local sanity checks passed.")
        return 0

    except SanityError as exc:
        log.error("")
        log.error("SANITY CHECK FAILED: %s", exc)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())