#!/usr/bin/env python3
"""Verify the rebuilt HiBy R1 audiobook development package layout.

This is a local pre-flash sanity check. It does not talk to the device and does
not modify any files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


EXPECTED_CURRENT_HASHES = {
    "r1-audiobooks-1.6.11-audiobook.upt": {
        "md5": "208b7312800e4c26af79d9af7cd5570d",
        "sha256": "bd353cd343d9968c532b2df8a9fd6ee74cb2e8dff66531bbb10a55bb5734abad",
    },
    "r1-audiobooks-1.6.9-audiobook.upt": {
        "md5": "3c3b3f05724acc474fb349e6378fc351",
        "sha256": "f78e67089ff84021b18d69a4af2cb01be6f872bc59d187bf9cba256f8cd792aa",
    },
    "r1-audiobooks-1.6.4-audiobook.upt": {
        "md5": "71c8d0d94bf50529a06aa9a31350f595",
        "sha256": "02b286676d93ec683307820e1ef40288f34ef21a42a24f5cbda361f2d3733b7b",
    },
    "rootfs.squashfs": {
        "md5": "34ac94a36a27ab32a082f340e2db260c",
        "sha256": "85bab0efbeb3f6931195a968eae02d3576b6edb2b0bb4f85682c5408b6a9c15c",
    },
    "r1-audiobooks-1.6.3-audiobook.upt": {
        "md5": "1954b92ae7a394a0dc450c2d5f70f3d2",
        "sha256": "e906ca7345cb3a467fadfe8e8c68dac7e9626e65503d4d49d4faf9cf9b2831c1",
    },
    "r1-audiobooks-1.6.2-audiobook.upt": {
        "md5": "1022a7dfe0cb9e73f35494e81f45b58f",
        "sha256": "9138fd1e91c008205f81857095c50341898d535fae11cc42edec6ed12556e519",
    },
    "squashfs-root/usr/bin/hiby_player": {
        "md5": "09997a636c94112ff76c85a6d4a8d0ff",
        "sha256": "f49ea55a48c1bdf1398a2a6672b1d596516650f7ebe77846ba7c33a5cfee329c",
    },
}

KNOWN_BAD_MD5 = {
    # First flashed package: rootfs repack left hiby_player non-executable.
    "2dc1152f096e84b3b8b52f809fc30e59",
    # Second flashed package: executable modes fixed, but booted to black screen/no ADB.
    "3bed523d5843522186164029139db7b1",
}

PLAYER_BYTE_CHECKS = {
    "audiobook launcher source marker @0x35DAEC": (
        0x35DAEC,
        bytes.fromhex(
            "a0fbbd275c04bfaf5804b1af5404b0af5000b08c18000012"
            "000000002800048ee0381c0c000000001300401000000000"
            "252000027600113ca8db31267800053ca070a5247600063c"
            "80dbc6248e00083c00400835b0a0093c15052935200009ad"
        ),
    ),
    "audiobook launcher root source marker @0x35DBC0": (
        0x35DBC0,
        bytes.fromhex(
            "a0fbbd275c04bfaf5804b1af5404b0af2580800013000012"
            "00000000252000027600113ca8db31267800053ca070a524"
            "7600063c80dbc6248e00083c00400835b0a0093c15052935"
            "200009ad"
        ),
    ),
    "audiobook title marker hook @0x09FE40": (0x09FE40, bytes.fromhex("80771d0800000000")),
    "old title wrapper remains stock @0x0A1780": (0x0A1780, bytes.fromhex("5000a58c7800063c")),
    "marker cave prefix @0x35DE00": (
        0x35DE00,
        bytes.fromhex(
            "8e00083c00400835dec0093c174a2935000009ad04001fad"
            "080004ad0c0005ad100006ad140007ad180005ad28000a8d"
            "01004a2528000aadc8fdbd272c02b2af927f120800000000"
        ),
    ),
    "audiobook launcher callback @0x482030": (0x482030, bytes.fromhex("ecda7500")),
    "book launcher root hook @0x140F20": (0x140F20, bytes.fromhex("f0761d0800000000")),
}

PACKET_SIZES = {
    "squashfs-root/usr/bin/r1_touch_next_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_first_track_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_first_track_down_event1.bin": 128,
    "squashfs-root/usr/bin/r1_touch_first_track_move_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_first_track_up_event1.bin": 48,
    "squashfs-root/usr/bin/r1_touch_back_event1.bin": 2080,
    "squashfs-root/usr/bin/r1_touch_track_row1_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_track_row2_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_track_row3_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_track_row4_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_track_row5_event1.bin": 960,
    "squashfs-root/usr/bin/r1_touch_track_swipe_down_event1.bin": 128,
    "squashfs-root/usr/bin/r1_touch_track_swipe_move1_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_track_swipe_move2_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_track_swipe_move3_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_track_swipe_move4_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_track_swipe_move5_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_track_swipe_move6_event1.bin": 112,
    "squashfs-root/usr/bin/r1_touch_track_swipe_up_event1.bin": 48,
    "squashfs-root/usr/bin/r1_key_next_event0.bin": 64,
    "squashfs-root/usr/bin/r1_key_prev_event2.bin": 64,
}

NEW_FILE_MODE_CHECKS = {
    "squashfs-root/etc/init.d/S91audiobook_resume.sh": "-rwxr-xr-x",
    "squashfs-root/etc/r1_audiobook_version": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_audiobook_resume_helper": "-rwxr-xr-x",
    "squashfs-root/usr/bin/r1_audiobook_resume_daemon.sh": "-rwxr-xr-x",
    "squashfs-root/usr/bin/r1_touch_next_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_first_track_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_first_track_down_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_first_track_move_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_first_track_up_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_back_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_row1_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_row2_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_row3_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_row4_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_row5_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_down_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_move1_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_move2_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_move3_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_move4_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_move5_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_move6_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_touch_track_swipe_up_event1.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_key_next_event0.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_key_prev_event2.bin": "-rw-r--r--",
    "squashfs-root/usr/bin/r1_audiobook_catalog.tsv": "-rw-r--r--",
}

BOOT_ADB_FILE_MODE_CHECKS = {
    "squashfs-root/etc/init.d/S90adb": "-rwxr-xr-x",
}

DB_MAINT_FILE_MODE_CHECKS = {
    "squashfs-root/etc/init.d/S92audiobook_db_maint.sh": "-rwxr-xr-x",
    "squashfs-root/usr/bin/r1_audiobook_db_maint": "-rwxr-xr-x",
    "squashfs-root/usr/bin/r1_audiobook_db_watch.sh": "-rwxr-xr-x",
    "squashfs-root/usr/bin/r1_usrlocal_media_seed.db": "-rw-r--r--",
}

SQUASHFS_LINE_RE = re.compile(
    r"^(?P<mode>\S+)\s+(?P<owner>.+?)\s+\d+\s+"
    r"\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}\s+(?P<path>squashfs-root(?:/.*)?)$"
)


@dataclass(frozen=True)
class SquashfsEntry:
    mode: str
    owner: str
    link_target: str = ""


def digest(path: Path, algorithm: str) -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require(condition: bool, message: str, failures: list[str]) -> None:
    if condition:
        print(f"OK   {message}")
    else:
        print(f"FAIL {message}")
        failures.append(message)


def read_ini_value(path: Path, key: str) -> str | None:
    if not path.exists():
        return None
    prefix = f"{key}="
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    return None


def squashfs_entries(rootfs: Path, unsquashfs: Path) -> dict[str, SquashfsEntry]:
    if not rootfs.exists() or not unsquashfs.exists():
        return {}
    proc = subprocess.run(
        [str(unsquashfs), "-ll", str(rootfs)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    entries: dict[str, SquashfsEntry] = {}
    for line in proc.stdout.splitlines():
        match = SQUASHFS_LINE_RE.match(line)
        if not match:
            continue
        raw_path = match.group("path")
        path, separator, link_target = raw_path.partition(" -> ")
        entries[path] = SquashfsEntry(
            mode=match.group("mode"),
            owner=match.group("owner"),
            link_target=link_target if separator else "",
        )
    return entries


def require_stock_modes(
    rebuilt_entries: dict[str, SquashfsEntry],
    stock_entries: dict[str, SquashfsEntry],
    failures: list[str],
) -> None:
    missing = sorted(set(stock_entries) - set(rebuilt_entries))
    mismatched = [
        (path, stock_entries[path].mode, rebuilt_entries[path].mode)
        for path in sorted(stock_entries)
        if path in rebuilt_entries and stock_entries[path].mode != rebuilt_entries[path].mode
    ]
    mismatched_links = [
        (path, stock_entries[path].link_target, rebuilt_entries[path].link_target)
        for path in sorted(stock_entries)
        if (
            path in rebuilt_entries
            and stock_entries[path].mode.startswith("l")
            and stock_entries[path].link_target != rebuilt_entries[path].link_target
        )
    ]
    if missing:
        print("FAIL stock paths are present in rebuilt rootfs")
        for path in missing[:20]:
            print(f"     missing {path}")
        if len(missing) > 20:
            print(f"     ... {len(missing) - 20} more")
        failures.append("stock paths are present in rebuilt rootfs")
    else:
        print(f"OK   all {len(stock_entries)} stock paths are present in rebuilt rootfs")

    if mismatched:
        print("FAIL stock file modes match rebuilt rootfs")
        for path, expected, actual in mismatched[:20]:
            print(f"     {path}: expected {expected}, got {actual}")
        if len(mismatched) > 20:
            print(f"     ... {len(mismatched) - 20} more")
        failures.append("stock file modes match rebuilt rootfs")
    else:
        print(f"OK   stock file modes match rebuilt rootfs for {len(stock_entries)} paths")

    if mismatched_links:
        print("FAIL stock symlink targets match rebuilt rootfs")
        for path, expected, actual in mismatched_links[:20]:
            print(f"     {path}: expected {expected}, got {actual}")
        if len(mismatched_links) > 20:
            print(f"     ... {len(mismatched_links) - 20} more")
        failures.append("stock symlink targets match rebuilt rootfs")
    else:
        link_count = sum(1 for entry in stock_entries.values() if entry.mode.startswith("l"))
        print(f"OK   stock symlink targets match rebuilt rootfs for {link_count} links")


def require_all_root_owned(entries: dict[str, SquashfsEntry], failures: list[str]) -> None:
    non_root = [
        (path, entry.owner)
        for path, entry in sorted(entries.items())
        if entry.owner != "0/0"
    ]
    if non_root:
        print("FAIL rebuilt rootfs entries are root-owned")
        for path, owner in non_root[:20]:
            print(f"     {path}: {owner}")
        if len(non_root) > 20:
            print(f"     ... {len(non_root) - 20} more")
        failures.append("rebuilt rootfs entries are root-owned")
    else:
        print(f"OK   rebuilt rootfs entries are root-owned ({len(entries)} paths)")


def find_upt(out_dir: Path, preferred_name: str) -> Path:
    preferred = out_dir / preferred_name
    if preferred.exists():
        return preferred
    candidates = sorted(out_dir.glob("*.upt"))
    if len(candidates) == 1:
        return candidates[0]
    return preferred


def verify(
    out_dir: Path,
    *,
    expect_current_hashes: bool,
    expected_version: str,
    expected_label: str,
    require_db_maintenance: bool,
    require_boot_adb: bool,
    stock_rootfs: Path,
    unsquashfs: Path,
    upt_name: str,
) -> int:
    failures: list[str] = []
    root = out_dir / "squashfs-root"
    player = root / "usr/bin/hiby_player"
    upt = find_upt(out_dir, upt_name)
    rootfs = out_dir / "rootfs.squashfs"

    required_paths = [upt, rootfs, player, root / "etc/init.d/S91audiobook_resume.sh"]
    if require_boot_adb:
        required_paths.append(root / "etc/init.d/S90adb")
    if require_db_maintenance:
        required_paths.extend(
            [
                root / "etc/init.d/S92audiobook_db_maint.sh",
                root / "usr/bin/r1_audiobook_db_maint",
                root / "usr/bin/r1_audiobook_db_watch.sh",
            ]
        )
    for path in required_paths:
        require(path.exists(), f"exists: {path}", failures)

    if player.exists():
        player_bytes = player.read_bytes()
        for label, (offset, expected) in PLAYER_BYTE_CHECKS.items():
            found = player_bytes[offset : offset + len(expected)]
            require(found == expected, f"{label}: {found.hex()}", failures)

    for relative, expected_size in PACKET_SIZES.items():
        path = out_dir / relative
        actual_size = path.stat().st_size if path.exists() else -1
        require(actual_size == expected_size, f"{relative} size {actual_size}", failures)

    entries = squashfs_entries(rootfs, unsquashfs)
    stock_entries = squashfs_entries(stock_rootfs, unsquashfs)
    require(bool(entries), f"read rebuilt squashfs entries with {unsquashfs}", failures)
    require(bool(stock_entries), f"read stock squashfs entries from {stock_rootfs}", failures)
    if entries and stock_entries:
        require_stock_modes(entries, stock_entries, failures)
        require_all_root_owned(entries, failures)
    for relative, expected_mode in NEW_FILE_MODE_CHECKS.items():
        actual_mode = entries.get(relative, SquashfsEntry("", "")).mode
        require(actual_mode == expected_mode, f"{relative} mode {actual_mode}", failures)
    if require_boot_adb:
        for relative, expected_mode in BOOT_ADB_FILE_MODE_CHECKS.items():
            actual_mode = entries.get(relative, SquashfsEntry("", "")).mode
            require(actual_mode == expected_mode, f"{relative} mode {actual_mode}", failures)
        boot_adb_script = root / "etc/init.d/S90adb"
        if boot_adb_script.exists():
            text = boot_adb_script.read_text(errors="replace")
            require("\r" not in text, "boot ADB wrapper uses LF line endings", failures)
            require("skip=1856" in text, "boot ADB wrapper reads USB working mode offset", failures)
            require('mode" != "1"' in text, "boot ADB wrapper requires Device USB working mode", failures)
            require("/etc/init.d/T90adb start" in text, "boot ADB wrapper delegates to stock helper", failures)
    else:
        for relative in BOOT_ADB_FILE_MODE_CHECKS:
            actual_mode = entries.get(relative, SquashfsEntry("", "")).mode
            if actual_mode:
                print(f"OK   optional boot ADB present: {relative} mode {actual_mode}")
            else:
                print(f"OK   optional boot ADB absent: {relative}")
    if require_db_maintenance:
        for relative, expected_mode in DB_MAINT_FILE_MODE_CHECKS.items():
            actual_mode = entries.get(relative, SquashfsEntry("", "")).mode
            require(actual_mode == expected_mode, f"{relative} mode {actual_mode}", failures)

    version_marker = root / "etc/r1_audiobook_version"
    if version_marker.exists():
        version_text = version_marker.read_text(encoding="ascii", errors="replace")
        require(f"version={expected_version}" in version_text, f"custom version marker has version={expected_version}", failures)
        require(f"label={expected_label}" in version_text, "custom version marker has visible label", failures)
        require("base_firmware=1.6" in version_text, "custom version marker records stock base firmware", failures)
        if require_boot_adb:
            require("boot_adb=enabled" in version_text, "custom version marker records boot ADB enabled", failures)
        elif "boot_adb=" in version_text:
            print("OK   custom version marker records boot ADB state")
    else:
        require(False, "custom version marker exists", failures)

    about_dev = root / "usr/resource/str/english/about_dev.ini"
    if about_dev.exists():
        about_text = about_dev.read_text(encoding="utf-16", errors="replace")
        require(f"<model>{expected_label}</model>" in about_text, "About screen model shows custom firmware label", failures)
    else:
        require(False, "English About resource exists", failures)

    config_json = root / "usr/resource/config.json"
    if config_json.exists():
        try:
            config_data = json.loads(config_json.read_text(encoding="utf-8"))
            product = next((entry for entry in config_data if isinstance(entry, dict) and entry.get("type") == "product"), {})
            require(product.get("version") == expected_version, "resource config product version shows custom firmware", failures)
            require(product.get("device") == "R1", "resource config keeps R1 product device", failures)
        except Exception as exc:
            print(f"FAIL resource config parses: {exc}")
            failures.append("resource config parses")
    else:
        require(False, "resource config exists", failures)

    resume_init = root / "etc/init.d/S91audiobook_resume.sh"
    if resume_init.exists():
        resume_init_text = resume_init.read_text(encoding="ascii", errors="replace")
        require("\r" not in resume_init_text, "resume init script uses LF line endings for BusyBox redirects", failures)
        require('case "$1" in' not in resume_init_text, "resume init script avoids BusyBox case parse issue", failures)
        require(
            'start-stop-daemon -S -b -m -p "$BASE/resume-daemon.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_resume_daemon.sh" >>"$BASE/resume-daemon.stdout.log" 2>&1'
            in resume_init_text,
            "resume init launches daemon through /bin/sh with redirected stdio for BusyBox start-stop-daemon",
            failures,
        )
        require("clear_stock_audiobook_last_file" in resume_init_text, "resume init clears stock last audiobook path before player start", failures)
        require("count=320" in resume_init_text, "resume init clears stock last-file slot and adjacent fragments", failures)
        require("00003a005c0041007500640069006f0062006f006f006b007300" in resume_init_text, "resume init also clears partially nulled audiobook path", failures)
        require("AUDIOBOOK_INTERVAL_SECONDS=1" in resume_init_text, "resume init script uses tuned 1s polling", failures)
        require("AUDIOBOOK_IDLE_INTERVAL_SECONDS=5" in resume_init_text, "resume init script uses lower-power idle polling", failures)
        require(
            "AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS=15" in resume_init_text,
            "resume init throttles title marker polling during music playback",
            failures,
        )
        require(
            "AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS=60" in resume_init_text,
            "resume init enables low-rate runtime diagnostics",
            failures,
        )
        require("AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS=1" in resume_init_text, "resume init script uses tuned title autostart delay", failures)
        require("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=1" in resume_init_text, "resume init enables pre-play direct track selection", failures)
        require("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4" in resume_init_text, "resume init sets direct track list geometry", failures)
        require("AUDIOBOOK_NEW_TRACK_COMMIT_MS=15000" in resume_init_text, "resume init script uses 15s new-track commit guard", failures)
        require("AUDIOBOOK_RESUME_LOG_MAX_BYTES=524288" in resume_init_text, "resume init caps resume daemon log growth", failures)
        require("AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED=1" in resume_init_text, "resume init script enables UI seek screen guard", failures)
    else:
        require(False, "resume init script exists", failures)

    if require_db_maintenance:
        db_init = root / "etc/init.d/S92audiobook_db_maint.sh"
        if db_init.exists():
            db_init_text = db_init.read_text(encoding="ascii", errors="replace")
            require("\r" not in db_init_text, "db maint init script uses LF line endings", failures)
            require(
                'start-stop-daemon -S -b -m -p "$BASE/db-maint.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_db_watch.sh"'
                in db_init_text,
                "db maint init launches watcher through /bin/sh",
                failures,
            )
            require("AUDIOBOOK_DB_STABLE_SECONDS=15" in db_init_text, "db maint init waits for stable DB after scan", failures)
            require(
                "AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS=0" in db_init_text,
                "db maint init avoids periodic DB churn by default",
                failures,
            )
            require("AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES=524288" in db_init_text, "db maint init caps DB watcher log growth", failures)
            require("AUDIOBOOK_DB_RUN_ON_MTIME_ONLY=0" in db_init_text, "db maint init ignores mtime-only DB churn by default", failures)
            require(
                "AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS=900" in db_init_text,
                "db maint init keeps a long mtime-only rerun guard",
                failures,
            )
            require("r1_usrlocal_media_seed.db" in db_init_text, "db maint init installs media DB seed", failures)
        else:
            require(False, "db maint init script exists", failures)

        db_helper = root / "usr/bin/r1_audiobook_db_maint"
        require(db_helper.exists() and db_helper.stat().st_size > 500000, f"db maint helper present: {db_helper}", failures)

        db_seed = root / "usr/bin/r1_usrlocal_media_seed.db"
        require(db_seed.exists() and db_seed.stat().st_size > 100000, f"media DB seed present: {db_seed}", failures)

        db_watch = root / "usr/bin/r1_audiobook_db_watch.sh"
        if db_watch.exists():
            db_watch_text = db_watch.read_text(encoding="ascii", errors="replace")
            require("\r" not in db_watch_text, "db watch script uses LF line endings", failures)
            require("date -r \"$DB\" '+%s'" in db_watch_text, "db watch uses R1-supported date -r signature", failures)
            require('run_maint "$run_reason"' in db_watch_text, "db watch runs maintainer after stable size-changing scan", failures)
            require("skip reason=mtime-only" in db_watch_text, "db watch skips mtime-only playback churn", failures)
            require("run_maint boot" in db_watch_text, "db watch runs maintainer once after boot", failures)
            require("--music-dir \"$MUSIC_DIR\"" in db_watch_text, "db watch passes Music folder to helper", failures)
            require("--books-catalog \"$CATALOG_BOOKS\"" in db_watch_text, "db watch writes book-level catalog", failures)
            require("seeded-db reason=" in db_watch_text, "db watch seeds missing media DB", failures)
            require("LOG_MAX_BYTES=${AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES:-524288}" in db_watch_text, "db watch defaults to capped log growth", failures)
            require("rotate_log_if_needed" in db_watch_text, "db watch rotates logs when capped", failures)
        else:
            require(False, "db watch script exists", failures)

    daemon = root / "usr/bin/r1_audiobook_resume_daemon.sh"
    if daemon.exists():
        daemon_text = daemon.read_text(encoding="ascii", errors="replace")
        require("BOOK_TITLE_MARKER_ADDR=${AUDIOBOOK_BOOK_TITLE_MARKER_ADDR:-9322496}" in daemon_text, "daemon marker address is 0x8E4000", failures)
        require("NEW_TRACK_COMMIT_MS=${AUDIOBOOK_NEW_TRACK_COMMIT_MS:-15000}" in daemon_text, "daemon defaults to 15s new-track commit guard", failures)
        require("TRACK_RESTORE_KEY_FALLBACK_ENABLED=${AUDIOBOOK_TRACK_RESTORE_KEY_FALLBACK_ENABLED:-0}" in daemon_text, "daemon defaults hardware track fallback off", failures)
        require("player_pid_cached" in daemon_text, "daemon caches hiby_player pid between polls", failures)
        require("current_path_slot_preview" in daemon_text, "daemon has low-overhead path preview for idle music", failures)
        require("path_preview_is_audiobook" in daemon_text, "daemon avoids full path decode outside Audiobooks", failures)
        require("leave audiobook current=non-audiobook" in daemon_text, "daemon clears audiobook state after leaving to music", failures)
        require("COMPLETED_END_THRESHOLD_MS=${AUDIOBOOK_COMPLETED_END_THRESHOLD_MS:-45000}" in daemon_text, "daemon includes completed-book end threshold", failures)
        require("completion_state_for_path_position" in daemon_text, "daemon can mark final-track playback as completed", failures)
        require('"completed": %s' in daemon_text, "daemon persists completed flag in resume records", failures)
        require("completed book start-over" in daemon_text, "daemon starts completed books from the beginning", failures)
        require("RESTORE_REWIND_MS=${AUDIOBOOK_RESTORE_REWIND_MS:-0}" in daemon_text, "daemon exposes optional smart rewind", failures)
        require("restore_target_ms" in daemon_text, "daemon applies smart rewind to restore target", failures)
        require("TOUCH_FIRST_TRACK_EVENT_FILE" in daemon_text, "daemon includes first-track autostart touch", failures)
        require("pid_mem_contains_catalog_album" in daemon_text, "daemon includes catalog-title autostart guard", failures)
        require("BOOK_TITLE_CONTEXT_SECONDS" in daemon_text, "daemon includes context-aware title autostart guard", failures)
        require("late restore path=" in daemon_text, "daemon includes late same-track restore retry", failures)
        require("should_skip_failed_restore_save" in daemon_text, "daemon includes failed-restore bookmark save guard", failures)
        require("close_inherited_socket_fds" in daemon_text, "daemon closes inherited socket fds on startup", failures)
        require("LOG_MAX_BYTES=${AUDIOBOOK_RESUME_LOG_MAX_BYTES:-524288}" in daemon_text, "daemon defaults to capped log growth", failures)
        require("rotate_log_if_needed" in daemon_text, "daemon rotates logs when capped", failures)
        require("book_title_direct_track_select" in daemon_text, "daemon includes title-list direct track selection", failures)
        require("book_title_visible_track_select" in daemon_text, "daemon includes visible-row track selection", failures)
        require("book_title_direct_start_saved_track" in daemon_text, "daemon includes pre-play saved-track direct start", failures)
        require("pid_mem_first_catalog_path" in daemon_text, "daemon can identify selected track list from catalog paths", failures)
        require("MEMSCAN_HELPER=${AUDIOBOOK_MEMSCAN_HELPER:-$BASE_DIR/bin/r1_audiobook_memscan}" in daemon_text, "daemon defines memscan helper", failures)
        require("book_title_memscan_root" in daemon_text, "daemon can identify selected book root from player memory", failures)
        require("book-title direct-start memscan root=" in daemon_text, "daemon logs memscan direct-start roots", failures)
        require("BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED" in daemon_text, "daemon can calibrate retained track-list scroll state", failures)
        require("book_title_verify_selected_track" in daemon_text, "daemon verifies direct-start row taps", failures)
        require("BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS" in daemon_text, "daemon exposes title-start recovery transport limit", failures)
        require("book-title-direct-misselect" in daemon_text, "daemon recovers from direct-start misselects", failures)
        require("AUDIOBOOK_RESUME_DAEMON_SOURCE_ONLY" in daemon_text, "daemon includes source-only guard for local logic tests", failures)
        require("TOUCH_TRACK_SWIPE_MOVE6_EVENT_FILE" in daemon_text, "daemon includes timed track-list swipe packets", failures)
        require("RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS" in daemon_text, "daemon backs off repeated failed seek restores", failures)
        require("FAILED_RESTORE_SKIP_LOG_BUCKET_MS" in daemon_text, "daemon throttles failed-restore save guard logging", failures)
        require('reset_key="$book_title_autostart_seq"' in daemon_text, "daemon resets title autostart state once per title tap", failures)
        require("BOOK_TITLE_RESTORE_LOG_BUCKET_MS" in daemon_text, "daemon throttles title-start restore logging", failures)
        require("PLAYER_DURATION_ADDR=${AUDIOBOOK_PLAYER_DURATION_ADDR:-9115252}" in daemon_text, "daemon reads live track duration from player memory", failures)
        require("ui_seek_restore" in daemon_text, "daemon includes verified UI seek fallback", failures)
        require("AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED" in daemon_text, "daemon exposes UI seek fallback toggle", failures)
        require("UI_SEEK_TOUCH_FRAMES" in daemon_text, "daemon uses short generated tap streams for UI seek fallback", failures)
        require("ui_seek_screen_ready" in daemon_text, "daemon guards UI seek against non-Now Playing screens", failures)
        require("PLAY_MODE_TARGET=${AUDIOBOOK_PLAY_MODE_TARGET:-3}" in daemon_text, "daemon targets sequential audiobook playback", failures)
        require("PLAY_MODE_USER_INI_OFFSET=${AUDIOBOOK_PLAY_MODE_USER_INI_OFFSET:-592}" in daemon_text, "daemon reads persisted play mode byte", failures)
        require("ensure_audiobook_play_mode" in daemon_text, "daemon enforces audiobook play mode", failures)
        require("play-mode skipped screen-not-ready" in daemon_text, "daemon guards play mode taps against non-Now Playing screens", failures)
        require("TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED" in daemon_text, "daemon exposes near-miss transport fallback toggle", failures)
        require("track_restore_near_miss_transport" in daemon_text, "daemon includes near-miss transport fallback", failures)
        require("track-restore near-miss transport skipped mode=" in daemon_text, "daemon gates near-miss transport by play mode", failures)

    catalog = root / "usr/bin/r1_audiobook_catalog.tsv"
    require(catalog.exists() and catalog.stat().st_size > 0, f"seed catalog present: {catalog}", failures)

    memscan = root / "usr/bin/r1_audiobook_memscan"
    require(memscan.exists(), "memscan helper present", failures)
    if memscan.exists():
        require(os.access(memscan, os.X_OK), "memscan helper is executable", failures)

    ota_update = out_dir / "ota-tree/ota_v0/ota_update.in"
    rootfs_md5 = digest(rootfs, "md5") if rootfs.exists() else ""
    ota_rootfs_md5 = read_ini_value(ota_update, "img_md5")
    if ota_update.exists():
        values = [line.strip() for line in ota_update.read_text(encoding="ascii", errors="replace").splitlines()]
        rootfs_values = [values[i + 3] for i, value in enumerate(values) if value == "img_type=rootfs" and i + 3 < len(values)]
        ota_rootfs_md5 = rootfs_values[0].removeprefix("img_md5=") if rootfs_values else ota_rootfs_md5
    require(ota_rootfs_md5 == rootfs_md5, f"ota rootfs md5 matches rebuilt rootfs: {ota_rootfs_md5}", failures)

    print("\nHashes:")
    for relative, path in (
        (upt.name, upt),
        ("rootfs.squashfs", rootfs),
        ("squashfs-root/usr/bin/hiby_player", player),
    ):
        if not path.exists():
            continue
        md5 = digest(path, "md5")
        sha256 = digest(path, "sha256")
        print(f"{relative}\n  md5    {md5}\n  sha256 {sha256}")
        if path == upt:
            require(md5 not in KNOWN_BAD_MD5, f"{relative} md5 is not in known-bad list", failures)
        if expect_current_hashes and relative in EXPECTED_CURRENT_HASHES:
            expected = EXPECTED_CURRENT_HASHES[relative]
            require(md5 == expected["md5"], f"{relative} md5 matches documented current build", failures)
            require(sha256 == expected["sha256"], f"{relative} sha256 matches documented current build", failures)

    if failures:
        print("\nVerification failed:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("\nVerification passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=Path, default=Path("work/audiobook-firmware-1.6.11-logcap-candidate"))
    parser.add_argument("--upt-name", default="r1-audiobooks-1.6.11-audiobook.upt")
    parser.add_argument("--expected-version", default="1.6.11-audiobook")
    parser.add_argument("--expected-label", default="HiBy R1 Audiobook FW 1.6.11")
    parser.add_argument(
        "--stock-rootfs",
        type=Path,
        default=Path("work/original/rootfs.squashfs"),
        help="Reference stock rootfs used to compare preserved modes.",
    )
    parser.add_argument(
        "--unsquashfs",
        type=Path,
        default=Path(".deps/squashfs/tools/squashfs-tools/unsquashfs.exe"),
    )
    parser.add_argument(
        "--expect-current-hashes",
        action="store_true",
        help="Also require hashes to match the package currently documented in README.md.",
    )
    parser.add_argument(
        "--require-db-maintenance",
        action="store_true",
        help="Require the on-device audiobook DB maintenance helper and watcher.",
    )
    parser.add_argument(
        "--require-boot-adb",
        action="store_true",
        help="Require the optional development boot-ADB init script and marker.",
    )
    args = parser.parse_args()
    return verify(
        args.out_dir,
        expect_current_hashes=args.expect_current_hashes,
        expected_version=args.expected_version,
        expected_label=args.expected_label,
        require_db_maintenance=args.require_db_maintenance,
        require_boot_adb=args.require_boot_adb,
        stock_rootfs=args.stock_rootfs,
        unsquashfs=args.unsquashfs,
        upt_name=args.upt_name,
    )


if __name__ == "__main__":
    raise SystemExit(main())
