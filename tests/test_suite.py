#!/usr/bin/env python3
"""Automated regression test suite for the HiBy R1 Audiobook Firmware Mod.

Runs on the host and drives the R1 via ADB.  Uses r1_adb_control.py for UI
interaction, adb_capture_fb0.py for screenshots, and check_audiobook_release_state.py
for DB validation.  The suite is read-only — it does not flash firmware or
modify device state beyond normal playback.

Python stdlib only; no pytest or unittest framework.

Usage::

    python3 tests/test_suite.py --help
    python3 tests/test_suite.py --suite smoke
    python3 tests/test_suite.py --suite full --json-report report.json
"""

from __future__ import annotations

import argparse
import datetime
import importlib
import json
import os
import shutil
import subprocess
import sys
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

# ── constants ────────────────────────────────────────────────────────────────

DEFAULT_ADB = ""
DEFAULT_TIMEOUT = 300          # per-test timeout in seconds
DEFAULT_TAP_FRAMES = 12
DEFAULT_SETTLE = 3              # seconds to wait after UI actions
DEFAULT_SCREENSHOT_DIR = "work/test-screenshots"
SUITE_SMOKE_TESTS = ["test_launcher", "test_playback", "test_resume"]
SUITE_FULL_TESTS = [
    "test_launcher",
    "test_playback",
    "test_resume",
    "test_book_switch",
    "test_music_idle",
    "test_db_maintenance",
    "test_play_mode",
    "test_navigation",
]

# ANSI colour codes
C_RESET = "\033[0m"
C_GREEN = "\033[32m"
C_RED = "\033[31m"
C_YELLOW = "\033[33m"
C_CYAN = "\033[36m"
C_BOLD = "\033[1m"
C_DIM = "\033[2m"

# ── helpers ──────────────────────────────────────────────────────────────────

def supports_color() -> bool:
    """Detect whether stdout supports ANSI colour."""
    if not sys.stdout.isatty():
        return False
    term = os.environ.get("TERM", "")
    return term in ("xterm", "xterm-256color", "screen", "screen-256color", "tmux", "tmux-256color")


def color(text: str, code: str) -> str:
    if supports_color():
        return f"{code}{text}{C_RESET}"
    return text


def timestamp() -> str:
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def resolve_adb(adb_arg: str) -> str:
    """Locate the adb binary."""
    if adb_arg:
        candidate = Path(adb_arg)
        if candidate.exists():
            return str(candidate)
        # If the user explicitly gave a path that doesn't exist, fail
        raise RuntimeError(
            f"ADB not found at: {adb_arg}"
        )
    found = shutil.which("adb")
    if found:
        return found
    fallback = Path.home() / ".local" / "bin" / "adb"
    if fallback.exists():
        return str(fallback)
    raise RuntimeError(
        "ADB not found. Install platform-tools, add adb to PATH, "
        f"or use --adb /path/to/adb."
    )


def adb_shell(adb: str, command: str, *, check: bool = False, timeout: int = 30) -> str:
    """Run an adb shell command and return stdout."""
    proc = subprocess.run(
        [adb, "shell", command],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(f"adb shell failed: {command}\n{proc.stdout}")
    return proc.stdout


def adb_run(adb: str, args: list[str], *, check: bool = False, timeout: int = 30) -> str:
    """Run an adb subcommand and return stdout."""
    proc = subprocess.run(
        [adb] + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"adb {' '.join(args)} failed with code {proc.returncode}\n{proc.stdout}"
        )
    return proc.stdout


def check_device(adb: str) -> bool:
    """Return True if at least one ADB device is connected and authorized."""
    try:
        output = adb_run(adb, ["devices", "-l"], timeout=10)
    except (subprocess.TimeoutExpired, FileNotFoundError, Exception):
        return False
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            return True
    return False


# ── test context ────────────────────────────────────────────────────────────

@dataclass
class TestContext:
    """Shared context passed to every test case."""
    adb: str
    repo_root: Path
    control_script: Path
    check_script: Path
    capture_script: Path
    screenshot_dir: Path
    tap_frames: int = DEFAULT_TAP_FRAMES
    settle: float = DEFAULT_SETTLE
    timeout: int = DEFAULT_TIMEOUT
    verbose: bool = False
    artifacts: Path = field(default_factory=lambda: Path("work/test-artifacts"))

    def screenshot(self, label: str) -> Path:
        """Capture a screenshot via r1_adb_control.py and return the PNG path."""
        png = self.screenshot_dir / f"{timestamp()}-{label}.png"
        png.parent.mkdir(parents=True, exist_ok=True)
        invoke_control(self, ["screenshot", "--adb", self.adb,
                              "--output", str(png), "--label", label, "--classify"])
        return png

    def tap(self, preset: str) -> str:
        """Tap a named preset via r1_adb_control.py."""
        return invoke_control(self, ["preset", "--adb", self.adb,
                                      "--frames", str(self.tap_frames), preset])

    def tap_point(self, x: int, y: int) -> str:
        """Tap raw coordinates via r1_adb_control.py."""
        return invoke_control(self, ["tap", "--adb", self.adb,
                                      "--frames", str(self.tap_frames), str(x), str(y)])

    def key(self, name: str) -> str:
        """Press a named key via r1_adb_control.py."""
        return invoke_control(self, ["key", "--adb", self.adb, name])

    def back(self) -> str:
        """Send the edge-back gesture via r1_adb_control.py."""
        return invoke_control(self, ["back", "--adb", self.adb,
                                      "--frames", "18"])

    def row(self, number: int, kind: str = "title") -> str:
        """Tap a visible list row via r1_adb_control.py."""
        return invoke_control(self, ["row", "--adb", self.adb,
                                      "--frames", str(self.tap_frames),
                                      str(number), "--kind", kind])

    def shell(self, command: str, *, check: bool = False) -> str:
        """Run an ADB shell command."""
        return adb_shell(self.adb, command, check=check)

    def sleep(self, seconds: float) -> None:
        """Sleep, logging if verbose."""
        if self.verbose:
            print(color(f"  …waiting {seconds}s", C_DIM))
        time.sleep(seconds)


def invoke_control(ctx: TestContext, control_args: list[str]) -> str:
    """Run r1_adb_control.py with given args and return stdout."""
    cmd = [sys.executable, str(ctx.control_script)] + control_args
    if ctx.verbose:
        print(color(f"  $ {' '.join(cmd)}", C_DIM))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"r1_adb_control.py failed: {' '.join(control_args)}\n{proc.stdout}"
        )
    return proc.stdout


def invoke_script(ctx: TestContext, script: Path, script_args: list[str]) -> str:
    """Run a Python script with given args and return stdout."""
    cmd = [sys.executable, str(script)] + script_args
    if ctx.verbose:
        print(color(f"  $ {' '.join(cmd)}", C_DIM))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )
    return proc.stdout


# ── test result ─────────────────────────────────────────────────────────────

@dataclass
class TestResult:
    name: str
    status: str           # PASS, WARN, FAIL, SKIP
    duration: float = 0.0
    message: str = ""
    warnings: list[str] = field(default_factory=list)
    screenshots: list[str] = field(default_factory=list)
    error: str = ""


# ── test runner ──────────────────────────────────────────────────────────────

class TestRunner:
    """Lightweight test runner with discovery, retry, and reporting."""

    def __init__(self, ctx: TestContext, *,
                 suite: str = "all",
                 stop_on_fail: bool = False,
                 retry: int = 0,
                 json_report: Path | None = None):
        self.ctx = ctx
        self.suite = suite
        self.stop_on_fail = stop_on_fail
        self.retry = retry
        self.json_report = json_report
        self.results: list[TestResult] = []

    def _select_tests(self) -> list[str]:
        if self.suite == "smoke":
            return list(SUITE_SMOKE_TESTS)
        elif self.suite == "full":
            return list(SUITE_FULL_TESTS)
        elif self.suite == "all":
            return list(SUITE_FULL_TESTS)
        else:
            # Allow comma-separated explicit list
            return [t.strip() for t in self.suite.split(",") if t.strip()]

    def _load_test(self, name: str) -> Callable[[TestContext], None]:
        """Dynamically import a test case module and return its run function."""
        module = importlib.import_module(f"test_cases.{name}")
        if not hasattr(module, "run"):
            raise RuntimeError(f"test_cases.{name} has no run() function")
        return module.run

    def run_all(self) -> int:
        tests = self._select_tests()
        if not tests:
            print(color("No tests selected.", C_YELLOW))
            return 1

        print()
        print(color(f"{'═' * 60}", C_CYAN))
        print(color(f"  HiBy R1 Audiobook Regression Test Suite", C_BOLD))
        print(color(f"  Suite: {self.suite}  |  Tests: {len(tests)}  |  Retry: {self.retry}", C_DIM))
        print(color(f"{'═' * 60}", C_CYAN))
        print()

        # Check device connectivity once up front
        if not check_device(self.ctx.adb):
            print(color("⚠  No ADB device connected — all tests will be SKIP.", C_YELLOW))
            print()
            for name in tests:
                result = TestResult(name=name, status="SKIP",
                                    message="No ADB device connected")
                self.results.append(result)
                self._print_result(result)
        else:
            for name in tests:
                result = self._run_one(name)
                self.results.append(result)
                self._print_result(result)
                if result.status == "FAIL" and self.stop_on_fail:
                    print(color("\n⏹  Stop-on-fail: skipping remaining tests.", C_YELLOW))
                    break

        self._print_summary()
        self._write_json_report()
        failed = sum(1 for r in self.results if r.status == "FAIL")
        return 1 if failed else 0

    def _run_one(self, name: str) -> TestResult:
        """Run a single test with retries."""
        for attempt in range(self.retry + 1):
            result = TestResult(name=name, status="PASS")
            start = time.time()
            try:
                if self.ctx.verbose:
                    print(color(f"  [attempt {attempt + 1}/{self.retry + 1}]", C_DIM))
                test_fn = self._load_test(name)
                test_fn(self.ctx)
                result.duration = time.time() - start
                result.status = "PASS"
                if attempt > 0:
                    result.message = f"passed on retry {attempt + 1}"
                return result
            except SkipTest as exc:
                result.duration = time.time() - start
                result.status = "SKIP"
                result.message = str(exc)
                return result
            except WarningOnly as exc:
                result.duration = time.time() - start
                result.status = "WARN"
                result.message = str(exc)
                return result
            except Exception as exc:
                result.duration = time.time() - start
                result.status = "FAIL"
                result.error = f"{exc}\n{traceback.format_exc()}"
                if attempt < self.retry:
                    if self.ctx.verbose:
                        print(color(f"  retrying ({attempt + 2}/{self.retry + 1})…", C_YELLOW))
                    time.sleep(2)
                    continue
                return result
        return result

    def _print_result(self, result: TestResult) -> None:
        status_str = {
            "PASS": color(" PASS ", C_GREEN),
            "WARN": color(" WARN ", C_YELLOW),
            "FAIL": color(" FAIL ", C_RED),
            "SKIP": color(" SKIP ", C_DIM),
        }.get(result.status, result.status)
        duration_str = f"{result.duration:.1f}s" if result.duration else "-"
        print(f"  {status_str}  {result.name:<25}  {duration_str:>6}")
        if result.message:
            print(color(f"         {result.message}", C_DIM))
        if result.error and self.ctx.verbose:
            for line in result.error.splitlines()[-5:]:
                print(color(f"         {line}", C_RED))
        print()

    def _print_summary(self) -> None:
        passed = sum(1 for r in self.results if r.status == "PASS")
        warned = sum(1 for r in self.results if r.status == "WARN")
        failed = sum(1 for r in self.results if r.status == "FAIL")
        skipped = sum(1 for r in self.results if r.status == "SKIP")
        total = len(self.results)

        print()
        print(color(f"{'═' * 60}", C_CYAN))
        summary = f"  {passed}/{total} passed"
        if warned:
            summary += f", {warned} warnings"
        if failed:
            summary += f", {failed} failures"
        if skipped:
            summary += f", {skipped} skipped"
        print(color(summary, C_BOLD))
        if failed:
            print(color("  FAILED:", C_RED))
            for r in self.results:
                if r.status == "FAIL":
                    print(f"    - {r.name}")
        print(color(f"{'═' * 60}", C_CYAN))
        print()

    def _write_json_report(self) -> None:
        if not self.json_report:
            return
        report = {
            "timestamp": timestamp(),
            "suite": self.suite,
            "results": [
                {
                    "name": r.name,
                    "status": r.status,
                    "duration": round(r.duration, 3),
                    "message": r.message,
                    "error": r.error,
                }
                for r in self.results
            ],
            "summary": {
                "total": len(self.results),
                "passed": sum(1 for r in self.results if r.status == "PASS"),
                "warned": sum(1 for r in self.results if r.status == "WARN"),
                "failed": sum(1 for r in self.results if r.status == "FAIL"),
                "skipped": sum(1 for r in self.results if r.status == "SKIP"),
            },
        }
        self.json_report.parent.mkdir(parents=True, exist_ok=True)
        self.json_report.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(color(f"  JSON report written to {self.json_report}", C_DIM))


# ── custom exceptions ────────────────────────────────────────────────────────

class SkipTest(Exception):
    """Raise to skip a test with a message."""
    pass


class WarningOnly(Exception):
    """Raise to mark a test as WARN instead of FAIL."""
    pass


# ── screen classification helper ─────────────────────────────────────────────

def classify_screen(ctx: TestContext, label: str) -> str:
    """Capture and classify the current screen, returning the state string."""
    output = invoke_control(ctx, ["classify", "--adb", ctx.adb, "--label", label])
    for line in output.splitlines():
        if line.startswith("state:"):
            return line.split(":", 1)[1].strip()
    return "unknown"


def goto_launcher(ctx: TestContext, max_backs: int = 10) -> str:
    """Navigate back to the launcher by sending edge-back gestures."""
    for _ in range(max_backs):
        state = classify_screen(ctx, "goto-launcher-check")
        if state == "launcher":
            return state
        ctx.back()
        ctx.sleep(ctx.settle + 1)  # extra settle for deep back stacks
    state = classify_screen(ctx, "goto-launcher-final")
    return state


def cleanup(ctx: TestContext) -> None:
    """Pause playback and return to launcher. Best-effort, never raises."""
    try:
        ctx.key("playpause")
        ctx.sleep(1)
    except Exception:
        pass
    try:
        goto_launcher(ctx)
    except Exception:
        pass


# ── main ────────────────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Automated regression test suite for the HiBy R1 Audiobook Firmware Mod."
    )
    parser.add_argument("--adb", default=DEFAULT_ADB,
                        help="Path to adb binary (default: search PATH).")
    parser.add_argument("--device", default="",
                        help="Target device serial (default: first device).")
    parser.add_argument("--suite", default="all",
                        choices=["all", "smoke", "full"],
                        help="Test selection: smoke = launcher+playback+resume, "
                             "full = all tests.")
    parser.add_argument("--screenshot-dir", default=DEFAULT_SCREENSHOT_DIR,
                        help="Directory for screenshot artifacts.")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT,
                        help="Per-test timeout in seconds.")
    parser.add_argument("--verbose", action="store_true",
                        help="Detailed output.")
    parser.add_argument("--json-report", type=Path, default=None,
                        help="Write machine-readable JSON report to PATH.")
    parser.add_argument("--stop-on-fail", action="store_true",
                        help="Stop after first failure.")
    parser.add_argument("--retry", type=int, default=0,
                        help="Retry failed tests N times before marking FAIL.")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    # Resolve paths
    repo_root = Path(__file__).resolve().parents[1]
    control_script = repo_root / "tools" / "r1_adb_control.py"
    check_script = repo_root / "tools" / "check_audiobook_release_state.py"
    capture_script = repo_root / "tools" / "adb_capture_fb0.py"

    if not control_script.exists():
        print(color(f"error: control script not found: {control_script}", C_RED),
              file=sys.stderr)
        return 1

    # Make test_cases importable
    sys.path.insert(0, str(Path(__file__).resolve().parent))

    # Resolve ADB
    try:
        adb = resolve_adb(args.adb)
    except RuntimeError as exc:
        # ADB not found — we can still show help and skip
        if not check_device_safe(args.adb):
            print(color(f"⚠  {exc}", C_YELLOW))
            print(color("  All tests will be SKIP.", C_YELLOW))
            adb = args.adb or "adb"
        else:
            adb = args.adb

    # Handle --device serial
    if args.device:
        adb_with_device = [adb, "-s", args.device]
        adb_base = adb
        # We need to wrap adb calls to include -s; but our helpers use adb as a
        # string.  For simplicity, if --device is set we prepend it in the
        # shell commands by using the full adb path with -s.
        # Most tools accept --adb as a single binary path, so we just warn.
        print(color(f"  Note: --device {args.device} specified. "
                    f"Ensure only one device is connected or ADB selects it.", C_DIM))

    screenshot_dir = Path(args.screenshot_dir) / timestamp()
    screenshot_dir.mkdir(parents=True, exist_ok=True)

    ctx = TestContext(
        adb=adb,
        repo_root=repo_root,
        control_script=control_script,
        check_script=check_script,
        capture_script=capture_script,
        screenshot_dir=screenshot_dir,
        timeout=args.timeout,
        verbose=args.verbose,
        artifacts=Path("work/test-artifacts") / timestamp(),
    )
    ctx.artifacts.mkdir(parents=True, exist_ok=True)

    runner = TestRunner(
        ctx,
        suite=args.suite,
        stop_on_fail=args.stop_on_fail,
        retry=args.retry,
        json_report=args.json_report,
    )
    return runner.run_all()


def check_device_safe(adb: str) -> bool:
    """Best-effort device check that never raises."""
    try:
        resolved = resolve_adb(adb)
        return check_device(resolved)
    except (Exception, subprocess.TimeoutExpired):
        return False


if __name__ == "__main__":
    raise SystemExit(main())