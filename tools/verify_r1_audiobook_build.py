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
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from patch_hiby_player import (
    AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE,
    AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL,
    AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH,
    AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE,
    AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL,
    AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH,
    AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_CODE,
    AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE,
    AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL,
    AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH,
    AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE,
    AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL,
    AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH,
)


EXPECTED_CURRENT_HASHES = {
    "r1-audiobooks-1.6.28-sd-ready-dev.upt": {
        "md5": "121e86097b433526e24d0f602d4310aa",
        "sha256": "f90e1fc62934ec1fe69b3c06169aa72881cc8f27ca8d9f6db6049a5c3ff34d06",
    },
    "r1-audiobooks-1.6.27-launcher-guard-dev.upt": {
        "md5": "8b4c776c1bb21b0f32e707fa3725ba35",
        "sha256": "a30f2d0277137dafe953e02fad9d2cf22266cd9a81566ba1963d3d502523bd46",
    },
    "r1-audiobooks-1.6.26-context-switch-dev.upt": {
        "md5": "dc0724a74cb3ed271ccdb5b9c40595c5",
        "sha256": "a076703ebbd82e07df0f96b4a578daa10d6b1e11a82e0a4ed610081d5c6e9a99",
    },
    "r1-audiobooks-1.6.25-zero-restore-dev.upt": {
        "md5": "d302b3ed5525b0600cfa5ca5c00a228c",
        "sha256": "cd73c114d30db7e7dd67c683d0f61ab44c4e9377830a288002d6d7c35bf78026",
    },
    "r1-audiobooks-1.6.24-exact-catalog-dev.upt": {
        "md5": "6a9cda191e4bb9772fa32a87185811cf",
        "sha256": "6374f07bc245eb68e9d04147ee5267260b94a191ffdafe603492424537af94de",
    },
    "r1-audiobooks-1.6.23-dbwatch-lock-dev.upt": {
        "md5": "c32159d55a5cbadc03c6ac3b8b779d16",
        "sha256": "4235a9addd653097899e55dcd1316074d7ab016229d84b250f0b2dae760ba561",
    },
    "r1-audiobooks-1.6.15-audiobook.upt": {
        "md5": "8f3ecb1f377493b84dbe80d947c89ecd",
        "sha256": "fa637ed2e4d6f21bf77014f6fc9bbcb9aed10aa6b3b58b8c52dd387465f639dc",
    },
    "r1-audiobooks-1.6.16-audiobook.upt": {
        "md5": "4938a5d3f74204995a1bb297175da463",
        "sha256": "ba3b16dc63e35abfc22cd0ac9e4324a5a2e3834ad894c42fd310f30f99c3f1e0",
    },
    "r1-audiobooks-1.6.16.1-audiobook.upt": {
        "md5": "d30527750a071602a67f1eceb462f8cc",
        "sha256": "085495646039eafb496279d3ef2625671783552ad069150c3e959e5c219d7f3f",
    },
    "r1-audiobooks-1.6.16.2-audiobook.upt": {
        "md5": "80c0d7295c2d55575870c4d226e83be9",
        "sha256": "3109fea179b816dcdd4c1536b8973f527ef8f8b2d628942317f6b4ded62ca4c6",
    },
    "r1-audiobooks-1.6.16.4-audiobook.upt": {
        "md5": "d6ebce37c653f3756b54a7b5c3725788",
        "sha256": "eefd1f060babf5930d7bae4be481d7f580edf225a128d17ab6130beced4dd404",
    },
    "r1-audiobooks-1.6.16.5-audiobook.upt": {
        "md5": "f6a0e65af41c7990f03e342fef995bad",
        "sha256": "efd77a5a6f83879e76089ace072657891ff2e5475c4f0e82d812f728ad4e2816",
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
        "md5": "1797f124a92177605e776615144f323a",
        "sha256": "cf2076de6c700abd24d66dc587ac3109786829e5f589f4068e61988b0a481325",
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

PLAYER_COMMON_BYTE_CHECKS = {
    "old title wrapper remains stock @0x0A1780": (0x0A1780, bytes.fromhex("5000a58c7800063c")),
}

PLAYER_TITLE_START_BYTE_CHECKS = {
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
    "marker cave prefix @0x35DE00": (
        0x35DE00,
        bytes.fromhex(
            "8e00083c00400835dec0093c174a2935000009ad04001fad"
            "080004ad0c0005ad100006ad140007ad180005ad28000a8d"
            "01004a2528000aadc8fdbd272c02b2af927f120800000000"
        ),
    ),
}

PLAYER_DIRECT_ENTRY_BYTE_CHECKS = {
    "audiobook launcher source marker @0x35DAEC": (
        0x35DAEC,
        bytes.fromhex(
            "a0fbbd275c04bfaf5804b1af5404b0af5000b08c18000012"
            "000000002800048ee0381c0c000000001300401000000000"
            "252000027600113ca8db31267800053ca070a5247600063c"
            "80dbc6248e00083c00400835b0a0093c15052935200009ad"
        ),
    ),
    "audiobook launcher callback @0x482030": (0x482030, bytes.fromhex("ecda7500")),
    "book launcher root hook @0x140F20": (0x140F20, bytes.fromhex("f0761d0800000000")),
}

PLAYER_DIRECT_PRIVATE_ROUTE_BYTE_CHECKS = {
    "audiobook launcher source marker @0x35DAEC": (
        0x35DAEC,
        bytes.fromhex(
            "a0fbbd275c04bfaf5804b1af5404b0af5000b08c18000012"
            "000000002800048ee0381c0c000000001300401000000000"
            "252000027600113ca8db31267600053c40dfa5247600063c"
            "80dbc6248e00083c00400835b0a0093c15052935200009ad"
        ),
    ),
    "audiobook private direct route record @0x35DF40": (
        0x35DF40,
        bytes.fromhex("c8ca7500986e780084f27700a4f2770000000000c0014f00"),
    ),
}

PLAYER_NATIVE_HUB_BYTE_CHECKS = {
    "native hub leaves Books hub opener stock @0x140F20": (
        0x140F20,
        bytes.fromhex("7800063c7800053c"),
    ),
    "native hub title row cave @0x35DE60": (
        0x35DE60,
        bytes.fromhex("25204002f0761d0c000000004b03150800000000"),
    ),
    "native hub title route string @0x35DB80": (
        0x35DB80,
        (
            "genre\\Audiobook".encode("utf-16le") + b"\x00\x00"
        ).ljust(
            len("Audiobook\\Audiobook".encode("utf-16le") + b"\x00\x00"),
            b"\x00",
        ),
    ),
    "native hub selected genre string @0x35DBA8": (
        0x35DBA8,
        ("Audiobook".encode("utf-16le") + b"\x00\x00").ljust(24, b"\x00"),
    ),
    "native hub title row jump-table entry @0x38D27C": (
        0x38D27C,
        bytes.fromhex("60de7500"),
    ),
}

PLAYER_NATIVE_HUB_STOCK_LAUNCHER_BYTE_CHECKS = {
    "native hub leaves launcher callback stock @0x482030": (0x482030, bytes.fromhex("20bb5300")),
}

PLAYER_NATIVE_HUB_LAUNCHER_BYTE_CHECKS = {
    "native hub launcher callback cave @0x35DAEC": (
        0x35DAEC,
        bytes.fromhex(
            "d8ffbd272400bfaf2000b1af1c00b0af5000b08ce0381c0c"
            "2800048e07004014010003242400bf8f2000b18f1c00b08f"
            "251060000800e0032800bd277800113c2cc12526d0c9100c"
            "25200002030040102cc1252678cf100c25200002c803150c"
            "252000022400bf8f010003242000b18f1c00b08f25106000"
            "0800e0032800bd27"
        ),
    ),
    "native hub launcher callback @0x482030": (0x482030, bytes.fromhex("ecda7500")),
    "native hub title private resource key @0x35DF40": (0x35DF40, b"titles\x00"),
    "native hub title private label lui @0x1407A0": (
        0x1407A0,
        bytes.fromhex("7600023c"),
    ),
    "native hub title private label addiu @0x1407A8": (
        0x1407A8,
        bytes.fromhex("40df4224"),
    ),
}

PLAYER_NATIVE_HUB_FOLDER_ROW_BYTE_CHECKS = {
    "native hub folder rows cave @0x35DEA0": (
        0x35DEA0,
        bytes.fromhex("d800048e7600053c34c1a5247600063c00dfc6247858120c000000004b03150800000000"),
    ),
    "native hub folder rows path @0x35DF00": (
        0x35DF00,
        (
            "a:\\Audiobooks\\*".encode("utf-16le") + b"\x00\x00"
        ).ljust(0x40, b"\x00"),
    ),
    "native hub Authors row jump-table entry @0x38D280": (
        0x38D280,
        bytes.fromhex("a0de7500"),
    ),
    "native hub Folders row jump-table entry @0x38D284": (
        0x38D284,
        bytes.fromhex("a0de7500"),
    ),
}

PLAYER_NATIVE_HUB_VIEW_ROW_BYTE_CHECKS = {
    "native hub view Titles cave @0x360708": (
        0x360708,
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_CODE,
    ),
    "native hub view Authors cave @0x360738": (
        0x360738,
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_CODE,
    ),
    "native hub view Series cave @0x360768": (
        0x360768,
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_CODE,
    ),
    "native hub view Folders cave @0x360798": (
        0x360798,
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_CODE,
    ),
    "native hub view explorer cleanup helper @0x360D50": (
        0x360D50,
        AUDIOBOOK_NATIVE_HUB_VIEW_OPEN_HELPER_CODE,
    ),
    "native hub view Titles label @0x3607C8": (
        0x3607C8,
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_LABEL,
    ),
    "native hub view Authors label @0x3607D8": (
        0x3607D8,
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_LABEL,
    ),
    "native hub view Series label @0x3607E8": (
        0x3607E8,
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_LABEL,
    ),
    "native hub view Folders label @0x3607F8": (
        0x3607F8,
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_LABEL,
    ),
    "native hub view Titles path @0x360808": (
        0x360808,
        AUDIOBOOK_NATIVE_HUB_VIEW_TITLE_PATH,
    ),
    "native hub view Authors path @0x360888": (
        0x360888,
        AUDIOBOOK_NATIVE_HUB_VIEW_AUTHOR_PATH,
    ),
    "native hub view Series path @0x360908": (
        0x360908,
        AUDIOBOOK_NATIVE_HUB_VIEW_SERIES_PATH,
    ),
    "native hub view Folders path @0x360988": (
        0x360988,
        AUDIOBOOK_NATIVE_HUB_VIEW_FOLDER_PATH,
    ),
    "native hub Titles row jump-table entry @0x38D27C": (
        0x38D27C,
        bytes.fromhex("08077600"),
    ),
    "native hub Authors row jump-table entry @0x38D280": (
        0x38D280,
        bytes.fromhex("38077600"),
    ),
    "native hub Series row jump-table entry @0x38D284": (
        0x38D284,
        bytes.fromhex("68077600"),
    ),
    "native hub Folders row jump-table entry @0x38D288": (
        0x38D288,
        bytes.fromhex("98077600"),
    ),
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
    "squashfs-root/usr/bin/r1_audiobook_direct_open": "-rwxr-xr-x",
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
}

SEED_CATALOG_MODE_CHECKS = {
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


def png_header(path: Path) -> tuple[int, int, int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError(f"not a PNG with IHDR header: {path}")
    width, height = struct.unpack(">II", data[16:24])
    bit_depth = data[24]
    color_type = data[25]
    return width, height, bit_depth, color_type


def check_audiobook_launcher_icons(root: Path, failures: list[str]) -> None:
    expected = {
        "usr/resource/litegui/midi/theme1/launcher/book.png": (224, 242),
        "usr/resource/litegui/midi/theme1/launcher/book_s.png": (224, 242),
        "usr/resource/litegui/theme1/launcher/book.png": (140, 140),
        "usr/resource/litegui/theme1/launcher/book_s.png": (140, 140),
        "usr/resource/litegui/theme2/launcher/book.png": (140, 140),
        "usr/resource/litegui/theme2/launcher/book_s.png": (140, 140),
    }
    for relative, (width, height) in expected.items():
        path = root / relative
        if not path.exists():
            failures.append(f"audiobook launcher icon missing: {relative}")
            continue
        try:
            actual_width, actual_height, bit_depth, color_type = png_header(path)
        except Exception as exc:
            failures.append(f"audiobook launcher icon parse failed for {relative}: {exc}")
            continue
        require(
            (actual_width, actual_height) == (width, height),
            f"{relative} dimensions {actual_width}x{actual_height}",
            failures,
        )
        require(bit_depth == 8 and color_type == 6, f"{relative} is 8-bit RGBA PNG", failures)


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


def config_function_value(config_data: object, key: str) -> object | None:
    if not isinstance(config_data, list):
        return None
    for section in config_data:
        if not isinstance(section, dict) or section.get("type") != "function":
            continue
        fcn0 = section.get("fcn0")
        if not isinstance(fcn0, list):
            continue
        for item in fcn0:
            if isinstance(item, dict) and key in item:
                return item[key]
    return None


def settings_function_value(settings_data: object, key: str) -> object | None:
    if not isinstance(settings_data, list):
        return None
    for section in settings_data:
        if not isinstance(section, dict):
            continue
        funs = section.get("funs")
        if not isinstance(funs, list):
            continue
        for item in funs:
            if isinstance(item, dict) and key in item:
                return item[key]
    return None


def verify(
    out_dir: Path,
    *,
    expect_current_hashes: bool,
    expected_version: str,
    expected_label: str,
    require_db_maintenance: bool,
    require_boot_adb: bool,
    expect_batd_disabled: bool,
    expect_audiobook_launcher_icon: bool,
    expect_native_dsd: bool,
    expect_sbc_xq: bool,
    expect_usb_dac_mode: bool,
    expect_seed_catalog: bool,
    expect_native_hub_title_row: bool,
    expect_native_hub_launcher: bool,
    expect_native_hub_folder_rows: bool,
    expect_native_hub_view_rows: bool,
    expect_private_direct_route: bool,
    expect_conservative_resume_runtime: bool,
    expected_ota_version: int,
    expected_ota_site: str | None,
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
        byte_checks = dict(PLAYER_COMMON_BYTE_CHECKS)
        if expect_native_hub_title_row:
            byte_checks.update(PLAYER_TITLE_START_BYTE_CHECKS)
            byte_checks.update(PLAYER_NATIVE_HUB_BYTE_CHECKS)
            if expect_native_hub_launcher:
                byte_checks.update(PLAYER_NATIVE_HUB_LAUNCHER_BYTE_CHECKS)
            else:
                byte_checks.update(PLAYER_NATIVE_HUB_STOCK_LAUNCHER_BYTE_CHECKS)
            if expect_native_hub_folder_rows:
                byte_checks.update(PLAYER_NATIVE_HUB_FOLDER_ROW_BYTE_CHECKS)
        elif expect_native_hub_view_rows:
            byte_checks.update(PLAYER_NATIVE_HUB_LAUNCHER_BYTE_CHECKS)
            byte_checks.update(PLAYER_NATIVE_HUB_VIEW_ROW_BYTE_CHECKS)
            if not expect_native_hub_launcher:
                failures.append("--expect-native-hub-view-rows requires --expect-native-hub-launcher")
        else:
            byte_checks.update(PLAYER_TITLE_START_BYTE_CHECKS)
            byte_checks.update(PLAYER_DIRECT_ENTRY_BYTE_CHECKS)
            if expect_private_direct_route:
                byte_checks.update(PLAYER_DIRECT_PRIVATE_ROUTE_BYTE_CHECKS)
            if expect_native_hub_launcher:
                failures.append("--expect-native-hub-launcher requires --expect-native-hub-title-row or --expect-native-hub-view-rows")
            if expect_native_hub_folder_rows:
                failures.append("--expect-native-hub-folder-rows requires --expect-native-hub-title-row")
            if expect_native_hub_view_rows:
                failures.append("--expect-native-hub-view-rows requires --expect-native-hub-launcher")
        for label, (offset, expected) in byte_checks.items():
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
    if expect_seed_catalog:
        for relative, expected_mode in SEED_CATALOG_MODE_CHECKS.items():
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
        marker_ota_version = read_ini_value(version_marker, "ota_version")
        if marker_ota_version is not None or expected_ota_version != 0:
            require(
                marker_ota_version == str(expected_ota_version),
                f"custom version marker records ota_version={expected_ota_version}",
                failures,
            )
        marker_ota_site = read_ini_value(version_marker, "ota_site")
        if expected_ota_site is not None:
            require(
                marker_ota_site == expected_ota_site,
                f"custom version marker records ota_site={expected_ota_site}",
                failures,
            )
        if read_ini_value(version_marker, "audiobook_entry") is not None:
            print("OK   custom version marker records audiobook entry mode")
        if expect_private_direct_route:
            require("audiobook_entry=direct-private-route" in version_text, "custom version marker records private direct route mode", failures)
        if require_boot_adb:
            require("boot_adb=enabled" in version_text, "custom version marker records boot ADB enabled", failures)
        elif "boot_adb=" in version_text:
            print("OK   custom version marker records boot ADB state")
        if expect_batd_disabled:
            require("batd_logger=disabled" in version_text, "custom version marker records batd logger disabled", failures)
        elif "batd_logger=" in version_text:
            print("OK   custom version marker records batd logger state")
        if expect_audiobook_launcher_icon:
            require("launcher_icon=audiobook" in version_text, "custom version marker records audiobook launcher icon", failures)
        elif "launcher_icon=" in version_text:
            print("OK   custom version marker records launcher icon state")
        if expect_native_dsd:
            require("native_dsd=enabled" in version_text, "custom version marker records native DSD enabled", failures)
        elif "native_dsd=" in version_text:
            print("OK   custom version marker records native DSD state")
        if expect_sbc_xq:
            require("bluetooth_sbc_xq=enabled" in version_text, "custom version marker records Bluetooth SBC XQ enabled", failures)
        elif "bluetooth_sbc_xq=" in version_text:
            print("OK   custom version marker records Bluetooth SBC XQ state")
        if expect_usb_dac_mode:
            require("usb_dac_mode=enabled" in version_text, "custom version marker records USB DAC mode enabled", failures)
        elif "usb_dac_mode=" in version_text:
            print("OK   custom version marker records USB DAC mode state")
        if expect_native_hub_launcher:
            require("native_hub_launcher=enabled" in version_text, "custom version marker records native hub launcher enabled", failures)
        elif "native_hub_launcher=" in version_text:
            print("OK   custom version marker records native hub launcher state")
        if expect_native_hub_folder_rows:
            require("native_hub_folder_rows=enabled" in version_text, "custom version marker records native hub folder rows enabled", failures)
        elif "native_hub_folder_rows=" in version_text:
            print("OK   custom version marker records native hub folder rows state")
        if expect_native_hub_view_rows:
            require("native_hub_view_rows=enabled" in version_text, "custom version marker records native hub view rows enabled", failures)
            require("audiobook_entry=native-hub-view-rows" in version_text, "custom version marker records native hub view-row entry", failures)
        elif "native_hub_view_rows=" in version_text:
            print("OK   custom version marker records native hub view rows state")
        expected_resume_profile = "conservative" if expect_conservative_resume_runtime else "direct"
        require(
            read_ini_value(version_marker, "resume_runtime_profile") == expected_resume_profile,
            f"custom version marker records {expected_resume_profile} resume runtime profile",
            failures,
        )
    else:
        require(False, "custom version marker exists", failures)

    if expect_audiobook_launcher_icon:
        check_audiobook_launcher_icons(root, failures)

    player_launch = root / "usr/bin/hiby_player.sh"
    if player_launch.exists():
        launch_text = player_launch.read_text(encoding="ascii", errors="replace")
        require("\r" not in launch_text, "hiby_player.sh uses LF line endings", failures)
        if expect_batd_disabled:
            require("batd -v -s -t5 -o /mnt/sd_0/batlog.txt" not in launch_text, "hiby_player.sh does not start batd SD logger", failures)
    elif expect_batd_disabled:
        require(False, "hiby_player.sh exists for batd logger check", failures)

    about_dev = root / "usr/resource/str/english/about_dev.ini"
    if about_dev.exists():
        about_text = about_dev.read_text(encoding="utf-16", errors="replace")
        require(f"<model>{expected_label}</model>" in about_text, "About screen model shows custom firmware label", failures)
    else:
        require(False, "English About resource exists", failures)

    book_ini = root / "usr/resource/str/english/book.ini"
    if book_ini.exists():
        book_text = book_ini.read_text(encoding="utf-16", errors="replace")
        if expect_native_hub_title_row or expect_native_hub_view_rows:
            require("<titles>Titles</titles>" in book_text, "Book resource includes native hub Titles row key", failures)
            require("<ebook>Audiobooks</ebook>" in book_text, "Book resource keeps Audiobooks page title", failures)
            require("<collect>Authors</collect>" in book_text, "Book resource includes native hub Authors row label", failures)
            if expect_native_hub_view_rows:
                require("<explorer>Series</explorer>" in book_text, "Book resource includes native hub Series row label", failures)
                require("<recent>Folders</recent>" in book_text, "Book resource includes native hub Folders row label", failures)
            else:
                require("<explorer>Folders</explorer>" in book_text, "Book resource includes native hub Folders row label", failures)
    else:
        require(False, "English Book resource exists", failures)

    config_json = root / "usr/resource/config.json"
    if config_json.exists():
        try:
            config_data = json.loads(config_json.read_text(encoding="utf-8"))
            product = next((entry for entry in config_data if isinstance(entry, dict) and entry.get("type") == "product"), {})
            require(product.get("version") == expected_version, "resource config product version shows custom firmware", failures)
            require(product.get("device") == "R1", "resource config keeps R1 product device", failures)
            if expect_usb_dac_mode:
                require(config_function_value(config_data, "dac_to_store") == 1, "resource config enables DAC-to-storage transition handling", failures)
        except Exception as exc:
            print(f"FAIL resource config parses: {exc}")
            failures.append("resource config parses")
    else:
        require(False, "resource config exists", failures)

    ota_info = root / "etc/ota_info"
    if ota_info.exists():
        ota_info_text = ota_info.read_text(encoding="ascii", errors="replace")
        require("\r" not in ota_info_text, "rootfs ota_info uses LF line endings", failures)
        require(
            read_ini_value(ota_info, "ota_version") == str(expected_ota_version),
            f"rootfs ota_info records ota_version={expected_ota_version}",
            failures,
        )
        if expected_ota_site is not None:
            require(
                read_ini_value(ota_info, "ota_site") == expected_ota_site,
                f"rootfs ota_info records ota_site={expected_ota_site}",
                failures,
            )
    else:
        require(False, "rootfs ota_info exists", failures)

    if expect_native_dsd:
        ot_devices = root / "usr/resource/ot_devices.json"
        if ot_devices.exists():
            try:
                ot_data = json.loads(ot_devices.read_text(encoding="utf-8"))
                analog = next((entry for entry in ot_data.get("DEVICES", []) if isinstance(entry, dict) and entry.get("Name") == "analog"), {})
                require(analog.get("AnalogDsdNative") == "native", "ot_devices enables native DSD for analog output", failures)
                require(analog.get("AnalogDsdD2p") == "dop", "ot_devices leaves DSD D2P mode as DoP", failures)
                require(analog.get("AnalogDsdDop") == "dop", "ot_devices leaves DSD DoP mode as DoP", failures)
            except Exception as exc:
                print(f"FAIL ot_devices parses: {exc}")
                failures.append("ot_devices parses")
        else:
            require(False, "ot_devices.json exists for native DSD check", failures)

    if expect_sbc_xq:
        bt_init = root / "usr/bin/bt_init"
        if bt_init.exists():
            bt_text = bt_init.read_text(encoding="utf-8", errors="replace")
            require("\r" not in bt_text, "bt_init uses LF line endings", failures)
            require("/usr/bin/bluealsa -p a2dp-source --a2dp-volume --sbc-quality=xq &" in bt_text, "bt_init enables Bluetooth SBC XQ", failures)
        else:
            require(False, "bt_init exists for SBC XQ check", failures)

    if expect_usb_dac_mode:
        for rel in ("usr/resource/set_functions.json", "usr/resource/midi_set_functions.json"):
            settings_path = root / rel
            if settings_path.exists():
                try:
                    settings_data = json.loads(settings_path.read_text(encoding="utf-8"))
                    for flag in ("usb_mode", "dac_feedback", "car_mode", "standby", "about"):
                        require(settings_function_value(settings_data, flag) == 1, f"{rel} enables {flag}", failures)
                    require(settings_function_value(settings_data, "car_mode_auto_play") == 0, f"{rel} leaves car_mode_auto_play disabled", failures)
                except Exception as exc:
                    print(f"FAIL {rel} parses: {exc}")
                    failures.append(f"{rel} parses")
            else:
                require(False, f"{rel} exists for USB DAC mode check", failures)

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
        require("AUDIOBOOK_INTERVAL_SECONDS=2" in resume_init_text, "resume init script uses lower-load 2s polling", failures)
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
        require("AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS=4" in resume_init_text, "resume init waits briefly for launcher track lists", failures)
        require('old_pid=$(cat "$BASE/resume-daemon.pid"' in resume_init_text, "resume init stops stale resume daemon pid before restart", failures)
        require('kill -9 "$old_pid"' in resume_init_text, "resume init force-cleans stale resume daemon pid", failures)
        expected_track_restore = "0" if expect_conservative_resume_runtime else "1"
        expected_autostart = "0" if expect_conservative_resume_runtime else "1"
        expected_direct_features = "0" if expect_conservative_resume_runtime else "1"
        expected_ui_seek = "0" if expect_conservative_resume_runtime else "1"
        require(
            f"AUDIOBOOK_TRACK_RESTORE_ENABLED={expected_track_restore}" in resume_init_text,
            f"resume init sets track restore to {expected_track_restore}",
            failures,
        )
        require(
            f"AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED={expected_autostart}" in resume_init_text,
            f"resume init sets book title autostart to {expected_autostart}",
            failures,
        )
        require(
            f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED={expected_direct_features}" in resume_init_text,
            f"resume init sets pre-play direct track selection to {expected_direct_features}",
            failures,
        )
        require(
            f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED={expected_direct_features}" in resume_init_text,
            f"resume init sets direct track selection to {expected_direct_features}",
            failures,
        )
        require(
            f"AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED={expected_direct_features}" in resume_init_text,
            f"resume init sets title memscan to {expected_direct_features}",
            failures,
        )
        require(
            f"AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED={expected_direct_features}" in resume_init_text,
            f"resume init sets direct track calibration to {expected_direct_features}",
            failures,
        )
        expected_first_track_entry = "0" if expect_conservative_resume_runtime else ("1" if expect_native_hub_view_rows else "0")
        require(
            f"AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED={expected_first_track_entry}" in resume_init_text,
            "resume init sets expected first-track entry restore mode",
            failures,
        )
        require("AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS=15000" in resume_init_text, "resume init bounds first-track entry restore window", failures)
        require('cp -f /usr/bin/r1_audiobook_direct_open "$BASE/bin/r1_audiobook_direct_open"' in resume_init_text, "resume init installs direct-open helper", failures)
        require(
            f"AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED={expected_direct_features}" in resume_init_text,
            f"resume init sets one-shot direct-open helper to {expected_direct_features}",
            failures,
        )
        require("AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR=0x760708" in resume_init_text, "resume init uses audited direct-open probe cave", failures)
        require("AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US=200000" in resume_init_text, "resume init uses short direct-open arm delay", failures)
        require("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4" in resume_init_text, "resume init sets direct track list geometry", failures)
        require("AUDIOBOOK_NEW_TRACK_COMMIT_MS=15000" in resume_init_text, "resume init script uses 15s new-track commit guard", failures)
        require("AUDIOBOOK_SAVE_BUCKET_MS=15000" in resume_init_text, "resume init script uses 15s steady-state save cadence", failures)
        require("AUDIOBOOK_RESUME_LOG_MAX_BYTES=524288" in resume_init_text, "resume init caps resume daemon log growth", failures)
        require(
            f"AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED={expected_ui_seek}" in resume_init_text,
            f"resume init sets UI seek fallback to {expected_ui_seek}",
            failures,
        )
        require("AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED=1" in resume_init_text, "resume init script enables UI seek screen guard", failures)
        expected_back_guard = "0" if (expect_conservative_resume_runtime or expect_native_hub_launcher) else "1"
        require(
            f"AUDIOBOOK_BACK_GUARD_ENABLED={expected_back_guard}" in resume_init_text,
            "resume init sets the expected Audiobooks back-stack guard mode",
            failures,
        )
        require("AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS=60" in resume_init_text, "resume init bounds Audiobooks back-stack guard polling", failures)
        require("AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS=1" in resume_init_text, "resume init samples idle Audiobooks screen quickly", failures)
        require("AUDIOBOOK_BACK_GUARD_EXTRA_BACKS=2" in resume_init_text, "resume init sends two guarded backs to return to launcher", failures)
    else:
        require(False, "resume init script exists", failures)

    if require_db_maintenance:
        db_init = root / "etc/init.d/S92audiobook_db_maint.sh"
        db_init_text = ""
        if db_init.exists():
            db_init_text = db_init.read_text(encoding="ascii", errors="replace")
            require("\r" not in db_init_text, "db maint init script uses LF line endings", failures)
            require(
                'start-stop-daemon -S -b -m -p "$BASE/db-maint.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_db_watch.sh"'
                in db_init_text,
                "db maint init launches watcher through /bin/sh",
                failures,
            )
            require("db_watch_pid_is_live()" in db_init_text, "db maint init can identify a live watcher before cleanup", failures)
            require("stop_db_watch()" in db_init_text, "db maint init uses a guarded stop/restart helper", failures)
            require('kill -9 "$old_pid"' in db_init_text, "db maint init force-clears a stuck old watcher", failures)
            require('rm -rf "$BASE/db-maint.lock"' in db_init_text, "db maint init clears stale DB watcher lock", failures)
            require("AUDIOBOOK_DB_STABLE_SECONDS=15" in db_init_text, "db maint init waits for stable DB after scan", failures)
            require(
                "AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS=0" in db_init_text,
                "db maint init avoids periodic DB churn by default",
                failures,
            )
            require(
                "AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=180" in db_init_text,
                "db maint init waits through boot scan DB churn",
                failures,
            )
            require("AUDIOBOOK_DB_STABLE_POLL_SECONDS=3" in db_init_text, "db maint init uses short stable-DB polling", failures)
            require("AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES=524288" in db_init_text, "db maint init caps DB watcher log growth", failures)
            require("AUDIOBOOK_DB_RUN_ON_MTIME_ONLY=0" in db_init_text, "db maint init ignores mtime-only DB churn by default", failures)
            require(
                "AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS=0" in db_init_text,
                "db maint init disables mtime-only reruns by default",
                failures,
            )
            require(
                "AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=600" in db_init_text,
                "db maint init waits for late Audiobooks mount after empty boot pass",
                failures,
            )
            require(
                "AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS=5" in db_init_text,
                "db maint init polls late Audiobooks mount at low rate",
                failures,
            )
            require(
                "AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS=600" in db_init_text,
                "db maint init retries while stock media DB scan holds SQLite locks",
                failures,
            )
            require(
                "AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS=5" in db_init_text,
                "db maint init polls locked DB retries at low rate",
                failures,
            )
            require("r1_usrlocal_media_seed.db" in db_init_text, "db maint init installs media DB seed", failures)
        else:
            require(False, "db maint init script exists", failures)

        db_helper = root / "usr/bin/r1_audiobook_db_maint"
        require(db_helper.exists() and db_helper.stat().st_size > 500000, f"db maint helper present: {db_helper}", failures)
        if db_helper.exists():
            db_helper_bytes = db_helper.read_bytes()
            require(b"--titles-catalog" in db_helper_bytes, "db maint helper supports title-view catalog flag", failures)
            require(b"--authors-catalog" in db_helper_bytes, "db maint helper supports author-view catalog flag", failures)
            require(b"--series-catalog" in db_helper_bytes, "db maint helper supports series-view catalog flag", failures)
            require(b"--view-root" in db_helper_bytes, "db maint helper supports generated view-root flag", failures)
            require(b"--needs-maintenance" in db_helper_bytes, "db maint helper supports cheap repair-needed check", failures)
            require(
                b"audiobook files missing from DB" in db_helper_bytes,
                "db maint helper checks filesystem audiobook paths during repair-needed checks",
                failures,
            )
            require(
                b"audiobook route genre missing" in db_helper_bytes,
                "db maint helper checks the route-visible Audiobook genre row",
                failures,
            )
            require(
                b"insert audiobook route genre" in db_helper_bytes,
                "db maint helper repairs the route-visible Audiobook genre row",
                failures,
            )

        db_seed = root / "usr/bin/r1_usrlocal_media_seed.db"
        require(db_seed.exists() and db_seed.stat().st_size > 100000, f"media DB seed present: {db_seed}", failures)

        db_watch = root / "usr/bin/r1_audiobook_db_watch.sh"
        if db_watch.exists():
            db_watch_text = db_watch.read_text(encoding="ascii", errors="replace")
            require("\r" not in db_watch_text, "db watch script uses LF line endings", failures)
            require("date -r \"$sig_path\" '+%s'" in db_watch_text, "db watch uses R1-supported date -r signature", failures)
            require(
                'run_maint_with_retries "$run_reason"' in db_watch_text,
                "db watch runs maintainer with transient retry handling after stable size-changing scan",
                failures,
            )
            require("skip reason=mtime-only" in db_watch_text, "db watch skips mtime-only playback churn", failures)
            require("wait_for_stable_db boot" in db_watch_text, "db watch waits for boot DB stability before first maint run", failures)
            require("wait-stable-timeout reason=" in db_watch_text, "db watch logs boot DB stability timeout", failures)
            require("stable reason=" in db_watch_text, "db watch logs stable DB signature", failures)
            require("AUDIOBOOK_DB_MIRROR_PATHS=" in db_watch_text, "db watch tracks active media DB mirror paths", failures)
            require("/data/usrlocal_media.db" in db_watch_text, "db watch includes /data media DB mirror", failures)
            require("$SD_ROOT/usrlocal_media.db" in db_watch_text, "db watch includes SD-root media DB mirror", failures)
            require("run_maint_one_db" in db_watch_text, "db watch can run helper per DB path", failures)
            require('run_maint_one_db "$reason" "$mirror_db" mirror' in db_watch_text, "db watch runs helper for mirror DB paths", failures)
            require("copy_primary_to_mirror" in db_watch_text, "db watch can copy repaired primary DB to mirrors", failures)
            require("mirror-copy reason=" in db_watch_text, "db watch logs mirror DB copy repairs", failures)
            require("copy_db_to_primary" in db_watch_text, "db watch can promote a scanned SD DB to primary", failures)
            require("primary-copy reason=" in db_watch_text, "db watch logs SD DB promotion", failures)
            require("promote_clean_sd_db" in db_watch_text, "db watch can prefer a clean SD-root DB after card changes", failures)
            require("mirror_db_signature" in db_watch_text, "db watch tracks mirror DB signature changes", failures)
            require("mirror-db-change" in db_watch_text, "db watch logs mirror DB changes", failures)
            require("mirror-stable" in db_watch_text, "db watch repairs stable mirror DB changes", failures)
            require("promoted-sd" in db_watch_text, "db watch runs maintainer after SD DB promotion", failures)
            require("any_db_needs_maintenance" in db_watch_text, "db watch can detect same-size stock DB rewrites", failures)
            require("--needs-maintenance" in db_watch_text, "db watch uses cheap DB repair-needed check", failures)
            require("content-repair-mtime" in db_watch_text, "db watch repairs same-size content rewrites", failures)
            require("boot_stable_timeout=" in db_watch_text, "db watch start log includes boot stability timeout", failures)
            require("zero_audio_retry=" in db_watch_text, "db watch start log includes zero-audiobook retry timeout", failures)
            require("locked_db_retry=" in db_watch_text, "db watch start log includes locked-DB retry timeout", failures)
            require("run_maint_with_retries boot" in db_watch_text, "db watch runs maintainer once after boot with retry handling", failures)
            require("retry_zero_audiobooks_if_needed" in db_watch_text, "db watch retries empty catalogs after late Audiobooks mount", failures)
            require("zero-audiobook-retry-ready" in db_watch_text, "db watch logs late Audiobooks readiness", failures)
            require("defer-zero-audiobooks" in db_watch_text, "db watch defers zero-row audiobook repairs while audio files are present", failures)
            require("mirror-skip reason=$reason db=$mirror_db primary-transient" in db_watch_text, "db watch avoids mirroring transient empty/locked primary DB results", failures)
            require("LOCKED_DB_RETRY_TIMEOUT_SECONDS=${AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS:-600}" in db_watch_text, "db watch defaults locked-DB retry timeout", failures)
            require("failed-locked reason=" in db_watch_text, "db watch detects locked SQLite helper failures", failures)
            require("locked-db-retry-start" in db_watch_text, "db watch retries after locked SQLite helper failures", failures)
            require("audiobook_tracks=${LAST_AUDIOBOOK_TRACKS:-unknown}" in db_watch_text, "db watch logs helper audiobook row counts", failures)
            require("--music-dir \"$MUSIC_DIR\"" in db_watch_text, "db watch passes Music folder to helper", failures)
            require("--books-catalog \"$CATALOG_BOOKS\"" in db_watch_text, "db watch writes book-level catalog", failures)
            require("--titles-catalog \"$CATALOG_TITLES\"" in db_watch_text, "db watch writes title-view catalog", failures)
            require("--authors-catalog \"$CATALOG_AUTHORS\"" in db_watch_text, "db watch writes author-view catalog", failures)
            require("--series-catalog \"$CATALOG_SERIES\"" in db_watch_text, "db watch writes series-view catalog", failures)
            expected_view_generation = "1" if expect_native_hub_view_rows else "0"
            require(
                f"AUDIOBOOK_VIEW_GENERATION_ENABLED={expected_view_generation}" in db_init_text,
                f"db maint init sets view generation to {expected_view_generation}",
                failures,
            )
            require("VIEW_GENERATION_ENABLED=${AUDIOBOOK_VIEW_GENERATION_ENABLED:-0}" in db_watch_text, "db watch defaults generated view folders off", failures)
            if expect_native_hub_view_rows:
                require("--view-root $VIEW_ROOT" in db_watch_text, "db watch can write generated audiobook view folders", failures)
            else:
                require("--view-root \"$VIEW_ROOT\"" not in db_watch_text, "db watch does not force generated view folders in stable builds", failures)
            require("seeded-db reason=" in db_watch_text, "db watch seeds missing media DB", failures)
            require("LOG_MAX_BYTES=${AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES:-524288}" in db_watch_text, "db watch defaults to capped log growth", failures)
            require("rotate_log_if_needed" in db_watch_text, "db watch rotates logs when capped", failures)
            require("LOCK_DIR=${AUDIOBOOK_DB_MAINT_LOCK:-$BASE/db-maint.lock}" in db_watch_text, "db watch has a duplicate-process lock", failures)
            require("pid_is_db_watcher" in db_watch_text, "db watch validates stale lock PID identity", failures)
            require("stale-lock-live-pid-not-watcher" in db_watch_text, "db watch recovers live non-watcher stale locks", failures)
            require("trap 'cleanup; exit 0' HUP INT TERM" in db_watch_text, "db watch exits cleanly on service stop", failures)
            require("exit reason=already-running" in db_watch_text, "db watch exits when another watcher is active", failures)
            require('compare_last_size=$(signature_size "$last_sig")' in db_watch_text, "db watch compares signature sizes for mtime-only churn", failures)
        else:
            require(False, "db watch script exists", failures)

    daemon = root / "usr/bin/r1_audiobook_resume_daemon.sh"
    if daemon.exists():
        daemon_text = daemon.read_text(encoding="ascii", errors="replace")
        require("BOOK_TITLE_MARKER_ADDR=${AUDIOBOOK_BOOK_TITLE_MARKER_ADDR:-9322496}" in daemon_text, "daemon marker address is 0x8E4000", failures)
        require("NEW_TRACK_COMMIT_MS=${AUDIOBOOK_NEW_TRACK_COMMIT_MS:-15000}" in daemon_text, "daemon defaults to 15s new-track commit guard", failures)
        require("SAVE_BUCKET_MS=${AUDIOBOOK_SAVE_BUCKET_MS:-15000}" in daemon_text, "daemon defaults to 15s steady-state save cadence", failures)
        require("bucket=$((pos / SAVE_BUCKET_MS))" in daemon_text, "daemon uses configurable steady-state save cadence", failures)
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
        require("DIRECT_OPEN_HELPER=${AUDIOBOOK_DIRECT_OPEN_HELPER:-$BASE_DIR/bin/r1_audiobook_direct_open}" in daemon_text, "daemon defines direct-open helper", failures)
        require("book_title_direct_open_row_override" in daemon_text, "daemon includes one-shot direct-open row override", failures)
        require("book-title direct-open-start" in daemon_text, "daemon uses direct-open during title-list pre-play start", failures)
        require("DIRECT_OPEN_ARM_DELAY_US=${AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US:-200000}" in daemon_text, "daemon defaults direct-open arm delay to 200 ms", failures)
        require("AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR" in daemon_text, "daemon exposes direct-open probe address", failures)
        require("allow_memscan_root" in daemon_text, "daemon can disable stale memscan root for context-only title starts", failures)
        require("book_title_should_preplay_direct_start" in daemon_text, "daemon has pre-play direct-start match guard", failures)
        require("book_title_wait_for_launcher_track_list" in daemon_text, "daemon retains launcher track-list detector for guarded title-start handling", failures)
        require("book_title_preplay_allow_memscan_root" in daemon_text, "daemon can direct-start context title switches without stale memscan roots", failures)
        require("launcher|context|path|relaxed) printf" in daemon_text, "daemon disables stale memscan roots for launcher/context/path title starts", failures)
        require("book-title touch-first skipped reason=launcher" in daemon_text, "daemon skips launcher-only first-row autostart", failures)
        require("book-title launcher-track-list back-to-title-list" in daemon_text, "daemon backs launcher-restored track lists to the title list", failures)
        require("restored_path:-" in daemon_text and "autostart_restore_active" in daemon_text, "daemon defers unresolved title-start bookmark overwrites", failures)
        require("book-title direct-start skipped reason=" in daemon_text, "daemon logs skipped context direct-starts", failures)
        require("restore settle after track restore path=" in daemon_text, "daemon settles before position restore after track jump", failures)
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
        require("should_attempt_restore_for_position" in daemon_text, "daemon can restore title-start bookmarks while position is zero", failures)

    catalog = root / "usr/bin/r1_audiobook_catalog.tsv"
    if expect_seed_catalog:
        require(catalog.exists() and catalog.stat().st_size > 0, f"seed catalog present: {catalog}", failures)
    elif catalog.exists():
        require(catalog.stat().st_size > 0, f"optional seed catalog nonempty: {catalog}", failures)
    else:
        print(f"OK   optional seed catalog absent: {catalog}")

    memscan = root / "usr/bin/r1_audiobook_memscan"
    require(memscan.exists(), "memscan helper present", failures)
    if memscan.exists():
        require(os.access(memscan, os.X_OK), "memscan helper is executable", failures)

    direct_open = root / "usr/bin/r1_audiobook_direct_open"
    require(direct_open.exists(), "direct-open helper present", failures)
    if direct_open.exists():
        require(os.access(direct_open, os.X_OK), "direct-open helper is executable", failures)

    ota_dir_name = f"ota_v{expected_ota_version}"
    ota_dir = out_dir / "ota-tree" / ota_dir_name
    ota_update = ota_dir / "ota_update.in"
    ota_config = out_dir / "ota-tree/ota_config.in"
    ota_ok = ota_dir / f"{ota_dir_name}.ok"
    rootfs_md5 = digest(rootfs, "md5") if rootfs.exists() else ""
    ota_rootfs_md5 = read_ini_value(ota_update, "img_md5")
    require(
        read_ini_value(ota_config, "current_version") == str(expected_ota_version),
        f"ota_config.in current_version={expected_ota_version}",
        failures,
    )
    require(
        read_ini_value(ota_update, "ota_version") == str(expected_ota_version),
        f"{ota_dir_name}/ota_update.in ota_version={expected_ota_version}",
        failures,
    )
    require(ota_ok.exists(), f"{ota_dir_name}/{ota_dir_name}.ok exists", failures)
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
    parser.add_argument("--out-dir", type=Path, default=Path("work/audiobook-firmware-1.6.16.5-audiobook"))
    parser.add_argument("--upt-name", default="r1-audiobooks-1.6.16.5-audiobook.upt")
    parser.add_argument("--expected-version", default="1.6.16.5-audiobook")
    parser.add_argument("--expected-label", default="HiBy R1 Audiobook FW 1.6.16.5")
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
    parser.add_argument(
        "--expect-batd-disabled",
        action="store_true",
        help="Require hiby_player.sh to omit the stock batd SD-card logger.",
    )
    parser.add_argument(
        "--expect-audiobook-launcher-icon",
        action="store_true",
        help="Require the generated audiobook launcher icon marker and PNG assets.",
    )
    parser.add_argument(
        "--expect-native-dsd",
        action="store_true",
        help="Require ot_devices.json to enable native DSD for the analog output path.",
    )
    parser.add_argument(
        "--expect-sbc-xq",
        action="store_true",
        help="Require bt_init to launch BlueALSA with SBC XQ quality.",
    )
    parser.add_argument(
        "--expect-usb-dac-mode",
        action="store_true",
        help="Require USB DAC mode and related hidden settings flags to be enabled.",
    )
    parser.add_argument(
        "--expect-seed-catalog",
        action="store_true",
        help="Require an embedded /usr/bin/r1_audiobook_catalog.tsv seed catalog. Public builds normally omit this.",
    )
    parser.add_argument(
        "--expect-native-hub-title-row",
        action="store_true",
        help="Verify the experimental native Audiobooks hub title-row entry path instead of the production direct launcher.",
    )
    parser.add_argument(
        "--expect-native-hub-launcher",
        action="store_true",
        help="Require the experimental launcher callback that always opens the native Audiobooks hub root.",
    )
    parser.add_argument(
        "--expect-native-hub-folder-rows",
        action="store_true",
        help="Require experimental native hub Authors/Folders rows to open the /Audiobooks explorer.",
    )
    parser.add_argument(
        "--expect-native-hub-view-rows",
        action="store_true",
        help="Require native hub Titles/Authors/Series/Folders rows to open generated audiobook view folders.",
    )
    parser.add_argument(
        "--expect-private-direct-route",
        action="store_true",
        help="Require the Audiobooks launcher to use a private direct route record.",
    )
    parser.add_argument(
        "--expect-conservative-resume-runtime",
        action="store_true",
        help="Require the safer resume runtime profile with direct-start, UI touch fallback, memscan, and back guard disabled.",
    )
    parser.add_argument(
        "--expected-ota-version",
        type=int,
        default=0,
        help="Expected numeric OTA version in ota_info and generated ota-tree metadata.",
    )
    parser.add_argument(
        "--expected-ota-site",
        default=None,
        help="Optional expected ota_site value in rootfs metadata.",
    )
    args = parser.parse_args()
    if args.expected_ota_version < 0:
        raise SystemExit("--expected-ota-version must be non-negative")
    return verify(
        args.out_dir,
        expect_current_hashes=args.expect_current_hashes,
        expected_version=args.expected_version,
        expected_label=args.expected_label,
        require_db_maintenance=args.require_db_maintenance,
        require_boot_adb=args.require_boot_adb,
        expect_batd_disabled=args.expect_batd_disabled,
        expect_audiobook_launcher_icon=args.expect_audiobook_launcher_icon,
        expect_native_dsd=args.expect_native_dsd,
        expect_sbc_xq=args.expect_sbc_xq,
        expect_usb_dac_mode=args.expect_usb_dac_mode,
        expect_seed_catalog=args.expect_seed_catalog,
        expect_native_hub_title_row=args.expect_native_hub_title_row,
        expect_native_hub_launcher=args.expect_native_hub_launcher,
        expect_native_hub_folder_rows=args.expect_native_hub_folder_rows,
        expect_native_hub_view_rows=args.expect_native_hub_view_rows,
        expect_private_direct_route=args.expect_private_direct_route,
        expect_conservative_resume_runtime=args.expect_conservative_resume_runtime,
        expected_ota_version=args.expected_ota_version,
        expected_ota_site=args.expected_ota_site,
        stock_rootfs=args.stock_rootfs,
        unsquashfs=args.unsquashfs,
        upt_name=args.upt_name,
    )


if __name__ == "__main__":
    raise SystemExit(main())
