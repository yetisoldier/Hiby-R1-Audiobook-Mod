#!/usr/bin/env python3
"""Post-flash on-device verification for the HiBy R1 audiobook release.

Connects to an R1 via ADB and verifies:
1. Version markers and firmware labels
2. Daemon / watcher process status
3. Database integrity and catalog invariants
4. Optional framebuffer screenshot capture
5. Reports pass/fail for each verification check

Artifacts are saved to a timestamped directory under --out-dir.
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

log = logging.getLogger("verify_installed")

DEFAULT_ADB_FALLBACK = "/home/yetisoldier/.local/bin/adb"
DEFAULT_CHECK_SCRIPT = "tools/check_audiobook_release_state.py"
DEFAULT_OUT_DIR = "work/installed-release-verification"
DEFAULT_EXPECTED_VERSION = "1.6.16.5-audiobook"
DEFAULT_MIN_USR_DATA_FREE_KB = 4096


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def resolve_adb_path(adb_arg: str) -> str:
    """Locate the adb binary."""
    if adb_arg:
        p = Path(adb_arg)
        if p.exists():
            return str(p.resolve())
        raise FileNotFoundError(f"Missing adb path: {adb_arg}")

    found = shutil.which("adb")
    if found:
        return found

    fallback = Path(DEFAULT_ADB_FALLBACK)
    if fallback.exists():
        return str(fallback)

    raise FileNotFoundError(
        "ADB not found. Install platform-tools, add adb to PATH, "
        f"or pass --adb. Fallback checked: {DEFAULT_ADB_FALLBACK}"
    )


def adb_shell(adb: str, command: str) -> str:
    """Run an adb shell command and return stdout. Raises on failure."""
    proc = subprocess.run(
        [adb, "shell", command],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"adb shell failed: {command}\nstderr: {proc.stderr.strip()}"
        )
    return proc.stdout


def adb_pull(adb: str, remote: str, local: str | Path) -> None:
    """Pull a file from the device. Raises on failure."""
    local_str = str(local)
    proc = subprocess.run(
        [adb, "pull", remote, local_str],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"adb pull failed: {remote}\nstderr: {proc.stderr.strip()}"
        )


def assert_contains(text: str, needle: str, label: str) -> None:
    """Assert that *text* contains *needle*, logging OK on success."""
    if needle not in text:
        raise AssertionError(f"{label} did not contain expected text: {needle}")
    log.info("OK   %s contains %s", label, needle)


def save_artifact(verify_dir: Path, name: str, content: str) -> None:
    """Write *content* to verify_dir/name."""
    (verify_dir / name).write_text(content, encoding="utf-8")


def assert_single_top_level_process(
    adb: str,
    verify_dir: Path,
    pattern: str,
    label: str,
    artifact_name: str,
    pid_file: str | None = None,
) -> None:
    """Verify exactly one top-level (PPid=1) process matches *pattern*."""
    shell_cmd = (
        "for p in $(ps | grep '__PATTERN__' | grep -v grep | awk '{print $1}'); do\n"
        "  ppid=$(awk '/^PPid:/ {print $2}' /proc/$p/status 2>/dev/null)\n"
        '  [ "$ppid" = 1 ] && echo "$p"\n'
        "done"
    ).replace("__PATTERN__", pattern)

    root_text = adb_shell(adb, shell_cmd)
    save_artifact(verify_dir, artifact_name, root_text)

    roots = [line.strip() for line in root_text.splitlines() if line.strip().isdigit()]
    if len(roots) != 1:
        raise AssertionError(
            f"{label} top-level process count expected 1, "
            f"found {len(roots)}: {', '.join(roots) if roots else '(none)'}"
        )

    if pid_file:
        pid_text = adb_shell(adb, f"cat {pid_file} 2>/dev/null || true")
        pid_artifact = artifact_name.replace(".txt", "_pidfile.txt")
        save_artifact(verify_dir, pid_artifact, pid_text)
        pid_from_file = pid_text.splitlines()[0].strip() if pid_text.strip() else ""
        if pid_from_file != roots[0]:
            raise AssertionError(
                f"{label} pidfile mismatch: root pid {roots[0]}, "
                f"pidfile {pid_file} contains [{pid_from_file}]"
            )

    log.info("OK   %s has one top-level process: %s", label, roots[0])


# ---------------------------------------------------------------------------
# Main verification
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Post-flash on-device verification for the HiBy R1 audiobook release."
    )
    parser.add_argument(
        "--adb",
        default="",
        help="Path to adb binary. If omitted, searched via PATH or fallback.",
    )
    parser.add_argument(
        "--check-script",
        default=DEFAULT_CHECK_SCRIPT,
        help="Path to the release-state check Python script.",
    )
    parser.add_argument(
        "--out-dir",
        default=DEFAULT_OUT_DIR,
        help="Directory for verification artifacts (timestamped sub-dir created inside).",
    )
    parser.add_argument(
        "--expected-version",
        default=DEFAULT_EXPECTED_VERSION,
        help="Expected version string in /etc/r1_audiobook_version.",
    )
    parser.add_argument(
        "--min-usr-data-free-kb",
        type=int,
        default=DEFAULT_MIN_USR_DATA_FREE_KB,
        help="Minimum free space (KB) required on /usr/data.",
    )
    parser.add_argument(
        "--require-db-maintenance",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Require the audiobook DB maintenance watcher to be running.",
    )
    parser.add_argument(
        "--require-play-mode-guard",
        action="store_true",
        help="Verify play-mode guard logic in the resume daemon.",
    )
    parser.add_argument(
        "--require-db-boot-stability-guard",
        action="store_true",
        help="Verify DB boot-stability guard logic in the DB watcher.",
    )
    parser.add_argument(
        "--require-context-start-guard",
        action="store_true",
        help="Verify context-start guard logic in the resume daemon.",
    )
    parser.add_argument(
        "--expect-native-dsd",
        action="store_true",
        help="Expect native DSD to be enabled on the device.",
    )
    parser.add_argument(
        "--expect-bluetooth-sbc-xq",
        action="store_true",
        help="Expect Bluetooth SBC XQ quality to be enabled.",
    )
    parser.add_argument(
        "--expect-usb-dac-mode",
        action="store_true",
        help="Expect USB DAC mode to be enabled.",
    )
    parser.add_argument(
        "--expect-conservative-resume-runtime",
        action="store_true",
        help="Expect conservative resume runtime profile.",
    )
    parser.add_argument(
        "--allow-staged-firmware",
        action="store_true",
        help="Allow (do not fail on) a staged firmware package on the SD card.",
    )
    parser.add_argument(
        "--capture-framebuffer",
        action="store_true",
        help="Capture a framebuffer screenshot via adb_capture_fb0.py.",
    )

    args = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )

    adb = resolve_adb_path(args.adb)

    check_script_path = Path(args.check_script)
    if not check_script_path.exists():
        raise FileNotFoundError(f"Missing check script: {check_script_path}")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    verify_dir = Path.cwd() / args.out_dir / stamp
    verify_dir.mkdir(parents=True, exist_ok=True)

    # -- adb devices --
    proc = subprocess.run(
        [adb, "devices"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    print(proc.stdout, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"adb devices failed\nstderr: {proc.stderr.strip()}")

    # -- /etc/r1_audiobook_version --
    version_text = adb_shell(adb, "cat /etc/r1_audiobook_version 2>/dev/null")
    save_artifact(verify_dir, "r1_audiobook_version.txt", version_text)
    assert_contains(version_text, f"version={args.expected_version}", "/etc/r1_audiobook_version")

    if args.expect_native_dsd:
        assert_contains(version_text, "native_dsd=enabled", "/etc/r1_audiobook_version")
    if args.expect_bluetooth_sbc_xq:
        assert_contains(version_text, "bluetooth_sbc_xq=enabled", "/etc/r1_audiobook_version")
    if args.expect_usb_dac_mode:
        assert_contains(version_text, "usb_dac_mode=enabled", "/etc/r1_audiobook_version")
    if args.expect_conservative_resume_runtime:
        assert_contains(version_text, "resume_runtime_profile=conservative", "/etc/r1_audiobook_version")

    # -- /usr/resource/config.json --
    config_text = adb_shell(adb, "cat /usr/resource/config.json 2>/dev/null")
    save_artifact(verify_dir, "resource_config.json", config_text)
    assert_contains(config_text, args.expected_version, "/usr/resource/config.json")

    if args.expect_usb_dac_mode:
        assert_contains(config_text, '"dac_to_store": 1', "/usr/resource/config.json")
        set_functions_text = adb_shell(adb, "cat /usr/resource/set_functions.json 2>/dev/null")
        save_artifact(verify_dir, "set_functions.json", set_functions_text)
        midi_set_functions_text = adb_shell(adb, "cat /usr/resource/midi_set_functions.json 2>/dev/null")
        save_artifact(verify_dir, "midi_set_functions.json", midi_set_functions_text)
        for flag in ("usb_mode", "dac_feedback", "car_mode", "standby", "about"):
            assert_contains(set_functions_text, f'"{flag}": 1', "/usr/resource/set_functions.json")
            assert_contains(midi_set_functions_text, f'"{flag}": 1', "/usr/resource/midi_set_functions.json")

    if args.expect_native_dsd:
        ot_devices_text = adb_shell(adb, "cat /usr/resource/ot_devices.json 2>/dev/null")
        save_artifact(verify_dir, "ot_devices.json", ot_devices_text)
        assert_contains(ot_devices_text, '"AnalogDsdNative": "native"', "/usr/resource/ot_devices.json")
        assert_contains(ot_devices_text, '"AnalogDsdD2p": "dop"', "/usr/resource/ot_devices.json")
        assert_contains(ot_devices_text, '"AnalogDsdDop": "dop"', "/usr/resource/ot_devices.json")

    if args.expect_bluetooth_sbc_xq:
        bt_init_text = adb_shell(adb, "cat /usr/bin/bt_init 2>/dev/null")
        save_artifact(verify_dir, "bt_init", bt_init_text)
        if "\r" in bt_init_text:
            raise AssertionError("/usr/bin/bt_init contains CR characters; BusyBox sh may fail to parse it")
        log.info("OK   /usr/bin/bt_init uses LF line endings")
        assert_contains(
            bt_init_text,
            "/usr/bin/bluealsa -p a2dp-source --a2dp-volume --sbc-quality=xq &",
            "/usr/bin/bt_init",
        )

    # -- resume daemon --
    daemon_text = adb_shell(adb, "ps | grep '[r]1_audiobook_resume_daemon' 2>/dev/null || true")
    save_artifact(verify_dir, "daemon.txt", daemon_text)
    if not daemon_text.strip():
        raise AssertionError("resume daemon is not running")
    log.info("OK   resume daemon is running")
    assert_single_top_level_process(
        adb, verify_dir,
        pattern="[r]1_audiobook_resume_daemon",
        label="resume daemon",
        artifact_name="resume_daemon_root_pids.txt",
        pid_file="/usr/data/audiobooks/resume-daemon.pid",
    )

    # -- runtime resume daemon script --
    runtime_daemon_script = adb_shell(
        adb,
        "cat /usr/data/audiobooks/bin/r1_audiobook_resume_daemon_shell.sh 2>/dev/null "
        "|| cat /usr/bin/r1_audiobook_resume_daemon_shell.sh 2>/dev/null",
    )
    save_artifact(verify_dir, "runtime_resume_daemon.sh", runtime_daemon_script)
    assert_contains(
        runtime_daemon_script,
        "LOG_MAX_BYTES=${AUDIOBOOK_RESUME_LOG_MAX_BYTES:-524288}",
        "runtime resume daemon",
    )
    assert_contains(runtime_daemon_script, "rotate_log_if_needed", "runtime resume daemon")
    assert_contains(
        runtime_daemon_script,
        "SAVE_BUCKET_MS=${AUDIOBOOK_SAVE_BUCKET_MS:-15000}",
        "runtime resume daemon",
    )
    assert_contains(
        runtime_daemon_script,
        "bucket=$((pos / SAVE_BUCKET_MS))",
        "runtime resume daemon",
    )

    if args.expect_conservative_resume_runtime:
        runtime_resume_init = adb_shell(adb, "cat /etc/init.d/S91audiobook_resume.sh 2>/dev/null")
        save_artifact(verify_dir, "runtime_resume_init.sh", runtime_resume_init)
        conservative_lines = [
            "AUDIOBOOK_TRACK_RESTORE_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED=0",
            "AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED=0",
            "AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED=0",
            "AUDIOBOOK_BACK_GUARD_ENABLED=0",
        ]
        for line in conservative_lines:
            assert_contains(runtime_resume_init, line, "runtime resume init")

    if args.require_play_mode_guard:
        assert_contains(
            runtime_daemon_script,
            "PLAY_MODE_TARGET=${AUDIOBOOK_PLAY_MODE_TARGET:-3}",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "PLAY_MODE_USER_INI_OFFSET=",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "ensure_audiobook_play_mode",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "play-mode skipped screen-not-ready",
            "runtime resume daemon",
        )

        daemon_log_play_mode = adb_shell(
            adb,
            "grep -a 'play_mode_target=3' /usr/data/audiobooks/resume-daemon.log 2>/dev/null | tail -5 || true",
        )
        save_artifact(verify_dir, "resume_daemon_play_mode_log.txt", daemon_log_play_mode)
        assert_contains(daemon_log_play_mode, "play_mode_target=3", "resume daemon log")

        play_mode_byte = adb_shell(
            adb,
            "dd if=/usr/data/user.ini bs=1 skip=592 count=1 2>/dev/null | od -An -tu1 2>/dev/null || true",
        )
        save_artifact(verify_dir, "play_mode_byte.txt", play_mode_byte)
        log.info("OK   captured current play-mode byte: %s", play_mode_byte.strip())

    if args.require_context_start_guard:
        assert_contains(runtime_daemon_script, "allow_memscan_root", "runtime resume daemon")
        assert_contains(
            runtime_daemon_script,
            "book_title_should_preplay_direct_start",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "book_title_preplay_allow_memscan_root",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "launcher|context|path|relaxed) printf",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "book-title touch-first skipped reason=launcher",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "book-title launcher-track-list back-to-title-list",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "restored_path:-",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "autostart_restore_active",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "book-title direct-start skipped reason=",
            "runtime resume daemon",
        )
        assert_contains(
            runtime_daemon_script,
            "restore settle after track restore path=",
            "runtime resume daemon",
        )

    # -- DB maintenance --
    if args.require_db_maintenance:
        db_maint_text = adb_shell(adb, "ps | grep '[r]1_audiobook_db_watch' 2>/dev/null || true")
        save_artifact(verify_dir, "db_maint_daemon.txt", db_maint_text)
        if not db_maint_text.strip():
            raise AssertionError("audiobook DB maintenance watcher is not running")
        log.info("OK   audiobook DB maintenance watcher is running")
        assert_single_top_level_process(
            adb, verify_dir,
            pattern="[r]1_audiobook_db_watch",
            label="audiobook DB maintenance watcher",
            artifact_name="db_maint_root_pids.txt",
            pid_file="/usr/data/audiobooks/db-maint.ssd.pid",
        )

        db_maint_files = adb_shell(
            adb,
            "ls -l /usr/data/audiobooks/bin/r1_audiobook_db_maint "
            "/usr/data/audiobooks/bin/r1_audiobook_db_watch.sh "
            "/usr/bin/r1_audiobook_db_maint "
            "/usr/bin/r1_audiobook_db_watch.sh 2>/dev/null",
        )
        save_artifact(verify_dir, "db_maint_files.txt", db_maint_files)
        assert_contains(db_maint_files, "r1_audiobook_db_maint", "DB maintenance helper files")
        assert_contains(db_maint_files, "r1_audiobook_db_watch.sh", "DB maintenance watcher files")

        db_maint_helper_strings = adb_shell(
            adb,
            "grep -a 'audiobook route genre missing' /usr/data/audiobooks/bin/r1_audiobook_db_maint 2>/dev/null || true",
        )
        save_artifact(verify_dir, "db_maint_helper_route_strings.txt", db_maint_helper_strings)
        assert_contains(
            db_maint_helper_strings,
            "audiobook route genre missing",
            "DB maintenance helper binary",
        )

        db_maint_needs_text = adb_shell(
            adb,
            "/usr/data/audiobooks/bin/r1_audiobook_db_maint "
            "--db /usr/data/usrlocal_media.db --needs-maintenance --verbose 2>&1; echo rc=$?",
        )
        save_artifact(verify_dir, "db_maint_needs_maintenance.txt", db_maint_needs_text)
        assert_contains(
            db_maint_needs_text,
            "audiobook route genre missing: 0",
            "DB maintenance helper needs-maintenance output",
        )
        assert_contains(
            db_maint_needs_text,
            "needs maintenance: no",
            "DB maintenance helper needs-maintenance output",
        )

        db_watch_script = adb_shell(
            adb,
            "cat /usr/data/audiobooks/bin/r1_audiobook_db_watch.sh 2>/dev/null "
            "|| cat /usr/bin/r1_audiobook_db_watch.sh 2>/dev/null",
        )
        save_artifact(verify_dir, "runtime_db_watch.sh", db_watch_script)
        assert_contains(
            db_watch_script,
            "LOG_MAX_BYTES=${AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES:-524288}",
            "runtime DB watcher",
        )
        assert_contains(db_watch_script, "rotate_log_if_needed", "runtime DB watcher")
        assert_contains(
            db_watch_script,
            "LOCK_DIR=${AUDIOBOOK_DB_MAINT_LOCK:-$BASE/db-maint.lock}",
            "runtime DB watcher",
        )
        assert_contains(db_watch_script, "pid_is_db_watcher", "runtime DB watcher")
        assert_contains(db_watch_script, "stale-lock-live-pid-not-watcher", "runtime DB watcher")
        assert_contains(
            db_watch_script,
            "trap 'cleanup; exit 0' HUP INT TERM",
            "runtime DB watcher",
        )
        assert_contains(db_watch_script, "exit reason=already-running", "runtime DB watcher")
        assert_contains(
            db_watch_script,
            'compare_last_size=$(signature_size "$last_sig")',
            "runtime DB watcher",
        )

        if args.require_db_boot_stability_guard:
            boot_stability_checks = [
                'BOOT_STABLE_TIMEOUT_SECONDS=${AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS:-180}',
                'STABLE_POLL_SECONDS=${AUDIOBOOK_DB_STABLE_POLL_SECONDS:-3}',
                "wait_for_stable_db boot",
                "wait-stable-timeout reason=",
                "AUDIOBOOK_DB_MIRROR_PATHS=",
                "/data/usrlocal_media.db",
                "$SD_ROOT/usrlocal_media.db",
                "run_maint_one_db",
                'run_maint_one_db "$reason" "$mirror_db" mirror',
                "copy_primary_to_mirror",
                "mirror-copy reason=",
                "copy_db_to_primary",
                "primary-copy reason=",
                "promote_clean_sd_db",
                "mirror_db_signature",
                "mirror-db-change",
                "mirror-stable",
                "promoted-sd",
                "any_db_needs_maintenance",
                "--needs-maintenance",
                "content-repair-mtime",
                "boot_stable_timeout=",
                "zero_audio_retry=",
                "locked_db_retry=",
                "run_maint_with_retries boot",
                "retry_zero_audiobooks_if_needed",
                "zero-audiobook-retry-ready",
                "defer-zero-audiobooks",
                "mirror-skip reason=$reason db=$mirror_db primary-transient",
                "LOCKED_DB_RETRY_TIMEOUT_SECONDS=${AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS:-600}",
                "failed-locked reason=",
                "locked-db-retry-start",
                "audiobook_tracks=${LAST_AUDIOBOOK_TRACKS:-unknown}",
            ]
            for check in boot_stability_checks:
                assert_contains(db_watch_script, check, "runtime DB watcher")

            db_init_script = adb_shell(adb, "cat /etc/init.d/S92audiobook_db_maint.sh 2>/dev/null")
            save_artifact(verify_dir, "runtime_db_init.sh", db_init_script)
            init_checks = [
                "AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=180",
                "AUDIOBOOK_DB_STABLE_POLL_SECONDS=3",
                "AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=600",
                "AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS=5",
                "AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS=600",
                "AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS=5",
                "db_watch_pid_is_live()",
                "stop_db_watch()",
                'kill -9 "$old_pid"',
                'rm -rf "$BASE/db-maint.lock"',
            ]
            for check in init_checks:
                assert_contains(db_init_script, check, "runtime DB init")

    # -- staged firmware (r1.upt) --
    upt_text = adb_shell(
        adb,
        "if [ -e /usr/data/mnt/sd_0/r1.upt ]; then ls -l /usr/data/mnt/sd_0/r1.upt; else echo no-r1.upt; fi",
    )
    save_artifact(verify_dir, "r1_upt_status.txt", upt_text)
    if args.allow_staged_firmware:
        if "no-r1.upt" in upt_text:
            log.info("OK   no staged firmware package is present")
        else:
            log.info("OK   staged firmware package is present by request")
    else:
        assert_contains(upt_text, "no-r1.upt", "SD update trigger status")

    # -- user.ini audiobook refs --
    user_ini_refs = adb_shell(
        adb,
        "grep -a -i 'Audiobook\\|Engulfed\\|Squirrel\\|Ice Like Fire' /usr/data/user.ini 2>/dev/null || true",
    )
    save_artifact(verify_dir, "user_ini_audiobook_refs.txt", user_ini_refs)
    if not user_ini_refs.strip():
        log.info("OK   user.ini has no saved-last audiobook references")
    else:
        log.warning(
            "WARN user.ini contains audiobook-related text; review %s",
            verify_dir / "user_ini_audiobook_refs.txt",
        )

    # -- disk free space --
    df_text = adb_shell(adb, "df -k /usr/data /usr/data/mnt/sd_0 2>/dev/null")
    save_artifact(verify_dir, "df.txt", df_text)
    usr_data_line = None
    for line in df_text.splitlines():
        if re.search(r"\s/usr/data$", line):
            usr_data_line = line
            break
    if usr_data_line:
        m = re.match(r"^\S+\s+\d+\s+\d+\s+(\d+)\s+", usr_data_line)
        if m:
            free_kb = int(m.group(1))
            if free_kb < args.min_usr_data_free_kb:
                raise AssertionError(
                    f"/usr/data free space is below threshold: "
                    f"{free_kb}KB < {args.min_usr_data_free_kb}KB"
                )
            log.info("OK   /usr/data free space %dKB", free_kb)
        else:
            log.warning("WARN could not parse /usr/data free space")
    else:
        log.warning("WARN could not find /usr/data line in df output")

    # -- pull DB and catalogs --
    db_local = verify_dir / "usrlocal_media.db"
    catalog_local = verify_dir / "catalog.tsv"
    books_catalog_local = verify_dir / "catalog-books.tsv"
    titles_catalog_local = verify_dir / "catalog-view-title.tsv"
    authors_catalog_local = verify_dir / "catalog-view-author.tsv"
    series_catalog_local = verify_dir / "catalog-view-series.tsv"

    adb_pull(adb, "/usr/data/usrlocal_media.db", db_local)
    adb_pull(adb, "/usr/data/audiobooks/catalog.tsv", catalog_local)

    books_catalog_arg: list[str] = []
    view_catalog_args: list[str] = []

    books_catalog_presence = adb_shell(
        adb,
        "if [ -s /usr/data/audiobooks/catalog-books.tsv ]; then echo present; else echo missing; fi",
    )
    if "present" in books_catalog_presence:
        adb_pull(adb, "/usr/data/audiobooks/catalog-books.tsv", books_catalog_local)
        books_catalog_arg = ["--books-catalog", str(books_catalog_local)]

    for remote_name, local_path, flag_name in [
        ("catalog-view-title.tsv", titles_catalog_local, "--titles-catalog"),
        ("catalog-view-author.tsv", authors_catalog_local, "--authors-catalog"),
        ("catalog-view-series.tsv", series_catalog_local, "--series-catalog"),
    ]:
        presence = adb_shell(
            adb,
            f"if [ -s /usr/data/audiobooks/{remote_name} ]; then echo present; else echo missing; fi",
        )
        if "present" in presence:
            adb_pull(adb, f"/usr/data/audiobooks/{remote_name}", local_path)
            view_catalog_args.extend([flag_name, str(local_path)])

    # -- run release-state check script --
    check_cmd = [
        sys.executable,
        str(check_script_path),
        str(db_local),
        "--catalog",
        str(catalog_local),
        *books_catalog_arg,
        *view_catalog_args,
        "--expect-audiobooks",
    ]
    proc = subprocess.run(check_cmd)
    if proc.returncode != 0:
        raise RuntimeError("release-state database check failed")

    # -- compute hashes --
    hash_lines: list[str] = []
    for path in [
        db_local,
        catalog_local,
        books_catalog_local,
        titles_catalog_local,
        authors_catalog_local,
        series_catalog_local,
    ]:
        if not path.exists():
            continue
        md5 = hashlib.md5(path.read_bytes()).hexdigest()
        size = path.stat().st_size
        hash_lines.append(f"{md5}  {size}  {path}")
    (verify_dir / "hashes.txt").write_text("\n".join(hash_lines) + "\n", encoding="utf-8")

    # -- dev artifact check --
    dev_artifact_command = (
        'for n in debug-daemon.out debug-daemon.pid dmr-probe.out helper-current.out '
        'helper-current.strace mem-pos-near.bin player-restart.out player-restart2.out '
        'position-watch-holidays-on-ice-2008.nohup.log '
        'position-watch-holidays-on-ice-2008.pid '
        'position-watch-holidays.loop.log position-watch-holidays.nohup.log '
        'position-watch-holidays.pid ptrwins r1_audiobook_resume_daemon.syntax-test.sh '
        'resume-daemon.testpid resume-daemon.trace scan_skip_runtime_patch.json '
        'tracklist-window.bin; do\n'
        '  [ -e "/usr/data/audiobooks/$n" ] && echo "$n"\n'
        "done\n"
        "for n in r1_audiobook_position_watch.sh r1_audiobook_resume.sh "
        "r1_audiobook_resume_daemon.sh.syntaxcheck r1_dmr_probe_helper "
        "r1_unix_socket_write r1_utf16_root_scan_probe.sh; do\n"
        '  [ -e "/usr/data/audiobooks/bin/$n" ] && echo "bin/$n"\n'
        "done"
    )
    dev_artifact_check = adb_shell(adb, dev_artifact_command)
    save_artifact(verify_dir, "dev_artifacts_remaining.txt", dev_artifact_check)
    if not dev_artifact_check.strip():
        log.info("OK   no known development artifacts remain in /usr/data/audiobooks")
    else:
        log.warning(
            "WARN known development artifacts remain; review %s",
            verify_dir / "dev_artifacts_remaining.txt",
        )

    # -- framebuffer capture --
    if args.capture_framebuffer:
        fb_png = verify_dir / "fb0.png"
        fb_script = Path("tools/adb_capture_fb0.py")
        fb_cmd = [sys.executable, str(fb_script), "--adb", adb, "--output", str(fb_png)]
        proc = subprocess.run(fb_cmd)
        if proc.returncode != 0:
            raise RuntimeError("framebuffer capture failed")

    # -- done --
    print()
    print("Installed audiobook release verification passed.")
    print(f"Artifacts: {verify_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
