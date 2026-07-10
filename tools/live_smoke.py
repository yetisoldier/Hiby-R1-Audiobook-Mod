#!/usr/bin/env python3
"""Live end-to-end audiobook smoke test on the HiBy R1 via ADB.

Opens Audiobooks, selects a title, verifies playback starts, waits for the
resume save threshold, captures screenshots, and checks resume daemon logs.

Python port of adb_live_audiobook_smoke.ps1.
"""

from __future__ import annotations

import argparse
import datetime
import logging
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

logger = logging.getLogger(__name__)

FALLBACK_ADB = "/home/yetisoldier/.local/bin/adb"
WIDTH = 480
HEIGHT = 800
STRIDE = WIDTH * 2


def resolve_adb(adb_arg: str) -> str:
    """Locate the adb binary."""
    if adb_arg:
        candidate = Path(adb_arg)
        if candidate.exists():
            return str(candidate)
        found = shutil.which(adb_arg)
        if found:
            return found
    found = shutil.which("adb")
    if found:
        return found
    if Path(FALLBACK_ADB).exists():
        return FALLBACK_ADB
    raise RuntimeError(
        "ADB not found. Install platform-tools, add adb to PATH, "
        f"or place adb at {FALLBACK_ADB}."
    )


def require_path(path: Path) -> Path:
    """Resolve and validate that a path exists."""
    if not path.exists():
        raise FileNotFoundError(f"Missing path: {path}")
    return path.resolve()


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    """Run an adb shell command and return stdout."""
    proc = subprocess.run(
        [adb, "shell", command],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0 and check:
        raise RuntimeError(f"adb shell failed: {command}\n{proc.stdout}")
    return proc.stdout


def adb_run(adb: str, args: list[str], *, check: bool = True) -> str:
    """Run an adb subcommand and return stdout."""
    proc = subprocess.run(
        [adb] + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.stdout.strip():
        logger.debug("%s", proc.stdout.rstrip())
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"adb {' '.join(args)} failed with code {proc.returncode}\n{proc.stdout}"
        )
    return proc.stdout


def invoke_control(control_script: Path, control_args: list[str]) -> str:
    """Run r1_adb_control.py with given args and return stdout."""
    cmd = [sys.executable, str(control_script)] + control_args
    logger.info("Running: %s", " ".join(cmd))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"r1_adb_control.py failed: {' '.join(control_args)}\n{proc.stdout}"
        )
    return proc.stdout


def get_remote_byte_count(adb: str, remote_path: str) -> int:
    """Get byte count of a remote file."""
    text = adb_shell(adb, f"wc -c < '{remote_path}' 2>/dev/null || echo 0", check=False)
    match = re.search(r"(\d+)", text)
    return int(match.group(1)) if match else 0


def capture_smoke_screen(
    control_script: Path,
    adb: str,
    smoke_dir: Path,
    name: str,
    expected_state: str = "",
) -> str:
    """Capture a screenshot via r1_adb_control.py and return classified state."""
    png_path = smoke_dir / f"{name}.png"
    output = invoke_control(
        control_script,
        ["screenshot", "--adb", adb, "--output", str(png_path), "--label", name, "--classify"],
    )
    (smoke_dir / f"{name}.capture.txt").write_text(output, encoding="utf-8")

    state = ""
    for line in output.splitlines():
        match = re.match(r"^state:\s+(.+)$", line)
        if match:
            state = match.group(1).strip()
            break

    if expected_state and state != expected_state:
        raise RuntimeError(f"{name} expected screen state [{expected_state}], got [{state}]")

    logger.info("OK   captured %s", name)
    if state:
        logger.info("OK   %s screen state: %s", name, state)
    return state


def get_smoke_raw_path(smoke_dir: Path, name: str) -> Path:
    """Get the raw framebuffer path for a named capture."""
    return smoke_dir / f"{name}.raw"


def count_rgb565_white_pixels_region(
    raw_path: Path,
    x0: int,
    y0: int,
    x1: int,
    y1: int,
    step: int = 1,
) -> int:
    """Count white-ish RGB565 pixels in a region of a raw framebuffer."""
    if not raw_path.exists():
        raise RuntimeError(f"Missing raw framebuffer capture: {raw_path}")
    raw = raw_path.read_bytes()
    count = 0
    for y in range(max(0, y0), min(HEIGHT, y1), max(1, step)):
        row = y * STRIDE
        for x in range(max(0, x0), min(WIDTH, x1), max(1, step)):
            offset = row + x * 2
            if offset + 1 >= len(raw):
                continue
            value = raw[offset] | (raw[offset + 1] << 8)
            r = (value >> 11) & 0x1F
            g = (value >> 5) & 0x3F
            b = value & 0x1F
            if r >= 24 and g >= 48 and b >= 24:
                count += 1
    return count


def test_audiobook_title_list_screen(raw_path: Path) -> bool:
    """Check if a raw framebuffer matches the audiobook title-list screen signature."""
    subheader = count_rgb565_white_pixels_region(raw_path, 60, 118, 220, 155, 1)
    header_mid = count_rgb565_white_pixels_region(raw_path, 170, 70, 260, 110, 1)
    header_icon = count_rgb565_white_pixels_region(raw_path, 400, 75, 440, 110, 1)
    summary = f"subheader={subheader} header_mid={header_mid} header_icon={header_icon}"
    metrics_path = Path(str(raw_path) + ".audiobook-title-metrics.txt")
    metrics_path.write_text(summary, encoding="utf-8")
    logger.info("Audiobook title-list metrics: %s", summary)
    return subheader >= 120 and header_mid <= 120 and header_icon >= 300


def assert_one_matching_line(text: str, pattern: str, label: str) -> None:
    """Assert exactly one line in text matches a regex pattern."""
    matches = [line for line in text.splitlines() if re.search(pattern, line)]
    if len(matches) != 1:
        raise RuntimeError(f"{label} expected exactly one match for {pattern}, found {len(matches)}")
    logger.info("OK   %s has one process root", label)


def live_smoke(args: argparse.Namespace) -> None:
    if not (1 <= args.title_row <= 5):
        raise RuntimeError("title_row must be between 1 and 5")

    adb = resolve_adb(args.adb)
    repo_root = Path(__file__).resolve().parents[1]
    control_script = require_path(Path(args.control_script))

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    smoke_dir = Path(args.out_dir) / stamp
    smoke_dir.mkdir(parents=True, exist_ok=True)

    # Check device
    adb_output = adb_run(adb, ["devices"])
    (smoke_dir / "adb_devices.txt").write_text(adb_output, encoding="utf-8")

    # Capture version and initial daemon log
    version_text = adb_shell(adb, "cat /etc/r1_audiobook_version 2>/dev/null || true", check=False)
    (smoke_dir / "r1_audiobook_version.txt").write_text(version_text, encoding="utf-8")

    log_before_bytes = get_remote_byte_count(adb, "/usr/data/audiobooks/resume-daemon.log")
    (smoke_dir / "resume-daemon-before.bytes.txt").write_text(str(log_before_bytes), encoding="utf-8")
    log_before = adb_shell(adb, "tail -n 120 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true", check=False)
    (smoke_dir / "resume-daemon-before.log").write_text(log_before, encoding="utf-8")

    # Initial screenshot
    current_state = capture_smoke_screen(control_script, adb, smoke_dir, "00-before")

    # Reset backs
    for i in range(1, args.reset_backs + 1):
        logger.info("Sending edge-back reset %d/%d...", i, args.reset_backs)
        back_output = invoke_control(control_script, ["back", "--adb", adb])
        (smoke_dir / f"00-reset-back-{i}.txt").write_text(back_output, encoding="utf-8")
        time.sleep(args.back_settle_seconds)
        current_state = capture_smoke_screen(control_script, adb, smoke_dir, f"00-reset-back-{i}")

    # Open Audiobooks
    if not args.skip_open_audiobooks:
        if current_state != "launcher":
            raise RuntimeError(
                f"Opening Audiobooks requires launcher state; current state is [{current_state}]. "
                "Increase --reset-backs or start from the main menu."
            )
        max_open_attempts = 1 + max(0, args.audiobooks_open_retries)
        screen_name = ""
        for attempt in range(1, max_open_attempts + 1):
            if attempt == 1:
                logger.info("Opening Audiobooks...")
                open_name = "01-open-audiobooks"
                screen_name = "01-audiobooks"
            else:
                logger.info("Retrying Audiobooks open %d/%d after state [%s]...", attempt, max_open_attempts, current_state)
                open_name = f"01-open-audiobooks-retry-{attempt}"
                screen_name = f"01-audiobooks-retry-{attempt}"
            open_output = invoke_control(
                control_script,
                ["preset", "--adb", adb, "--frames", str(args.tap_frames), "main-audiobooks"],
            )
            (smoke_dir / f"{open_name}.txt").write_text(open_output, encoding="utf-8")
            time.sleep(args.audiobooks_settle_seconds)
            current_state = capture_smoke_screen(control_script, adb, smoke_dir, screen_name)
            if current_state == "list":
                break
            if current_state != "launcher":
                break

        if current_state != "list":
            raise RuntimeError(f"Opening Audiobooks expected screen state [list], got [{current_state}]")

        if not args.skip_audiobook_title_list_check:
            raw_path = get_smoke_raw_path(smoke_dir, screen_name)
            if not test_audiobook_title_list_screen(raw_path):
                raise RuntimeError(
                    "Opening Audiobooks reached a list, but it does not match the audiobook title-list signature. "
                    "It may have opened global Music Genres instead."
                )
            logger.info("OK   Audiobooks opened the audiobook title-list screen")

    # Start title
    playback_assertion_passed = False
    if not args.skip_start_title:
        if current_state != "list":
            raise RuntimeError(f"Starting a title requires list state; current state is [{current_state}]")
        logger.info("Starting title row %d...", args.title_row)
        row_output = invoke_control(
            control_script,
            ["row", "--adb", adb, "--frames", str(args.tap_frames), str(args.title_row)],
        )
        (smoke_dir / "02-title-row.txt").write_text(row_output, encoding="utf-8")
        time.sleep(args.start_settle_seconds)
        title_tap_state = capture_smoke_screen(control_script, adb, smoke_dir, "02-after-title-tap")
        if title_tap_state != "now-playing":
            logger.info("Retrying title row %d after state [%s]...", args.title_row, title_tap_state)
            row_retry_output = invoke_control(
                control_script,
                ["row", "--adb", adb, "--frames", str(args.tap_frames), str(args.title_row)],
            )
            (smoke_dir / "02-title-row-retry.txt").write_text(row_retry_output, encoding="utf-8")
            time.sleep(args.start_settle_seconds)
            capture_smoke_screen(control_script, adb, smoke_dir, "02-after-title-retry", "now-playing")

        logger.info("Waiting %d seconds for resume save threshold...", args.playback_seconds)
        time.sleep(args.playback_seconds)
        capture_smoke_screen(control_script, adb, smoke_dir, "03-after-playback", "now-playing")

    # Check daemon logs
    log_after = adb_shell(adb, "tail -n 180 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true", check=False)
    (smoke_dir / "resume-daemon-after.log").write_text(log_after, encoding="utf-8")
    log_after_bytes = get_remote_byte_count(adb, "/usr/data/audiobooks/resume-daemon.log")

    if log_after_bytes >= log_before_bytes:
        log_new = adb_shell(
            adb,
            f"dd if=/usr/data/audiobooks/resume-daemon.log bs=1 skip={log_before_bytes} 2>/dev/null || true",
            check=False,
        )
    else:
        log_new = log_after
    (smoke_dir / "resume-daemon-new.log").write_text(log_new, encoding="utf-8")

    if not args.skip_start_title:
        if re.search(r"leave audiobook current=non-audiobook|audiobook path=a:\\Music\\", log_new):
            raise RuntimeError("resume daemon log shows non-audiobook playback during smoke test")
        if not re.search(r"audiobook path=a:\\Audiobooks\\|restore path=a:\\Audiobooks\\", log_new):
            raise RuntimeError("resume daemon log did not show audiobook playback or a fresh audiobook restore response")
        if not re.search(r"saves=[1-9]|restore path=|after_position_response=", log_new):
            raise RuntimeError("resume daemon log did not show a restore response or saved progress")
        playback_assertion_passed = True
        logger.info("OK   resume daemon log shows audiobook playback/resume activity")
    else:
        logger.info("OK   skipped playback/resume log assertion")

    # Check processes
    process_roots = invoke_control(
        control_script,
        ["processes", "--adb", adb, "--top-level-only",
         "--pattern", "r1_audiobook_(resume_daemon|db_watch)|hiby_player"],
    )
    (smoke_dir / "process-roots.txt").write_text(process_roots, encoding="utf-8")
    assert_one_matching_line(process_roots, "r1_audiobook_resume_daemon", "resume daemon")
    assert_one_matching_line(process_roots, "r1_audiobook_db_watch", "DB watcher")

    # Pause after if requested
    if not args.no_pause_after and playback_assertion_passed:
        logger.info("Pausing playback...")
        pause_output = invoke_control(control_script, ["key", "--adb", adb, "playpause"])
        (smoke_dir / "04-pause.txt").write_text(pause_output, encoding="utf-8")
        time.sleep(1)
        capture_smoke_screen(control_script, adb, smoke_dir, "04-after-pause", "now-playing")

    logger.info("")
    logger.info("Live audiobook smoke test passed.")
    logger.info("Artifacts: %s", smoke_dir)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Live end-to-end audiobook smoke test on the HiBy R1 via ADB."
    )
    parser.add_argument("--adb", default="", help="Path to adb binary.")
    parser.add_argument("--control-script", default="tools/r1_adb_control.py", help="Path to r1_adb_control.py.")
    parser.add_argument("--out-dir", default="work/live-audiobook-smoke", help="Output directory for smoke test artifacts.")
    parser.add_argument("--title-row", type=int, default=1, help="Audiobook title row to tap (1-5).")
    parser.add_argument("--audiobooks-settle-seconds", type=int, default=10, help="Settle time after opening Audiobooks.")
    parser.add_argument("--start-settle-seconds", type=int, default=12, help="Settle time after tapping a title.")
    parser.add_argument("--playback-seconds", type=int, default=22, help="Playback time before post-play check.")
    parser.add_argument("--tap-frames", type=int, default=36, help="Touch event frames for taps.")
    parser.add_argument("--audiobooks-open-retries", type=int, default=2, help="Retries for opening Audiobooks.")
    parser.add_argument("--reset-backs", type=int, default=0, help="Edge-back resets before opening Audiobooks.")
    parser.add_argument("--back-settle-seconds", type=int, default=2, help="Settle time after edge-back.")
    parser.add_argument("--skip-open-audiobooks", action="store_true", help="Skip opening Audiobooks.")
    parser.add_argument("--skip-start-title", action="store_true", help="Skip starting a title.")
    parser.add_argument("--skip-audiobook-title-list-check", action="store_true", help="Skip audiobook title-list screen verification.")
    parser.add_argument("--no-pause-after", action="store_true", help="Skip pausing playback at the end.")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        live_smoke(args)
    except Exception as exc:
        logger.error("error: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())