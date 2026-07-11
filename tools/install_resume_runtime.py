#!/usr/bin/env python3
"""Install audiobook resume daemon runtime files to the HiBy R1 via ADB.

Pushes the resume helper, memscan helper, direct-open helper, daemon script,
catalog, and generated touch/key event streams to the device, then starts the
resume daemon with configurable environment variables.

Python port of adb_install_audiobook_resume_runtime.ps1.
"""

from __future__ import annotations

import argparse
import logging
import shutil
import subprocess
import sys
import time
from pathlib import Path

logger = logging.getLogger(__name__)

FALLBACK_ADB = "/home/yetisoldier/.local/bin/adb"
REMOTE_BASE_DEFAULT = "/usr/data/audiobooks"
WIDTH = 480
HEIGHT = 800


# ---------------------------------------------------------------------------
# ADB helpers
# ---------------------------------------------------------------------------

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


def adb_run(adb: str, args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    """Run an adb subcommand and return the completed process."""
    cmd = [adb] + args
    logger.debug("adb %s", " ".join(args))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.stdout.strip():
        logger.info("%s", proc.stdout.rstrip())
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"adb {' '.join(args)} failed with code {proc.returncode}\n{proc.stdout}"
        )
    return proc


def adb_shell(adb: str, command: str, *, check: bool = True) -> str:
    """Run an adb shell command and return stdout."""
    return adb_run(adb, ["shell", command], check=check).stdout


def require_path(path: Path) -> Path:
    """Resolve and validate that a local path exists."""
    if not path.exists():
        raise FileNotFoundError(f"Missing path: {path}")
    return path.resolve()


def quote_remote(value: str) -> str:
    """Single-quote a remote shell string."""
    return "'" + value.replace("'", "'\"'\"'") + "'"


# ---------------------------------------------------------------------------
# Touch event generation
# ---------------------------------------------------------------------------

def generate_touch_event(
    adb: str,
    touch_tool: Path,
    output: Path,
    *,
    x: int | None = None,
    y: int | None = None,
    to_x: int | None = None,
    to_y: int | None = None,
    touch_phase: str | None = None,
    button: str | None = None,
    drag_frames: int | None = None,
) -> None:
    """Call adb_inject_touch_event.py to generate an event stream."""
    cmd = [sys.executable, str(touch_tool), "--output", str(output)]
    if x is not None:
        cmd += ["--x", str(x)]
    if y is not None:
        cmd += ["--y", str(y)]
    if to_x is not None:
        cmd += ["--to-x", str(to_x)]
    if to_y is not None:
        cmd += ["--to-y", str(to_y)]
    if touch_phase is not None:
        cmd += ["--touch-phase", touch_phase]
    if button is not None:
        cmd += ["--button", button]
    if drag_frames is not None:
        cmd += ["--drag-frames", str(drag_frames)]
    logger.info("Generating touch event: %s", output)
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.stdout.strip():
        logger.debug("%s", proc.stdout.rstrip())
    if proc.returncode != 0:
        raise RuntimeError(
            f"failed to generate touch event stream {output}: {proc.stdout}"
        )


# ---------------------------------------------------------------------------
# Main install logic
# ---------------------------------------------------------------------------

def install_runtime(args: argparse.Namespace) -> None:
    adb = resolve_adb(args.adb)
    repo_root = Path(__file__).resolve().parents[1]
    touch_tool = repo_root / "tools" / "adb_inject_touch_event.py"

    # Validate source paths
    helper_path = require_path(Path(args.helper_source))
    memscan_path = require_path(Path(args.memscan_helper_source))
    direct_open_path = require_path(Path(args.direct_open_helper_source))
    daemon_binary_path = require_path(Path(args.daemon_source))
    daemon_wrapper_path = require_path(Path(args.daemon_wrapper_source))
    daemon_shell_path = require_path(Path(args.daemon_shell_source))

    catalog_path: Path | None = None
    if args.catalog_source:
        catalog_path = require_path(Path(args.catalog_source))

    # Prepare touch event output directory
    touch_dir = repo_root / "work" / "r1-touch-events"
    touch_dir.mkdir(parents=True, exist_ok=True)

    # Generate or resolve touch-next event
    touch_next_path: Path
    if args.touch_next_event_source:
        touch_next_path = require_path(Path(args.touch_next_event_source))
    else:
        generated = touch_dir / "touch_next_event1.bin"
        generate_touch_event(adb, touch_tool, generated)
        touch_next_path = require_path(generated)

    # Generate or resolve touch-first-track event
    touch_first_track_path: Path
    if args.touch_first_track_event_source:
        touch_first_track_path = require_path(Path(args.touch_first_track_event_source))
    else:
        generated = touch_dir / "touch_first_track_event1.bin"
        generate_touch_event(adb, touch_tool, generated, x=203, y=197)
        touch_first_track_path = require_path(generated)

    # Generate first-track phase events
    touch_first_track_down = touch_dir / "touch_first_track_down_event1.bin"
    generate_touch_event(adb, touch_tool, touch_first_track_down, x=203, y=197, touch_phase="down")
    touch_first_track_down_path = require_path(touch_first_track_down)

    touch_first_track_move = touch_dir / "touch_first_track_move_event1.bin"
    generate_touch_event(adb, touch_tool, touch_first_track_move, x=203, y=197, touch_phase="move")
    touch_first_track_move_path = require_path(touch_first_track_move)

    touch_first_track_up = touch_dir / "touch_first_track_up_event1.bin"
    generate_touch_event(adb, touch_tool, touch_first_track_up, x=203, y=197, touch_phase="up")
    touch_first_track_up_path = require_path(touch_first_track_up)

    # Generate back event
    touch_back = touch_dir / "touch_back_event1.bin"
    generate_touch_event(
        adb, touch_tool, touch_back, x=30, y=400, to_x=360, to_y=400, drag_frames=18
    )
    touch_back_path = require_path(touch_back)

    # Generate track-row events
    track_row_specs = [
        ("touch_track_row2_event1.bin", 203, 325),
        ("touch_track_row3_event1.bin", 203, 453),
        ("touch_track_row4_event1.bin", 203, 581),
        ("touch_track_row5_event1.bin", 203, 745),
    ]
    touch_track_row_paths = [touch_first_track_path]
    for name, x, y in track_row_specs:
        generated = touch_dir / name
        generate_touch_event(adb, touch_tool, generated, x=x, y=y)
        touch_track_row_paths.append(require_path(generated))

    # Generate swipe phase events
    swipe_phase_specs = [
        ("touch_track_swipe_down_event1.bin", 120, 680, "down"),
        ("touch_track_swipe_move1_event1.bin", 120, 600, "move"),
        ("touch_track_swipe_move2_event1.bin", 120, 520, "move"),
        ("touch_track_swipe_move3_event1.bin", 120, 440, "move"),
        ("touch_track_swipe_move4_event1.bin", 120, 360, "move"),
        ("touch_track_swipe_move5_event1.bin", 120, 280, "move"),
        ("touch_track_swipe_move6_event1.bin", 120, 220, "move"),
        ("touch_track_swipe_up_event1.bin", 120, 220, "up"),
    ]
    touch_track_swipe_paths: list[Path] = []
    for name, x, y, phase in swipe_phase_specs:
        generated = touch_dir / name
        generate_touch_event(adb, touch_tool, generated, x=x, y=y, touch_phase=phase)
        touch_track_swipe_paths.append(require_path(generated))

    # Generate key events
    key_next_path: Path
    if args.key_next_event_source:
        key_next_path = require_path(Path(args.key_next_event_source))
    else:
        generated = touch_dir / "key_next_event0.bin"
        generate_touch_event(adb, touch_tool, generated, button="next")
        key_next_path = require_path(generated)

    key_prev_path: Path
    if args.key_prev_event_source:
        key_prev_path = require_path(Path(args.key_prev_event_source))
    else:
        generated = touch_dir / "key_prev_event2.bin"
        generate_touch_event(adb, touch_tool, generated, button="prev")
        key_prev_path = require_path(generated)

    # Compute configuration values
    track_restore_value = "1" if (args.restore_enabled and not args.disable_track_restore) else "0"
    direct_track_select_value = "0" if args.disable_book_title_direct_track_select else "1"
    direct_track_preplay_value = "0" if args.disable_book_title_direct_track_preplay else "1"
    direct_open_value = "0"
    book_title_memscan_value = "0" if args.disable_book_title_memscan else "1"
    book_title_direct_track_calibrate_value = "0" if args.disable_book_title_direct_track_calibrate else "1"
    book_title_direct_track_recovery_value = "0" if args.disable_book_title_direct_track_recovery else "1"
    ui_seek_fallback_value = "0" if args.disable_ui_seek_fallback else "1"
    ui_seek_screen_guard_value = "0" if args.disable_ui_seek_screen_guard else "1"
    back_guard_value = "1" if args.enable_back_guard else "0"
    restore_enabled_value = "1" if args.restore_enabled else "0"
    path_guard_value = "0" if args.disable_book_title_path_guard else "1"

    remote_base = args.remote_base

    # Check device and create remote directories
    adb_run(adb, ["devices"])
    adb_shell(adb, f"mkdir -p '{remote_base}/bin' '{remote_base}/input' '{remote_base}/resume.d'")

    # Push binary helpers
    adb_run(adb, ["push", str(helper_path), f"{remote_base}/bin/r1_audiobook_resume_helper"])
    adb_run(adb, ["push", str(memscan_path), f"{remote_base}/bin/r1_audiobook_memscan"])
    adb_run(adb, ["push", str(direct_open_path), f"{remote_base}/bin/r1_audiobook_direct_open"])
    adb_run(adb, ["push", str(daemon_binary_path), f"{remote_base}/bin/r1_audiobook_resume_daemon"])
    adb_run(adb, ["push", str(daemon_wrapper_path), f"{remote_base}/bin/r1_audiobook_resume_daemon.sh"])
    adb_run(adb, ["push", str(daemon_shell_path), f"{remote_base}/bin/r1_audiobook_resume_daemon_shell.sh"])

    # Push catalog if provided
    if catalog_path:
        adb_run(adb, ["push", str(catalog_path), f"{remote_base}/catalog.tsv"])

    # Push touch/key event streams
    adb_run(adb, ["push", str(touch_next_path), f"{remote_base}/input/touch_next_event1.bin"])
    adb_run(adb, ["push", str(touch_first_track_path), f"{remote_base}/input/touch_first_track_event1.bin"])
    adb_run(adb, ["push", str(touch_first_track_down_path), f"{remote_base}/input/touch_first_track_down_event1.bin"])
    adb_run(adb, ["push", str(touch_first_track_move_path), f"{remote_base}/input/touch_first_track_move_event1.bin"])
    adb_run(adb, ["push", str(touch_first_track_up_path), f"{remote_base}/input/touch_first_track_up_event1.bin"])
    adb_run(adb, ["push", str(touch_back_path), f"{remote_base}/input/touch_back_event1.bin"])

    for i, row_path in enumerate(touch_track_row_paths):
        row_number = i + 1
        adb_run(adb, ["push", str(row_path), f"{remote_base}/input/touch_track_row{row_number}_event1.bin"])

    for swipe_path in touch_track_swipe_paths:
        leaf = swipe_path.name
        adb_run(adb, ["push", str(swipe_path), f"{remote_base}/input/{leaf}"])

    adb_run(adb, ["push", str(key_next_path), f"{remote_base}/input/key_next_event0.bin"])
    adb_run(adb, ["push", str(key_prev_path), f"{remote_base}/input/key_prev_event2.bin"])

    # Build and run the install/start command
    env_vars = " ".join([
        f"AUDIOBOOK_POSITION_SOURCE='{args.position_source}'",
        f"AUDIOBOOK_RESTORE_ENABLED='{restore_enabled_value}'",
        f"AUDIOBOOK_TRACK_RESTORE_ENABLED='{track_restore_value}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED='{direct_track_select_value}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED='{direct_track_preplay_value}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED='{direct_open_value}'",
        "AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR='0x760708'",
        "AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR='0x8e4400'",
        "AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS='6000'",
        "AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US='200000'",
        "AUDIOBOOK_ARM_WINDOW_MS='1000'",
        "AUDIOBOOK_ARM_POLL_MS='200'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES='{args.book_title_direct_track_max_swipes}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS='{args.book_title_direct_track_visible_rows}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE='{args.book_title_direct_track_rows_per_swipe}'",
        f"AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED='{book_title_memscan_value}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED='{book_title_direct_track_calibrate_value}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED='{book_title_direct_track_recovery_value}'",
        f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS='{args.book_title_direct_track_recovery_max_steps}'",
        f"AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH='{path_guard_value}'",
        f"AUDIOBOOK_INTERVAL_SECONDS='{args.interval_seconds}'",
        f"AUDIOBOOK_IDLE_INTERVAL_SECONDS='{args.idle_interval_seconds}'",
        f"AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS='{args.book_title_marker_idle_poll_seconds}'",
        f"AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS='{args.book_title_marker_music_poll_seconds}'",
        f"AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS='{args.diagnostics_interval_seconds}'",
        f"AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS='{args.book_title_autostart_delay_seconds}'",
        f"AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS='{args.book_title_launcher_tracklist_wait_seconds}'",
        f"AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS='{args.book_title_context_seconds}'",
        "AUDIOBOOK_SAVE_BUCKET_MS='15000'",
        f"AUDIOBOOK_NEW_TRACK_COMMIT_MS='{args.new_track_commit_ms}'",
        f"AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS='{args.restore_retry_max_after_failure_seconds}'",
        f"AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS='{args.failed_restore_skip_log_bucket_ms}'",
        f"AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS='{args.book_title_restore_log_bucket_ms}'",
        f"AUDIOBOOK_RESUME_LOG_MAX_BYTES='{args.resume_log_max_bytes}'",
        f"AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED='{ui_seek_fallback_value}'",
        f"AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED='{ui_seek_screen_guard_value}'",
        f"AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS='{args.ui_seek_screen_min_bar_pixels}'",
        f"AUDIOBOOK_UI_SEEK_BAR_X_MIN='{args.ui_seek_bar_x_min}'",
        f"AUDIOBOOK_UI_SEEK_BAR_X_MAX='{args.ui_seek_bar_x_max}'",
        f"AUDIOBOOK_UI_SEEK_BAR_Y='{args.ui_seek_bar_y}'",
        f"AUDIOBOOK_UI_SEEK_VERIFY_TOLERANCE_MS='{args.ui_seek_verify_tolerance_ms}'",
        f"AUDIOBOOK_UI_SEEK_TOUCH_FRAMES='{args.ui_seek_touch_frames}'",
        f"AUDIOBOOK_BACK_GUARD_ENABLED='{back_guard_value}'",
        f"AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS='{args.back_guard_window_seconds}'",
        f"AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS='{args.back_guard_after_screen_seconds}'",
        f"AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS='{args.back_guard_idle_interval_seconds}'",
        f"AUDIOBOOK_BACK_GUARD_EXTRA_BACKS='{args.back_guard_extra_backs}'",
    ])

    install_command = (
        f"chmod 755 '{remote_base}/bin/r1_audiobook_resume_helper' "
        f"'{remote_base}/bin/r1_audiobook_memscan' "
        f"'{remote_base}/bin/r1_audiobook_direct_open' "
        f"'{remote_base}/bin/r1_audiobook_resume_daemon' "
        f"'{remote_base}/bin/r1_audiobook_resume_daemon.sh' "
        f"'{remote_base}/bin/r1_audiobook_resume_daemon_shell.sh'; "
        f"old_pid=$(cat '{remote_base}/resume-daemon.pid' 2>/dev/null || true); "
        '[ -n "$old_pid" ] && kill "$old_pid" 2>/dev/null || true; '
        f"start-stop-daemon -K -p '{remote_base}/resume-daemon.ssd.pid' 2>/dev/null || true; "
        "sleep 1; "
        '[ -n "$old_pid" ] && kill -9 "$old_pid" 2>/dev/null || true; '
        f"rm -f '{remote_base}/resume-daemon.pid' '{remote_base}/resume-daemon.ssd.pid'; "
        f"{env_vars} start-stop-daemon -S -b -m -p '{remote_base}/resume-daemon.ssd.pid' "
        f"-x '{remote_base}/bin/r1_audiobook_resume_daemon.sh'; "
        "sleep 1; "
        "echo '--- pid ---'; "
        f"cat '{remote_base}/resume-daemon.pid' '{remote_base}/resume-daemon.ssd.pid' 2>/dev/null; "
        "echo '--- process ---'; "
        "ps | grep r1_audiobook_resume_daemon | grep -v grep || true; "
        "echo '--- log ---'; "
        f"cat '{remote_base}/resume-daemon.log' 2>/dev/null | tail -20;"
    )

    adb_shell(adb, install_command)
    logger.info("Resume daemon installed and started.")


# ---------------------------------------------------------------------------
# Argparse
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Install audiobook resume daemon runtime files to the R1 via ADB."
    )
    parser.add_argument("--adb", default="", help="Path to adb binary.")
    parser.add_argument(
        "--helper-source",
        default="work/device-audiobook-helper-20260609/audiobooks/bin/r1_audiobook_resume_helper",
        help="Local path to the resume helper binary.",
    )
    parser.add_argument(
        "--memscan-helper-source",
        default="work/native-memscan/r1_audiobook_memscan",
        help="Local path to the memscan helper binary.",
    )
    parser.add_argument(
        "--direct-open-helper-source",
        default="work/native-direct-open/r1_audiobook_direct_open",
        help="Local path to the direct-open helper binary.",
    )
    parser.add_argument(
        "--daemon-source",
        default="build/r1_audiobook_resume_daemon",
        help="Local path to the compiled resume daemon binary.",
    )
    parser.add_argument(
        "--daemon-wrapper-source",
        default="tools/r1_audiobook_resume_daemon_wrapper.sh",
        help="Local path to the resume daemon wrapper script.",
    )
    parser.add_argument(
        "--daemon-shell-source",
        default="tools/r1_audiobook_resume_daemon.sh",
        help="Local path to the resume daemon shell fallback.",
    )
    parser.add_argument("--catalog-source", default="", help="Local path to a resume catalog TSV.")
    parser.add_argument("--restore-enabled", action="store_true", help="Enable bookmark restore on startup.")
    parser.add_argument("--disable-track-restore", action="store_true", help="Disable per-track restore.")
    parser.add_argument("--disable-book-title-path-guard", action="store_true", help="Disable autostart path guard.")
    parser.add_argument("--disable-book-title-direct-track-select", action="store_true", help="Disable direct track selection.")
    parser.add_argument("--disable-book-title-direct-track-preplay", action="store_true", help="Disable direct track pre-play.")
    parser.add_argument("--disable-book-title-direct-open", action="store_true", help="Disable one-shot direct-open helper.")
    parser.add_argument("--disable-ui-seek-fallback", action="store_true", help="Disable UI seek fallback.")
    parser.add_argument("--disable-ui-seek-screen-guard", action="store_true", help="Disable UI seek screen guard.")
    parser.add_argument("--touch-next-event-source", default="", help="Pre-built touch-next event stream.")
    parser.add_argument("--touch-first-track-event-source", default="", help="Pre-built first-track touch event stream.")
    parser.add_argument("--key-next-event-source", default="", help="Pre-built key-next event stream.")
    parser.add_argument("--key-prev-event-source", default="", help="Pre-built key-prev event stream.")
    parser.add_argument("--position-source", choices=["memory", "helper"], default="memory", help="Position data source.")
    parser.add_argument("--interval-seconds", type=int, default=1, help="Active polling interval.")
    parser.add_argument("--idle-interval-seconds", type=int, default=5, help="Idle polling interval.")
    parser.add_argument("--book-title-marker-idle-poll-seconds", type=int, default=5, help="Idle title marker poll interval.")
    parser.add_argument("--book-title-marker-music-poll-seconds", type=int, default=15, help="Music title marker poll interval.")
    parser.add_argument("--diagnostics-interval-seconds", type=int, default=60, help="Diagnostics log interval.")
    parser.add_argument("--book-title-autostart-delay-seconds", type=int, default=1, help="Autostart delay after title tap.")
    parser.add_argument("--book-title-launcher-tracklist-wait-seconds", type=int, default=4, help="Launcher tracklist wait.")
    parser.add_argument("--book-title-context-seconds", type=int, default=300, help="Context-aware autostart window.")
    parser.add_argument("--new-track-commit-ms", type=int, default=15000, help="New track commit guard ms.")
    parser.add_argument("--restore-retry-max-after-failure-seconds", type=int, default=300, help="Restore retry backoff.")
    parser.add_argument("--failed-restore-skip-log-bucket-ms", type=int, default=30000, help="Failed restore log throttle.")
    parser.add_argument("--book-title-restore-log-bucket-ms", type=int, default=5000, help="Title restore log throttle.")
    parser.add_argument("--resume-log-max-bytes", type=int, default=524288, help="Resume daemon log cap.")
    parser.add_argument("--book-title-direct-track-max-swipes", type=int, default=20, help="Max track-list swipes.")
    parser.add_argument("--book-title-direct-track-visible-rows", type=int, default=5, choices=range(1, 6), help="Visible track rows.")
    parser.add_argument("--book-title-direct-track-rows-per-swipe", type=int, default=4, help="Rows scrolled per swipe.")
    parser.add_argument("--disable-book-title-memscan", action="store_true", help="Disable title memscan.")
    parser.add_argument("--disable-book-title-direct-track-calibrate", action="store_true", help="Disable track calibration.")
    parser.add_argument("--disable-book-title-direct-track-recovery", action="store_true", help="Disable track recovery transport.")
    parser.add_argument("--book-title-direct-track-recovery-max-steps", type=int, default=20, help="Max recovery steps.")
    parser.add_argument("--ui-seek-bar-x-min", type=int, default=21, help="Seek bar min X.")
    parser.add_argument("--ui-seek-bar-x-max", type=int, default=459, help="Seek bar max X.")
    parser.add_argument("--ui-seek-bar-y", type=int, default=619, help="Seek bar Y.")
    parser.add_argument("--ui-seek-verify-tolerance-ms", type=int, default=15000, help="Seek verify tolerance.")
    parser.add_argument("--ui-seek-touch-frames", type=int, default=2, help="Seek touch frames.")
    parser.add_argument("--ui-seek-screen-min-bar-pixels", type=int, default=300, help="Min seek bar pixels for screen guard.")
    parser.add_argument("--enable-back-guard", action="store_true", help="Enable Audiobooks back-stack guard.")
    parser.add_argument("--back-guard-window-seconds", type=int, default=60, help="Back guard window.")
    parser.add_argument("--back-guard-after-screen-seconds", type=int, default=8, help="Back guard post-screen delay.")
    parser.add_argument("--back-guard-idle-interval-seconds", type=int, default=1, help="Back guard idle poll.")
    parser.add_argument("--back-guard-extra-backs", type=int, default=2, help="Extra back gestures.")
    parser.add_argument("--remote-base", default=REMOTE_BASE_DEFAULT, help="Remote install base directory.")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        install_runtime(args)
    except Exception as exc:
        logger.error("error: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
