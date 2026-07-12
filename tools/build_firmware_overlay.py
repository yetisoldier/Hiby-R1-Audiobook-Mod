#!/usr/bin/env python3
"""Build HiBy R1 audiobook firmware using an overlay-based approach.

Instead of keeping a pre-extracted rootfs tree in the workspace, this script:
  1. Takes a stock rootfs.squashfs as input
  2. Unsquashfs it to a temp directory
  3. Applies the overlay described in tools/firmware_overlay.json
  4. Repacks as squashfs with preserved modes
  5. Wraps in a UPT firmware package

This produces the same result as build_firmware.py but with cleaner git diffs
and no need to store the full extracted rootfs tree.

Usage:
  python3 tools/build_firmware_overlay.py \
      --rootfs work/original/rootfs.squashfs \
      --x-image work/original/xImage \
      --output-upt work/audiobook-firmware/r1-audiobooks.upt

The overlay manifest (tools/firmware_overlay.json) is human-readable JSON that
lists every file the mod adds, patches, or removes from the stock rootfs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Optional

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Embedded shell scripts (must match build_firmware.py exactly)
# ---------------------------------------------------------------------------

ADB_BOOT_SCRIPT = """\
#!/bin/sh
#
# Development-only boot ADB wrapper.
# The stock helper is T90adb, but rcS only starts S??* scripts. This wrapper
# starts ADB only when the stock UI setting System -> USB working mode is Device.
#

read_usb_working_mode() {
    if [ ! -f /usr/data/user.ini ]; then
        echo ""
        return
    fi

    set -- $(dd if=/usr/data/user.ini bs=1 skip=1856 count=1 2>/dev/null | od -An -t u1 2>/dev/null)
    echo "${1:-}"
}

case "$1" in
  start)
    mode=$(read_usb_working_mode)
    if [ "$mode" != "1" ]; then
        echo "Skip boot adb: usb_working_mode=$mode"
        exit 0
    fi
    /etc/init.d/T90adb start
    ;;
  stop)
    /etc/init.d/T90adb stop
    ;;
  restart|reload)
    /etc/init.d/T90adb stop
    /etc/init.d/T90adb start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac

exit $?
"""

BATD_BLOCK = """\
if [ -f "/usr/bin/batd" ]; then
killall    batd    &>/dev/null
killall -9 batd    &>/dev/null
/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi

"""

RESUME_BOOT_SCRIPT = r"""\
#!/bin/sh

BASE=/usr/data/audiobooks

clear_stock_audiobook_last_file() {
  user_ini=/usr/data/user.ini
  [ -f "$user_ini" ] || return 0

  hex=
  for byte in $(dd if="$user_ini" bs=1 skip=40 count=28 2>/dev/null | od -An -tx1); do
    hex=$hex$byte
  done

  case "$hex" in
    61003a005c0041007500640069006f0062006f006f006b007300*|41003a005c0041007500640069006f0062006f006f006b007300*|00003a005c0041007500640069006f0062006f006f006b007300*)
      if [ ! -f "$BASE/user.ini.before-stock-audiobook-last-clear" ]; then
        cp -f "$user_ini" "$BASE/user.ini.before-stock-audiobook-last-clear" 2>/dev/null || true
      fi
      dd if=/dev/zero of="$user_ini" bs=1 seek=40 count=320 conv=notrunc 2>/dev/null || true
      sync
      printf '%s cleared stock last audiobook path slot in /usr/data/user.ini\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" >>"$BASE/boot-reset.log"
      ;;
  esac
}

if [ "$1" = stop ]; then
  start-stop-daemon -K -p "$BASE/resume-daemon.ssd.pid" 2>/dev/null || true
  exit 0
fi

if [ "$1" != start ]; then
  if [ -n "$1" ]; then
    exit 0
  fi
fi

mkdir -p "$BASE/bin" "$BASE/resume.d"
clear_stock_audiobook_last_file
cp -f /usr/bin/r1_audiobook_resume_daemon "$BASE/bin/r1_audiobook_resume_daemon"
cp -f /usr/bin/r1_audiobook_resume_daemon.sh "$BASE/bin/r1_audiobook_resume_daemon.sh"
cp -f /usr/bin/r1_audiobook_resume_daemon_shell.sh "$BASE/bin/r1_audiobook_resume_daemon_shell.sh"

if [ ! -s "$BASE/catalog.tsv" ]; then
  if [ -f /usr/bin/r1_audiobook_catalog.tsv ]; then
    cp -f /usr/bin/r1_audiobook_catalog.tsv "$BASE/catalog.tsv"
  fi
fi

chmod 755 "$BASE/bin/r1_audiobook_resume_daemon" "$BASE/bin/r1_audiobook_resume_daemon.sh" "$BASE/bin/r1_audiobook_resume_daemon_shell.sh"

old_pid=$(cat "$BASE/resume-daemon.pid" 2>/dev/null || true)
[ -n "$old_pid" ] && kill "$old_pid" 2>/dev/null || true
start-stop-daemon -K -p "$BASE/resume-daemon.ssd.pid" 2>/dev/null || true
sleep 1
[ -n "$old_pid" ] && kill -9 "$old_pid" 2>/dev/null || true
rm -f "$BASE/resume-daemon.pid" "$BASE/resume-daemon.ssd.pid"

AUDIOBOOK_POSITION_SOURCE=memory
AUDIOBOOK_RESTORE_ENABLED=1
AUDIOBOOK_TRACK_RESTORE_ENABLED=__AUDIOBOOK_TRACK_RESTORE_ENABLED__
AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED=__AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED__
AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS=15000
AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED=__AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=20
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=5
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4
AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED=__AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS=20
AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED__
AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR=0x760708
AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR=0x8e4400
AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS=6000
AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US=200000
AUDIOBOOK_ARM_WINDOW_MS=1000
AUDIOBOOK_ARM_POLL_MS=200
AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH=1
AUDIOBOOK_INTERVAL_SECONDS=2
AUDIOBOOK_IDLE_INTERVAL_SECONDS=5
AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS=5
AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS=15
AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS=60
AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS=1
AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS=4
AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS=300
AUDIOBOOK_SAVE_BUCKET_MS=15000
AUDIOBOOK_NEW_TRACK_COMMIT_MS=15000
AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS=300
AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS=30000
AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS=5000
AUDIOBOOK_RESUME_LOG_MAX_BYTES=524288
AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED=__AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED__
AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED=1
AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS=300
AUDIOBOOK_UI_SEEK_TOUCH_FRAMES=2
AUDIOBOOK_BACK_GUARD_ENABLED=__AUDIOBOOK_BACK_GUARD_ENABLED__
AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS=60
AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS=8
AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS=1
AUDIOBOOK_BACK_GUARD_EXTRA_BACKS=2
export AUDIOBOOK_POSITION_SOURCE AUDIOBOOK_RESTORE_ENABLED AUDIOBOOK_TRACK_RESTORE_ENABLED
export AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS
export AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE
export AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS
export AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR
export AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US
export AUDIOBOOK_ARM_WINDOW_MS AUDIOBOOK_ARM_POLL_MS
export AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH
export AUDIOBOOK_INTERVAL_SECONDS AUDIOBOOK_IDLE_INTERVAL_SECONDS AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS
export AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS
export AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS
export AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS
export AUDIOBOOK_SAVE_BUCKET_MS AUDIOBOOK_NEW_TRACK_COMMIT_MS AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS
export AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS AUDIOBOOK_RESUME_LOG_MAX_BYTES AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED
export AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS AUDIOBOOK_UI_SEEK_TOUCH_FRAMES
export AUDIOBOOK_BACK_GUARD_ENABLED AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS
export AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS
export AUDIOBOOK_BACK_GUARD_EXTRA_BACKS
start-stop-daemon -S -b -m -p "$BASE/resume-daemon.ssd.pid" -x "$BASE/bin/r1_audiobook_resume_daemon.sh" >>"$BASE/resume-daemon.stdout.log" 2>&1
"""

DB_MAINT_BOOT_SCRIPT = r"""\
#!/bin/sh

BASE=/usr/data/audiobooks

db_watch_pid_is_live() {
  check_pid=$1
  [ -n "$check_pid" ] || return 1
  [ -d "/proc/$check_pid" ] || return 1

  if [ -r "/proc/$check_pid/cmdline" ]; then
    cmdline=$(tr '\000' ' ' <"/proc/$check_pid/cmdline" 2>/dev/null || true)
    case "$cmdline" in
      *r1_audiobook_db_watch.sh*) return 0 ;;
    esac
  fi

  ps_line=$(ps | awk -v pid="$check_pid" '$1 == pid { $1=""; print }' 2>/dev/null | head -n 1)
  case "$ps_line" in
    *r1_audiobook_db_watch.sh*) return 0 ;;
  esac
  return 1
}

stop_db_watch() {
  old_pid=$(cat "$BASE/db-maint.ssd.pid" 2>/dev/null || cat "$BASE/db-maint.lock/pid" 2>/dev/null || true)
  case "$old_pid" in
    ''|*[!0-9]*) old_pid= ;;
  esac

  start-stop-daemon -K -p "$BASE/db-maint.ssd.pid" 2>/dev/null || true

  if [ -n "$old_pid" ]; then
    wait_count=0
    while [ "$wait_count" -lt 3 ] && db_watch_pid_is_live "$old_pid"; do
      sleep 1
      wait_count=$((wait_count + 1))
    done
    if db_watch_pid_is_live "$old_pid"; then
      kill -9 "$old_pid" 2>/dev/null || true
      sleep 1
    fi
  fi

  if [ -z "$old_pid" ] || ! db_watch_pid_is_live "$old_pid"; then
    rm -rf "$BASE/db-maint.lock" 2>/dev/null || true
  fi
  rm -f "$BASE/db-maint.pid" "$BASE/db-maint.ssd.pid"
}

if [ "$1" = stop ]; then
  stop_db_watch
  exit 0
fi

if [ "$1" != start ]; then
  if [ -n "$1" ]; then
    exit 0
  fi
fi

mkdir -p "$BASE/bin" "$BASE/resume.d"
cp -f /usr/bin/r1_audiobook_db_maint "$BASE/bin/r1_audiobook_db_maint"
cp -f /usr/bin/r1_audiobook_db_watch.sh "$BASE/bin/r1_audiobook_db_watch.sh"
cp -f /usr/bin/r1_usrlocal_media_seed.db "$BASE/bin/r1_usrlocal_media_seed.db"
chmod 755 "$BASE/bin/r1_audiobook_db_maint" "$BASE/bin/r1_audiobook_db_watch.sh"
chmod 644 "$BASE/bin/r1_usrlocal_media_seed.db"

stop_db_watch

AUDIOBOOK_DB_BOOT_DELAY_SECONDS=20
AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=180
AUDIOBOOK_DB_STABLE_POLL_SECONDS=3
AUDIOBOOK_DB_INTERVAL_SECONDS=30
AUDIOBOOK_DB_STABLE_SECONDS=15
AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS=0
AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES=524288
AUDIOBOOK_DB_RUN_ON_MTIME_ONLY=0
AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS=0
AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=600
AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS=5
AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS=600
AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS=5
AUDIOBOOK_VIEW_GENERATION_ENABLED=__AUDIOBOOK_VIEW_GENERATION_ENABLED__
export AUDIOBOOK_DB_BOOT_DELAY_SECONDS AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS AUDIOBOOK_DB_STABLE_POLL_SECONDS
export AUDIOBOOK_DB_INTERVAL_SECONDS AUDIOBOOK_DB_STABLE_SECONDS
export AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES
export AUDIOBOOK_DB_RUN_ON_MTIME_ONLY AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS
export AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS
export AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS
export AUDIOBOOK_VIEW_GENERATION_ENABLED

start-stop-daemon -S -b -m -p "$BASE/db-maint.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_db_watch.sh" >>"$BASE/db-maint.stdout.log" 2>&1
"""


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOLS_DIR.parent


def load_manifest() -> dict[str, Any]:
    """Load the overlay manifest JSON."""
    manifest_path = TOOLS_DIR / "firmware_overlay.json"
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def resolve_path_strict(path_value: str | Path) -> Path:
    """Resolve a path and ensure it exists."""
    p = Path(path_value).resolve()
    if not p.exists():
        raise FileNotFoundError(f"Missing path: {p}")
    return p


def run(cmd: list[str | Path], *, cwd: Optional[Path] = None, check: bool = True) -> subprocess.CompletedProcess:
    """Run a subprocess command, logging it first."""
    cmd_str = [str(c) for c in cmd]
    log.info("RUN: %s", " ".join(cmd_str))
    result = subprocess.run(cmd_str, cwd=str(cwd) if cwd else None)
    if check and result.returncode != 0:
        raise RuntimeError(f"Command failed (exit {result.returncode}): {' '.join(cmd_str)}")
    return result


def run_python(script: str | Path, args: list[str | Path], *, cwd: Optional[Path] = None) -> None:
    """Run a Python script from the tools/ directory."""
    script_path = TOOLS_DIR / script if not Path(script).is_absolute() else Path(script)
    cmd = [sys.executable, str(script_path)] + [str(a) for a in args]
    run(cmd, cwd=cwd)


def write_ascii(path: Path, text: str) -> None:
    """Write text as ASCII to a file."""
    path.write_text(text, encoding="ascii")


def is_conditional_enabled(condition: str, args: argparse.Namespace) -> bool:
    """Check if a conditional flag in the manifest is enabled by the build args."""
    flag_map = {
        "enable_boot_adb": args.enable_boot_adb,
        "include_audiobook_launcher_icon": args.include_audiobook_launcher_icon,
        "disable_batd_logger": args.disable_batd_logger,
        "unlock_native_dsd": args.unlock_native_dsd,
        "enable_bluetooth_sbc_xq": args.enable_bluetooth_sbc_xq,
        "unlock_usb_dac_mode": args.unlock_usb_dac_mode,
        "ota_version_nonzero_or_ota_site": args.ota_version != 0 or bool(args.ota_site),
    }
    return flag_map.get(condition, False)


# ---------------------------------------------------------------------------
# Overlay application
# ---------------------------------------------------------------------------

def apply_copy_files(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
    tmp_dir: Path,
) -> None:
    """Copy pre-built files from the workspace into the rootfs tree."""
    for entry in manifest.get("add_files", []):
        if entry.get("optional") and not entry.get("source"):
            # Optional file with no source — skip unless user provided one
            continue

        target_rel = entry["target"]
        target_path = root_tree / target_rel
        target_path.parent.mkdir(parents=True, exist_ok=True)

        if entry.get("source_type") == "generated":
            # Generated files are handled by apply_generated_files()
            continue

        source = entry.get("source", "")
        if not source:
            continue

        source_path = resolve_path_strict(PROJECT_DIR / source)
        log.info("COPY: %s → %s", source_path, target_path)
        shutil.copy2(source_path, target_path)


def apply_generated_files(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
    tmp_dir: Path,
) -> None:
    """Generate touch/key event files and copy them into the rootfs tree."""
    for entry in manifest.get("add_files", []):
        if entry.get("source_type") != "generated":
            continue

        target_rel = entry["target"]
        target_path = root_tree / target_rel
        target_path.parent.mkdir(parents=True, exist_ok=True)

        generator = entry["generator"]
        gen_args = entry.get("generator_args", [])

        # Check for user-provided overrides
        target_name = Path(target_rel).name
        override_map = {
            "r1_touch_next_event1.bin": args.touch_next_event_source,
            "r1_touch_first_track_event1.bin": args.touch_first_track_event_source,
            "r1_key_next_event0.bin": args.key_next_event_source,
            "r1_key_prev_event2.bin": args.key_prev_event_source,
        }
        override = override_map.get(target_name, "")
        if override:
            source_path = resolve_path_strict(override)
            log.info("COPY (override): %s → %s", source_path, target_path)
            shutil.copy2(source_path, target_path)
            continue

        # Generate using the helper script
        gen_output = tmp_dir / target_name
        gen_full_args = ["--output", str(gen_output)] + gen_args
        log.info("GENERATE: %s %s → %s", generator, " ".join(gen_full_args), target_path)
        run_python(generator, gen_full_args)
        shutil.copy2(gen_output, target_path)


def apply_optional_catalog(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Copy optional resume catalog TSV if provided."""
    if not args.audiobook_resume_catalog:
        return

    source_path = resolve_path_strict(args.audiobook_resume_catalog)
    target_path = root_tree / "usr" / "bin" / "r1_audiobook_catalog.tsv"
    target_path.parent.mkdir(parents=True, exist_ok=True)
    log.info("COPY (catalog): %s → %s", source_path, target_path)
    shutil.copy2(source_path, target_path)


def apply_scripts(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Write generated boot scripts into the rootfs tree."""
    for entry in manifest.get("add_scripts", []):
        conditional = entry.get("conditional")
        if conditional and not is_conditional_enabled(conditional, args):
            continue

        target_rel = entry["target"]
        target_path = root_tree / target_rel
        target_path.parent.mkdir(parents=True, exist_ok=True)

        template_name = entry["template"]
        if template_name == "ADB_BOOT_SCRIPT":
            script_text = ADB_BOOT_SCRIPT
        elif template_name == "RESUME_BOOT_SCRIPT":
            script_text = _render_resume_script(args)
        elif template_name == "DB_MAINT_BOOT_SCRIPT":
            script_text = _render_db_maint_script(args)
        else:
            raise ValueError(f"Unknown script template: {template_name}")

        script_text = script_text.replace("\r\n", "\n").replace("\r", "\n")
        log.info("WRITE SCRIPT: %s", target_path)
        write_ascii(target_path, script_text)


def _render_resume_script(args: argparse.Namespace) -> str:
    """Render the resume boot script with feature flag substitutions."""
    conservative = args.use_conservative_resume_runtime
    include_native_hub_launcher = args.include_audiobook_native_hub_launcher
    if args.include_audiobook_native_hub_view_rows and not include_native_hub_launcher:
        include_native_hub_launcher = True

    audiobook_track_restore_enabled = "0" if conservative else "1"
    audiobook_book_title_autostart_enabled = "0" if conservative else "1"
    audiobook_direct_track_select_enabled = "0" if conservative else "1"
    audiobook_direct_track_preplay_enabled = "0" if conservative else "1"
    audiobook_book_title_memscan_enabled = "0" if conservative else "1"
    audiobook_direct_track_calibrate_enabled = "0" if conservative else "1"
    audiobook_direct_track_recovery_enabled = "0" if conservative else "1"
    audiobook_direct_open_enabled = "0"
    audiobook_ui_seek_fallback_enabled = "0" if conservative else "1"
    audiobook_back_guard_enabled = "0" if (conservative or include_native_hub_launcher) else "1"
    audiobook_first_track_entry_restore_enabled = (
        "1" if (not conservative and args.include_audiobook_native_hub_view_rows) else "0"
    )

    replacements = {
        "__AUDIOBOOK_BACK_GUARD_ENABLED__": audiobook_back_guard_enabled,
        "__AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED__": audiobook_first_track_entry_restore_enabled,
        "__AUDIOBOOK_TRACK_RESTORE_ENABLED__": audiobook_track_restore_enabled,
        "__AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED__": audiobook_book_title_autostart_enabled,
        "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED__": audiobook_direct_track_select_enabled,
        "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED__": audiobook_direct_track_preplay_enabled,
        "__AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED__": audiobook_book_title_memscan_enabled,
        "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED__": audiobook_direct_track_calibrate_enabled,
        "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED__": audiobook_direct_track_recovery_enabled,
        "__AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED__": audiobook_direct_open_enabled,
        "__AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED__": audiobook_ui_seek_fallback_enabled,
    }

    text = RESUME_BOOT_SCRIPT
    for placeholder, value in replacements.items():
        text = text.replace(placeholder, value)
    return text


def _render_db_maint_script(args: argparse.Namespace) -> str:
    """Render the DB maintenance boot script with feature flag substitutions."""
    audiobook_view_generation_enabled = "1" if args.include_audiobook_native_hub_view_rows else "0"
    text = DB_MAINT_BOOT_SCRIPT.replace(
        "__AUDIOBOOK_VIEW_GENERATION_ENABLED__", audiobook_view_generation_enabled
    )
    return text


def apply_version_marker(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Write the firmware version marker file."""
    # Determine audiobook entry marker
    include_native_hub_launcher = args.include_audiobook_native_hub_launcher
    if args.include_audiobook_native_hub_view_rows and not include_native_hub_launcher:
        include_native_hub_launcher = True

    audiobook_entry_marker = "stock-book"
    if args.include_audiobook_launcher_genre:
        audiobook_entry_marker = "direct-genre"
    if args.include_audiobook_private_direct_route:
        audiobook_entry_marker = "direct-private-route"
    if args.include_audiobook_native_hub_title_row:
        audiobook_entry_marker = "native-hub-title-row"
    if include_native_hub_launcher:
        audiobook_entry_marker = "native-hub-launcher-title-row"
    if args.include_audiobook_native_hub_folder_rows:
        if include_native_hub_launcher:
            audiobook_entry_marker = "native-hub-launcher-title-folders"
        else:
            audiobook_entry_marker = "native-hub-title-folders"
    if args.include_audiobook_native_hub_view_rows:
        audiobook_entry_marker = "native-hub-view-rows"
    if getattr(args, 'include_audiobook_system_launcher', False):
        audiobook_entry_marker = "system-launcher"

    # Determine OTA site
    ota_info_path = root_tree / "etc" / "ota_info"
    effective_ota_site = "/data/autoupdate/autoupdate"
    if ota_info_path.exists():
        ota_info_text = ota_info_path.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"^ota_site=(.+)$", ota_info_text, re.MULTILINE)
        if m:
            effective_ota_site = m.group(1).strip()
    if args.ota_site:
        effective_ota_site = args.ota_site

    boot_adb_marker = "enabled" if args.enable_boot_adb else "disabled"
    batd_logger_marker = "disabled" if args.disable_batd_logger else "enabled"
    launcher_icon_marker = "audiobook" if args.include_audiobook_launcher_icon else "stock-book"
    native_dsd_marker = "enabled" if args.unlock_native_dsd else "stock"
    sbc_xq_marker = "enabled" if args.enable_bluetooth_sbc_xq else "stock"
    usb_dac_marker = "enabled" if args.unlock_usb_dac_mode else "stock"
    native_hub_launcher_marker = "enabled" if include_native_hub_launcher else "disabled"
    native_hub_folder_rows_marker = "enabled" if args.include_audiobook_native_hub_folder_rows else "disabled"
    native_hub_view_rows_marker = "enabled" if args.include_audiobook_native_hub_view_rows else "disabled"
    resume_runtime_profile_marker = "conservative" if args.use_conservative_resume_runtime else "direct"

    version_text = (
        f"version={args.custom_version_id}\n"
        f"label={args.custom_version_label}\n"
        f"base_firmware=1.6\n"
        f"ota_version={args.ota_version}\n"
        f"ota_site={effective_ota_site}\n"
        f"audiobook_entry={audiobook_entry_marker}\n"
        f"boot_adb={boot_adb_marker}\n"
        f"batd_logger={batd_logger_marker}\n"
        f"launcher_icon={launcher_icon_marker}\n"
        f"native_dsd={native_dsd_marker}\n"
        f"bluetooth_sbc_xq={sbc_xq_marker}\n"
        f"usb_dac_mode={usb_dac_marker}\n"
        f"native_hub_launcher={native_hub_launcher_marker}\n"
        f"native_hub_folder_rows={native_hub_folder_rows_marker}\n"
        f"native_hub_view_rows={native_hub_view_rows_marker}\n"
        f"resume_runtime_profile={resume_runtime_profile_marker}\n"
    )

    version_marker = root_tree / "etc" / "r1_audiobook_version"
    version_marker.parent.mkdir(parents=True, exist_ok=True)
    write_ascii(version_marker, version_text)
    log.info("WRITE: %s", version_marker)

    # Update OTA info if needed
    if args.ota_version != 0 or args.ota_site:
        new_ota_info = f"ota_version={args.ota_version}\nota_site={effective_ota_site}\n"
        write_ascii(ota_info_path, new_ota_info)
        log.info("WRITE: %s", ota_info_path)


def apply_binary_patches(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
    tmp_dir: Path,
) -> None:
    """Apply binary patches to hiby_player."""
    player_path = root_tree / "usr" / "bin" / "hiby_player"
    patched_player = tmp_dir / "hiby_player.audiobooks"

    player_patch_args: list[str | Path] = [str(player_path), "-o", str(patched_player)]
    if args.include_scanner_audiobook_skip:
        player_patch_args.append("--scan-skip")
    if args.include_experimental_book_audio_shim:
        player_patch_args.append("--book-audio-shim")
    if args.include_audiobook_launcher_genre:
        player_patch_args.append("--audiobook-launcher-genre")
    if args.include_audiobook_private_direct_route:
        player_patch_args.append("--audiobook-private-direct-route")
    if args.include_audiobook_native_hub_title_row:
        player_patch_args.append("--audiobook-native-hub-title-row")

    include_native_hub_launcher = args.include_audiobook_native_hub_launcher
    if include_native_hub_launcher:
        player_patch_args.append("--audiobook-native-hub-launcher")
    if args.include_audiobook_native_hub_folder_rows:
        player_patch_args.append("--audiobook-native-hub-folder-rows")
    if args.include_audiobook_native_hub_view_rows:
        if not include_native_hub_launcher:
            include_native_hub_launcher = True
            player_patch_args.append("--audiobook-native-hub-launcher")
        player_patch_args.append("--audiobook-native-hub-view-rows")
    if args.include_audiobook_system_launcher:
        player_patch_args.append("--audiobook-system-launcher")
    if args.include_audiobook_title_auto_start_marker:
        player_patch_args.append("--audiobook-title-autostart-marker")
    if args.include_select_dispatch_branch:
        player_patch_args.append("--select-dispatch-branch")
    if args.skip_existing_patches:
        player_patch_args.append("--skip-existing-patches")

    log.info("PATCH BINARY: hiby_player")
    run_python("patch_hiby_player.py", player_patch_args)
    shutil.copy2(patched_player, player_path)


def apply_resource_patches(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Apply resource text patches."""
    resource_patch_args: list[str | Path] = [
        str(root_tree),
        "--about-model", args.custom_version_label,
        "--product-version", args.custom_version_id,
    ]
    if args.include_audiobook_native_hub_view_rows:
        resource_patch_args.append("--audiobook-native-hub-view-labels")
    elif args.include_audiobook_native_hub_title_row or args.include_audiobook_native_hub_folder_rows:
        resource_patch_args.append("--audiobook-native-hub-labels")
    log.info("PATCH RESOURCES: resource text")
    run_python("patch_r1_resource_text.py", resource_patch_args)

    # Generate launcher icons
    if args.include_audiobook_launcher_icon:
        log.info("PATCH RESOURCES: launcher icons")
        run_python("generate_audiobook_launcher_icons.py", [str(root_tree)])


def apply_audio_unlocks(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Apply audio feature unlock patches."""
    audio_unlock_args: list[str | Path] = [str(root_tree)]
    if args.unlock_native_dsd:
        audio_unlock_args.append("--native-dsd")
    if args.enable_bluetooth_sbc_xq:
        audio_unlock_args.append("--sbc-xq")
    if args.unlock_usb_dac_mode:
        audio_unlock_args.append("--usb-dac")

    if len(audio_unlock_args) > 1:
        log.info("PATCH AUDIO UNLOCKS")
        run_python("patch_r1_audio_feature_unlocks.py", audio_unlock_args)


def apply_text_patches(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Apply text file patches (e.g., batd logger removal)."""
    if not args.disable_batd_logger:
        return

    player_launch_script = root_tree / "usr" / "bin" / "hiby_player.sh"
    launch_text = player_launch_script.read_text(encoding="utf-8", errors="replace")
    launch_text = launch_text.replace("\r\n", "\n").replace("\r", "\n")
    batd_block = BATD_BLOCK.replace("\r\n", "\n").replace("\r", "\n")
    if batd_block not in launch_text:
        raise RuntimeError(f"Expected batd launch block not found in {player_launch_script}")
    launch_text = launch_text.replace(batd_block, "")
    if "/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt" in launch_text:
        raise RuntimeError(f"batd SD logger command still present in {player_launch_script}")
    write_ascii(player_launch_script, launch_text)
    log.info("PATCH TEXT: removed batd logger from %s", player_launch_script)


def apply_mode_overrides(
    manifest: dict[str, Any],
    args: argparse.Namespace,
    root_tree: Path,
    pseudo_file: Path,
) -> None:
    """Write squashfs pseudo mode overrides for new and modified files."""
    pseudo_lines: list[str] = []
    for entry in manifest.get("mode_overrides", []):
        conditional = entry.get("conditional")
        if conditional and not is_conditional_enabled(conditional, args):
            continue

        target_rel = entry["target"]
        mode = entry["mode"]
        if (root_tree / target_rel).exists():
            squash_path = target_rel.replace("\\", "/")
            pseudo_lines.append(f"{squash_path} m {mode} 0 0")

    if pseudo_lines:
        with open(pseudo_file, "a", encoding="ascii") as f:
            for line in pseudo_lines:
                f.write(line + "\n")
        log.info("WROTE %d mode overrides to %s", len(pseudo_lines), pseudo_file)


# ---------------------------------------------------------------------------
# Main build logic
# ---------------------------------------------------------------------------

def build(args: argparse.Namespace) -> None:
    """Execute the overlay-based firmware build."""
    # -- Validate mutually exclusive / unsafe options ------------------------
    if args.include_audiobook_native_hub_title_row and not args.allow_unsafe_native_hub_title_row:
        raise RuntimeError(
            "include_audiobook_native_hub_title_row is unsafe after live testing rebooted the R1. "
            "Pass --allow-unsafe-native-hub-title-row only for controlled flash-test builds."
        )
    if args.include_audiobook_native_hub_view_rows and args.include_audiobook_launcher_genre:
        raise RuntimeError(
            "include_audiobook_native_hub_view_rows and include_audiobook_launcher_genre are "
            "alternative audiobook entry paths. Choose only one."
        )

    if args.ota_version < 0:
        raise ValueError("ota_version must be non-negative")

    # -- Load manifest ------------------------------------------------------
    manifest = load_manifest()
    log.info("Loaded overlay manifest: %s", TOOLS_DIR / "firmware_overlay.json")

    # -- Resolve input paths ------------------------------------------------
    rootfs_path = resolve_path_strict(args.rootfs)
    ximage_path = resolve_path_strict(args.x_image)

    mksquashfs = shutil.which("mksquashfs")
    if not mksquashfs:
        raise FileNotFoundError("mksquashfs not found on PATH")
    unsquashfs = shutil.which("unsquashfs")
    if not unsquashfs:
        raise FileNotFoundError("unsquashfs not found on PATH")

    # -- Prepare output directory -------------------------------------------
    out_dir = Path(args.out_dir)
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # -- Create temp working directory --------------------------------------
    with tempfile.TemporaryDirectory(prefix="r1-overlay-build-") as tmp:
        tmp_dir = Path(tmp)
        root_tree = tmp_dir / "squashfs-root"
        new_rootfs = out_dir / "rootfs.squashfs"
        ota_tree = out_dir / "ota-tree"
        pseudo_file = out_dir / "rootfs-pseudo.txt"

        # -- Extract stock rootfs -------------------------------------------
        log.info("Extracting stock rootfs from %s → %s", rootfs_path, root_tree)
        run([unsquashfs, "-d", str(root_tree), str(rootfs_path)])

        # -- Generate stock SquashFS pseudo modes ---------------------------
        log.info("Generating stock SquashFS pseudo modes")
        run_python(
            "write_squashfs_pseudo_modes.py",
            ["--rootfs", str(rootfs_path), "--unsquashfs", str(unsquashfs), "--output", str(pseudo_file)],
        )

        # -- Apply overlay: copy pre-built files ----------------------------
        apply_copy_files(manifest, args, root_tree, tmp_dir)

        # -- Apply overlay: generate touch/key event files ------------------
        apply_generated_files(manifest, args, root_tree, tmp_dir)

        # -- Apply overlay: optional catalog TSV ----------------------------
        apply_optional_catalog(manifest, args, root_tree)

        # -- Apply overlay: binary patches ----------------------------------
        apply_binary_patches(manifest, args, root_tree, tmp_dir)

        # -- Apply overlay: resource text patches ---------------------------
        apply_resource_patches(manifest, args, root_tree)

        # -- Apply overlay: audio feature unlocks ---------------------------
        apply_audio_unlocks(manifest, args, root_tree)

        # -- Apply overlay: text patches (batd removal) ---------------------
        apply_text_patches(manifest, args, root_tree)

        # -- Apply overlay: generated scripts -------------------------------
        apply_scripts(manifest, args, root_tree)

        # -- Apply overlay: version marker and OTA info ---------------------
        apply_version_marker(manifest, args, root_tree)

        # -- Apply mode overrides for new/modified files --------------------
        apply_mode_overrides(manifest, args, root_tree, pseudo_file)

        # -- Repack rootfs --------------------------------------------------
        log.info("Packing rootfs → %s", new_rootfs)
        run([
            mksquashfs,
            str(root_tree), str(new_rootfs),
            "-comp", "lzo",
            "-b", "131072",
            "-no-progress",
            "-all-root",
            "-pf", str(pseudo_file),
        ])

        # -- Build UPT firmware package -------------------------------------
        log.info("Building UPT firmware package → %s", args.output_upt)
        run_python("build_r1_upt.py", [
            "--ximage", str(ximage_path),
            "--rootfs", str(new_rootfs),
            "--output", str(args.output_upt),
            "--keep-tree", str(ota_tree),
            "--ota-version", str(args.ota_version),
        ])

    # -- Report -------------------------------------------------------------
    upt_path = Path(args.output_upt)
    log.info("Output UPT: %s (%d bytes)", upt_path, upt_path.stat().st_size)
    log.info("Output rootfs: %s (%d bytes)", new_rootfs, new_rootfs.stat().st_size)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Build HiBy R1 audiobook firmware using an overlay-based approach. "
            "Takes a stock rootfs.squashfs, applies the overlay manifest, and "
            "produces a .upt firmware package."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "The overlay manifest is at tools/firmware_overlay.json.\n"
            "This script produces the same output as build_firmware.py but\n"
            "does not require a pre-extracted rootfs tree in the workspace.\n"
            "\n"
            "Example:\n"
            "  python3 tools/build_firmware_overlay.py \\\n"
            "      --rootfs work/original/rootfs.squashfs \\\n"
            "      --x-image work/original/xImage \\\n"
            "      --output-upt work/audiobook-firmware/r1-audiobooks.upt\n"
        ),
    )

    # Path arguments (same defaults as build_firmware.py)
    parser.add_argument("--rootfs", default="work/original/rootfs.squashfs",
                        help="Path to stock rootfs squashfs (default: work/original/rootfs.squashfs)")
    parser.add_argument("--x-image", default="work/original/xImage",
                        help="Path to stock kernel xImage (default: work/original/xImage)")
    parser.add_argument("--out-dir", default="work/audiobook-firmware",
                        help="Output directory (default: work/audiobook-firmware)")
    parser.add_argument("--output-upt", default="work/audiobook-firmware/r1-audiobooks-dev-safe.upt",
                        help="Output UPT firmware path")

    # Feature switches (identical to build_firmware.py)
    parser.add_argument("--include-scanner-audiobook-skip", action="store_true",
                        help="Patch hiby_player to skip audiobooks in media scanner")
    parser.add_argument("--include-experimental-book-audio-shim", action="store_true",
                        help="Enable experimental book audio shim in hiby_player")
    parser.add_argument("--include-audiobook-launcher-genre", action="store_true",
                        help="Add audiobook launcher genre to hiby_player")
    parser.add_argument("--include-audiobook-private-direct-route", action="store_true",
                        help="Add audiobook private direct route to hiby_player")
    parser.add_argument("--include-audiobook-native-hub-title-row", action="store_true",
                        help="Add audiobook native hub title row to hiby_player")
    parser.add_argument("--allow-unsafe-native-hub-title-row", action="store_true",
                        help="Allow unsafe native hub title row (for controlled flash-test builds only)")
    parser.add_argument("--include-audiobook-native-hub-launcher", action="store_true",
                        help="Add audiobook native hub launcher to hiby_player")
    parser.add_argument("--include-audiobook-native-hub-folder-rows", action="store_true",
                        help="Add audiobook native hub folder rows to hiby_player")
    parser.add_argument("--include-audiobook-native-hub-view-rows", action="store_true",
                        help="Add audiobook native hub view rows to hiby_player")
    parser.add_argument("--include-audiobook-system-launcher", action="store_true",
                        help="Patch Audiobooks tile to call system() with our launch script")
    parser.add_argument("--include-audiobook-title-auto-start-marker", action="store_true",
                        help="Add audiobook title auto-start marker to hiby_player")
    parser.add_argument("--include-select-dispatch-branch", action="store_true",
                        help="Add select dispatch branch to hiby_player")
    parser.add_argument("--skip-existing-patches", action="store_true",
                        help="Skip patches already applied (for re-patching an already-patched binary)")
    parser.add_argument("--include-audiobook-launcher-icon", action="store_true",
                        help="Generate audiobook launcher icons")
    parser.add_argument("--enable-boot-adb", action="store_true",
                        help="Enable ADB on boot via S90adb init script")
    parser.add_argument("--disable-batd-logger", action="store_true",
                        help="Remove batd SD logger from hiby_player.sh")
    parser.add_argument("--unlock-native-dsd", action="store_true",
                        help="Unlock native DSD playback")
    parser.add_argument("--enable-bluetooth-sbc-xq", action="store_true",
                        help="Enable Bluetooth SBC-XQ codec")
    parser.add_argument("--unlock-usb-dac-mode", action="store_true",
                        help="Unlock USB DAC mode")
    parser.add_argument("--include-audiobook-resume-runtime", action="store_true",
                        help="Install audiobook resume runtime (daemon, helpers, event streams)")
    parser.add_argument("--use-conservative-resume-runtime", action="store_true",
                        help="Use conservative resume runtime profile (disables advanced features)")

    # Resume runtime helper paths (identical to build_firmware.py)
    parser.add_argument("--audiobook-resume-helper",
                        default="work/device-audiobook-helper-20260609/audiobooks/bin/r1_audiobook_resume_helper",
                        help="Path to r1_audiobook_resume_helper binary")
    parser.add_argument("--audiobook-memscan-helper",
                        default="work/native-memscan/r1_audiobook_memscan",
                        help="Path to r1_audiobook_memscan binary")
    parser.add_argument("--audiobook-direct-open-helper",
                        default="work/native-direct-open/r1_audiobook_direct_open",
                        help="Path to r1_audiobook_direct_open binary")
    parser.add_argument("--audiobook-resume-daemon",
                        default="build/r1_audiobook_resume_daemon",
                        help="Path to compiled r1_audiobook_resume_daemon binary")
    parser.add_argument("--audiobook-resume-wrapper",
                        default="tools/r1_audiobook_resume_daemon_wrapper.sh",
                        help="Path to r1_audiobook_resume_daemon.sh wrapper")
    parser.add_argument("--audiobook-resume-shell",
                        default="tools/r1_audiobook_resume_daemon.sh",
                        help="Path to shell fallback r1_audiobook_resume_daemon script")
    parser.add_argument("--audiobook-resume-catalog", default="",
                        help="Path to audiobook resume catalog TSV (optional)")

    # DB maintenance (identical to build_firmware.py)
    parser.add_argument("--include-audiobook-db-maintenance", action="store_true",
                        help="Install audiobook DB maintenance (db watcher, helper, seed)")
    parser.add_argument("--audiobook-db-maint-helper",
                        default="work/native-db-maint/r1_audiobook_db_maint",
                        help="Path to r1_audiobook_db_maint binary")
    parser.add_argument("--audiobook-db-watch",
                        default="tools/r1_audiobook_db_watch.sh",
                        help="Path to r1_audiobook_db_watch.sh")
    parser.add_argument("--audiobook-refresh-request",
                        default="tools/r1_audiobook_refresh.sh",
                        help="Path to r1_audiobook_refresh.sh")
    parser.add_argument("--media-db-seed",
                        default="firmware/seed/usrlocal_media.seed.db",
                        help="Path to media DB seed file")

    # Version / OTA (identical to build_firmware.py)
    parser.add_argument("--custom-version-id", default="1.6.16.5-audiobook",
                        help="Custom firmware version ID")
    parser.add_argument("--custom-version-label", default="HiBy R1 Audiobook FW 1.6.16.5",
                        help="Custom firmware version label")
    parser.add_argument("--ota-version", type=int, default=0,
                        help="OTA version number (non-negative integer)")
    parser.add_argument("--ota-site", default="",
                        help="OTA site URL override")

    # Touch/key event sources (identical to build_firmware.py)
    parser.add_argument("--touch-next-event-source", default="",
                        help="Path to pre-built touch-next event stream")
    parser.add_argument("--touch-first-track-event-source", default="",
                        help="Path to pre-built touch-first-track event stream")
    parser.add_argument("--key-next-event-source", default="",
                        help="Path to pre-built key-next event stream")
    parser.add_argument("--key-prev-event-source", default="",
                        help="Path to pre-built key-prev event stream")

    return parser


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    parser = build_parser()
    args = parser.parse_args()
    try:
        build(args)
    except (RuntimeError, FileNotFoundError, ValueError) as exc:
        log.error("%s", exc)
        sys.exit(1)


if __name__ == "__main__":
    main()
