#!/usr/bin/env python3
"""Build HiBy R1 audiobook firmware.

Converted from tools/build_r1_audiobook_firmware.ps1.

Workflow:
  1. Extract a stock rootfs squashfs
  2. Apply binary patches to hiby_player (patch_hiby_player.py)
  3. Apply resource text patches (patch_r1_resource_text.py)
  4. Optionally generate launcher icons (generate_audiobook_launcher_icons.py)
  5. Install audiobook runtime files into rootfs
  6. Repack rootfs as squashfs with preserved modes
  7. Wrap rootfs + kernel image into a UPT firmware package
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Embedded shell scripts (kept as triple-quoted constants)
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
AUDIOBOOK_TRACK_RESTORE_ENABLED=0
AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED=0
AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS=15000
AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED=0
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=0
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=0
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=20
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=5
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4
AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED=0
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED=0
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED=0
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS=20
AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED=0
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
AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED=0
AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED=1
AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS=300
AUDIOBOOK_UI_SEEK_TOUCH_FRAMES=2
AUDIOBOOK_BACK_GUARD_ENABLED=0
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
    result = subprocess.run(cmd_str, cwd=str(cwd) if cwd else None, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"Command failed (exit {result.returncode}): {' '.join(cmd_str)}")
    return result


def run_python(script: str | Path, args: list[str | Path], *, cwd: Optional[Path] = None) -> None:
    """Run a Python script from the tools/ directory."""
    tools_dir = Path(__file__).resolve().parent
    script_path = tools_dir / script if not Path(script).is_absolute() else Path(script)
    cmd = [sys.executable, str(script_path)] + [str(a) for a in args]
    run(cmd, cwd=cwd)


def write_ascii(path: Path, text: str) -> None:
    """Write text as ASCII to a file."""
    path.write_text(text, encoding="ascii")


# ---------------------------------------------------------------------------
# Main build logic
# ---------------------------------------------------------------------------

def build(args: argparse.Namespace) -> None:
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

    root_tree = out_dir / "squashfs-root"
    player_path = root_tree / "usr" / "bin" / "hiby_player"
    patched_player = out_dir / "hiby_player.audiobooks"
    new_rootfs = out_dir / "rootfs.squashfs"
    ota_tree = out_dir / "ota-tree"
    pseudo_file = out_dir / "rootfs-pseudo.txt"

    # -- Extract rootfs -----------------------------------------------------
    log.info("Extracting rootfs from %s → %s", rootfs_path, root_tree)
    run([unsquashfs, "-d", str(root_tree), str(rootfs_path)])

    # -- Remove legacy standalone-app artifacts left by stock/prior firmware -
    log.info("Removing legacy direct-open, touch-event, and helper artifacts")
    legacy_patterns = [
        "usr/bin/r1_audiobook_direct_open",
        "usr/bin/r1_audiobook_memscan",
        "usr/bin/r1_audiobook_refresh.sh",
        "usr/bin/r1_audiobook_resume_helper",
        "usr/bin/r1_touch_*.bin",
    ]
    for pattern in legacy_patterns:
        for path in root_tree.glob(pattern):
            try:
                path.unlink()
                log.info("Removed legacy artifact: %s", path.relative_to(root_tree))
            except OSError as exc:
                log.warning("Could not remove %s: %s", path, exc)

    # -- Build standalone audiobook app ------------------------------------
    log.info("Building standalone audiobook app")
    run(["sh", "app/build.sh"])
    built_app = resolve_path_strict("build/r1_audiobook_app")
    file_stdout = run(["file", str(built_app)], cwd=Path(__file__).resolve().parent.parent).stdout
    log.info("Using built app: %s (%d bytes, %s)", built_app, built_app.stat().st_size,
             "stripped" if "not stripped" not in file_stdout else "UNSTRIPPED")
    if "not stripped" in file_stdout:
        log.warning("Build produced an unstripped binary; firmware will be larger than 5 MB")

    # -- Patch hiby_player --------------------------------------------------
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

    # The order matters: launcher flag is checked first, then view-rows
    # may implicitly enable launcher (matching the PowerShell order).
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
    if args.include_audiobook_explorer_marker:
        player_patch_args.append("--audiobook-explorer-marker")
    if args.include_select_dispatch_branch:
        player_patch_args.append("--select-dispatch-branch")
    if args.skip_existing_patches:
        player_patch_args.append("--skip-existing-patches")

    log.info("Patching hiby_player")
    run_python("patch_hiby_player.py", player_patch_args)
    shutil.copy2(patched_player, player_path)

    # -- Patch resource text ------------------------------------------------
    resource_patch_args: list[str | Path] = [
        str(root_tree),
        "--about-model", args.custom_version_label,
        "--product-version", args.custom_version_id,
    ]
    if args.include_audiobook_native_hub_view_rows:
        resource_patch_args.append("--audiobook-native-hub-view-labels")
    elif args.include_audiobook_native_hub_title_row or args.include_audiobook_native_hub_folder_rows:
        resource_patch_args.append("--audiobook-native-hub-labels")
    log.info("Patching resource text")
    run_python("patch_r1_resource_text.py", resource_patch_args)

    # -- Generate launcher icons --------------------------------------------
    if args.include_audiobook_launcher_icon:
        log.info("Generating audiobook launcher icons")
        run_python("generate_audiobook_launcher_icons.py", [str(root_tree)])

    # -- Install standalone app + launcher wrapper + UI assets ----------------
    bin_dir = root_tree / "usr" / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    share_dir = root_tree / "usr" / "share" / "audiobooks"
    font_dir = share_dir / "fonts"
    theme_dir = share_dir / "hiby-theme"
    font_dir.mkdir(parents=True, exist_ok=True)
    theme_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built_app, bin_dir / "r1_audiobook_app")
    shutil.copy2(resolve_path_strict("tools/r1_audiobook_launch.sh"), bin_dir / "r1_audiobook_launch.sh")
    (bin_dir / "r1_audiobook_launch.sh").chmod(0o755)
    shutil.copy2(resolve_path_strict("app/assets/msyh.ttf"), font_dir / "msyh.ttf")
    if (root_tree / "usr" / "share" / "audiobooks" / "hiby-theme").exists():
        shutil.rmtree(root_tree / "usr" / "share" / "audiobooks" / "hiby-theme")
    shutil.copytree(resolve_path_strict("app/assets/hiby-theme"), theme_dir)

    # -- OTA info -----------------------------------------------------------
    ota_info_path = root_tree / "etc" / "ota_info"
    effective_ota_site = "/data/autoupdate/autoupdate"
    if ota_info_path.exists():
        ota_info_text = ota_info_path.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"^ota_site=(.+)$", ota_info_text, re.MULTILINE)
        if m:
            effective_ota_site = m.group(1).strip()
    if args.ota_site:
        effective_ota_site = args.ota_site
    if args.ota_version != 0 or args.ota_site:
        new_ota_info = f"ota_version={args.ota_version}\nota_site={effective_ota_site}\n"
        write_ascii(ota_info_path, new_ota_info)

    # -- Audio feature unlocks ----------------------------------------------
    audio_unlock_args: list[str | Path] = [str(root_tree)]
    if args.unlock_native_dsd:
        audio_unlock_args.append("--native-dsd")
    if args.enable_bluetooth_sbc_xq:
        audio_unlock_args.append("--sbc-xq")
    if args.unlock_usb_dac_mode:
        audio_unlock_args.append("--usb-dac")
    if len(audio_unlock_args) > 1:
        log.info("Patching audio feature unlocks")
        run_python("patch_r1_audio_feature_unlocks.py", audio_unlock_args)

    # -- Version marker -----------------------------------------------------
    custom_version_marker = root_tree / "etc" / "r1_audiobook_version"

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
    if args.include_audiobook_system_launcher:
        audiobook_entry_marker = "system-launcher"

    boot_adb_marker = "enabled" if args.enable_boot_adb else "disabled"
    batd_logger_marker = "disabled" if args.disable_batd_logger else "enabled"
    launcher_icon_marker = "audiobook" if args.include_audiobook_launcher_icon else "stock-book"
    audiobook_app_marker = "built"
    native_dsd_marker = "enabled" if args.unlock_native_dsd else "stock"
    sbc_xq_marker = "enabled" if args.enable_bluetooth_sbc_xq else "stock"
    usb_dac_marker = "enabled" if args.unlock_usb_dac_mode else "stock"
    native_hub_launcher_marker = "enabled" if include_native_hub_launcher else "disabled"
    native_hub_folder_rows_marker = "enabled" if args.include_audiobook_native_hub_folder_rows else "disabled"
    native_hub_view_rows_marker = "enabled" if args.include_audiobook_native_hub_view_rows else "disabled"
    audiobook_view_generation_enabled = "1" if args.include_audiobook_native_hub_view_rows else "0"
    resume_runtime_profile_marker = "disabled"
    audiobook_view_generation_enabled = "0"

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
        f"audiobook_app={audiobook_app_marker}\n"
        f"native_dsd={native_dsd_marker}\n"
        f"bluetooth_sbc_xq={sbc_xq_marker}\n"
        f"usb_dac_mode={usb_dac_marker}\n"
        f"native_hub_launcher={native_hub_launcher_marker}\n"
        f"native_hub_folder_rows={native_hub_folder_rows_marker}\n"
        f"native_hub_view_rows={native_hub_view_rows_marker}\n"
        f"resume_runtime_profile={resume_runtime_profile_marker}\n"
    )
    write_ascii(custom_version_marker, version_text)

    # -- Disable batd logger ------------------------------------------------
    if args.disable_batd_logger:
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

    # -- Replace hiby_player.sh with audiobook-aware wrapper ---------------
    if args.include_audiobook_system_launcher:
        player_launch_script = root_tree / "usr" / "bin" / "hiby_player.sh"
        hiby_sh = (Path(__file__).parent / "hiby_player.sh").read_text(encoding="utf-8", errors="replace")
        write_ascii(player_launch_script, hiby_sh.replace("\r\n", "\n").replace("\r", "\n"))

    # -- Enable boot ADB ----------------------------------------------------
    if args.enable_boot_adb:
        persistent_adb_boot_script = root_tree / "etc" / "init.d" / "S90adb"
        write_ascii(persistent_adb_boot_script, ADB_BOOT_SCRIPT.replace("\r\n", "\n").replace("\r", "\n"))

    # -- Audiobook resume runtime -------------------------------------------
    if args.include_audiobook_resume_runtime:
        _install_resume_runtime(args, root_tree)

    # -- Audiobook DB maintenance -------------------------------------------
    if args.include_audiobook_db_maintenance:
        _install_db_maintenance(args, root_tree, audiobook_view_generation_enabled)

    # -- Generate squashfs pseudo modes -------------------------------------
    log.info("Generating stock SquashFS pseudo modes")
    run_python(
        "write_squashfs_pseudo_modes.py",
        ["--rootfs", str(rootfs_path), "--unsquashfs", str(unsquashfs), "--output", str(pseudo_file)],
    )

    # Drop pseudo entries for legacy artifacts we just removed so mksquashfs doesn't warn.
    if pseudo_file.exists():
        keep_lines: list[str] = []
        for line in pseudo_file.read_text(encoding="ascii").splitlines():
            parts = line.split()
            if not parts:
                keep_lines.append(line)
                continue
            rel = parts[0].lstrip("/")
            if (root_tree / rel).exists():
                keep_lines.append(line)
            else:
                log.debug("Dropping pseudo entry for removed path: %s", rel)
        pseudo_file.write_text("\n".join(keep_lines) + "\n", encoding="ascii")

    # -- Apply new file mode overrides --------------------------------------
    new_file_mode_overrides: list[tuple[str, str]] = []
    if args.enable_boot_adb:
        new_file_mode_overrides.append(("etc/init.d/S90adb", "0755"))
    new_file_mode_overrides.extend([
        ("etc/init.d/S91audiobook_resume.sh", "0755"),
        ("etc/init.d/S92audiobook_db_maint.sh", "0755"),
        ("etc/r1_audiobook_version", "0644"),
        ("usr/bin/r1_audiobook_app", "0755"),
        ("usr/bin/r1_audiobook_launch.sh", "0755"),
        ("usr/bin/r1_audiobook_resume_daemon", "0755"),
        ("usr/bin/r1_audiobook_resume_daemon.sh", "0755"),
        ("usr/bin/r1_audiobook_resume_daemon_shell.sh", "0755"),
        ("usr/bin/r1_audiobook_db_maint", "0755"),
        ("usr/bin/r1_audiobook_db_watch.sh", "0755"),
        ("usr/bin/r1_usrlocal_media_seed.db", "0644"),
        ("usr/share/audiobooks/fonts/msyh.ttf", "0644"),
        ("usr/share/audiobooks/hiby-theme", "0644"),
        ("usr/bin/r1_audiobook_catalog.tsv", "0644"),
    ])

    pseudo_lines: list[str] = []
    for rel_path, mode in new_file_mode_overrides:
        if (root_tree / rel_path).exists():
            squash_path = rel_path.replace("\\", "/")
            pseudo_lines.append(f"{squash_path} m {mode} 0 0")

    with open(pseudo_file, "a", encoding="ascii") as f:
        for line in pseudo_lines:
            f.write(line + "\n")

    # -- Repack rootfs ------------------------------------------------------
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

    # -- Build UPT firmware package -----------------------------------------
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
# Resume runtime installation
# ---------------------------------------------------------------------------

def _install_resume_runtime(
    args: argparse.Namespace,
    root_tree: Path,
) -> None:
    """Install audiobook resume runtime files into the rootfs tree.

    For the standalone-app firmware path the legacy touch event streams,
    memscan, and direct-open helpers are intentionally not copied. Only the
    resume daemon and its wrappers remain so the daemon can still act as a
    lifecycle/power-event bridge if enabled.
    """
    log.info("Installing audiobook resume runtime")

    resume_daemon_path = resolve_path_strict(args.audiobook_resume_daemon)
    resume_daemon_wrapper_path = resolve_path_strict(args.audiobook_resume_wrapper)
    resume_daemon_shell_path = resolve_path_strict(args.audiobook_resume_shell)

    # Resume catalog (optional)
    resume_catalog_path: Optional[Path] = None
    if args.audiobook_resume_catalog:
        resume_catalog_path = resolve_path_strict(args.audiobook_resume_catalog)

    # -- Copy files into rootfs ---------------------------------------------
    bin_dir = root_tree / "usr" / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(resume_daemon_path, bin_dir / "r1_audiobook_resume_daemon")
    shutil.copy2(resume_daemon_wrapper_path, bin_dir / "r1_audiobook_resume_daemon.sh")
    shutil.copy2(resume_daemon_shell_path, bin_dir / "r1_audiobook_resume_daemon_shell.sh")

    if resume_catalog_path:
        shutil.copy2(resume_catalog_path, bin_dir / "r1_audiobook_catalog.tsv")

    # -- Write resume boot script -------------------------------------------
    audiobook_track_restore_enabled = "0"
    audiobook_book_title_autostart_enabled = "0"
    audiobook_direct_track_select_enabled = "0"
    audiobook_direct_track_preplay_enabled = "0"
    audiobook_book_title_memscan_enabled = "0"
    audiobook_direct_track_calibrate_enabled = "0"
    audiobook_direct_track_recovery_enabled = "0"
    audiobook_direct_open_enabled = "0"
    audiobook_ui_seek_fallback_enabled = "0"
    audiobook_back_guard_enabled = "0"
    audiobook_first_track_entry_restore_enabled = "0"

    resume_script_text = RESUME_BOOT_SCRIPT
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
    for placeholder, value in replacements.items():
        resume_script_text = resume_script_text.replace(placeholder, value)
    resume_script_text = resume_script_text.replace("\r\n", "\n").replace("\r", "\n")

    resume_boot_script = root_tree / "etc" / "init.d" / "S91audiobook_resume.sh"
    write_ascii(resume_boot_script, resume_script_text)


# ---------------------------------------------------------------------------
# DB maintenance installation
# ---------------------------------------------------------------------------

def _install_db_maintenance(
    args: argparse.Namespace,
    root_tree: Path,
    audiobook_view_generation_enabled: str,
) -> None:
    """Install audiobook DB maintenance files into the rootfs tree."""
    log.info("Installing audiobook DB maintenance")

    db_maint_helper_path = resolve_path_strict(args.audiobook_db_maint_helper)
    db_watch_path = resolve_path_strict(args.audiobook_db_watch)
    media_db_seed_path = resolve_path_strict(args.media_db_seed)

    bin_dir = root_tree / "usr" / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(db_maint_helper_path, bin_dir / "r1_audiobook_db_maint")
    shutil.copy2(db_watch_path, bin_dir / "r1_audiobook_db_watch.sh")
    shutil.copy2(media_db_seed_path, bin_dir / "r1_usrlocal_media_seed.db")

    db_maint_script_text = DB_MAINT_BOOT_SCRIPT
    db_maint_script_text = db_maint_script_text.replace(
        "__AUDIOBOOK_VIEW_GENERATION_ENABLED__", audiobook_view_generation_enabled
    )
    db_maint_script_text = db_maint_script_text.replace("\r\n", "\n").replace("\r", "\n")

    db_maint_boot_script = root_tree / "etc" / "init.d" / "S92audiobook_db_maint.sh"
    write_ascii(db_maint_boot_script, db_maint_script_text)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build HiBy R1 audiobook firmware from a stock rootfs and kernel image.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    # Path arguments
    parser.add_argument("--rootfs", default="work/original/rootfs.squashfs",
                        help="Path to stock rootfs squashfs (default: work/original/rootfs.squashfs)")
    parser.add_argument("--x-image", default="work/original/xImage",
                        help="Path to stock kernel xImage (default: work/original/xImage)")
    parser.add_argument("--out-dir", default="work/audiobook-firmware",
                        help="Output directory (default: work/audiobook-firmware)")
    parser.add_argument("--output-upt", default="work/audiobook-firmware/r1-audiobooks-dev-safe.upt",
                        help="Output UPT firmware path")
    parser.add_argument("--squashfs-tools", default=".deps/squashfs/tools/squashfs-tools",
                        help="Directory containing mksquashfs/unsquashfs (legacy, unused on Linux)")

    # Feature switches
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
    parser.add_argument("--include-audiobook-explorer-marker", action="store_true",
                        help="Add extended autostart marker for .m3u explorer callback")
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
                        help="Install audiobook resume runtime (daemon and wrappers only)")

    # Resume runtime helper paths
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

    # DB maintenance
    parser.add_argument("--include-audiobook-db-maintenance", action="store_true",
                        help="Install audiobook DB maintenance (db watcher, helper, seed)")
    parser.add_argument("--audiobook-db-maint-helper",
                        default="work/native-db-maint/r1_audiobook_db_maint",
                        help="Path to r1_audiobook_db_maint binary")
    parser.add_argument("--audiobook-db-watch",
                        default="tools/r1_audiobook_db_watch.sh",
                        help="Path to r1_audiobook_db_watch.sh")
    parser.add_argument("--media-db-seed",
                        default="firmware/seed/usrlocal_media.seed.db",
                        help="Path to media DB seed file")

    # Version / OTA
    parser.add_argument("--custom-version-id", default="1.6.16.5-audiobook",
                        help="Custom firmware version ID")
    parser.add_argument("--custom-version-label", default="HiBy R1 Audiobook FW 1.6.16.5",
                        help="Custom firmware version label")
    parser.add_argument("--ota-version", type=int, default=0,
                        help="OTA version number (non-negative integer)")
    parser.add_argument("--ota-site", default="",
                        help="OTA site URL override")

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
