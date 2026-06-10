#!/usr/bin/env python3
"""Run a local fixture test for r1_audiobook_db_maint.

The native helper is built for the R1, but the same C source can also be built
as a Windows test executable. This test uses a disposable seed DB and fake SD
card tree to verify fallback scans without touching a device.
"""

from __future__ import annotations

import argparse
import shutil
import sqlite3
import subprocess
import sys
from datetime import datetime
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_DB = REPO_ROOT / "firmware" / "seed" / "usrlocal_media.seed.db"


def denul(value: object) -> str:
    return "" if value is None else str(value).rstrip("\x00")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def assert_equal(actual: object, expected: object, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")
    print(f"OK   {label}")


def assert_true(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)
    print(f"OK   {label}")


def run_helper(helper: Path, work_dir: Path) -> tuple[Path, Path]:
    sd_root = work_dir / "sdroot"
    db = work_dir / "usrlocal_media.db"
    base_dir = work_dir / "state"
    catalog = work_dir / "catalog.tsv"
    album_patterns = work_dir / "catalog-albums.txt"
    shutil.copy2(SEED_DB, db)

    music_dir = sd_root / "Music" / "Test Artist" / "Test Album"
    book_dir = sd_root / "Audiobooks" / "Test Author" / "2020 - Test Book"
    write_text(music_dir / "01 - SACD.iso", "placeholder iso")
    write_text(music_dir / "cover.jpg", "fake jpg")
    write_text(book_dir / "01 - Opening.mp3", "placeholder mp3")
    write_text(book_dir / "02 - Continued.m4b", "placeholder m4b")
    write_text(book_dir / "cover.jpg", "fake jpg")
    write_text(book_dir / "01 - Opening.lrc", "[00:00.00]Opening")

    cmd = [
        str(helper),
        "--db",
        str(db),
        "--sd-root",
        str(sd_root),
        "--music-dir",
        str(sd_root / "Music"),
        "--audiobooks-dir",
        str(sd_root / "Audiobooks"),
        "--base-dir",
        str(base_dir),
        "--catalog",
        str(catalog),
        "--album-patterns",
        str(album_patterns),
        "--verbose",
    ]
    proc = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (work_dir / "helper.log").write_text(proc.stdout, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stdout)
        raise SystemExit(proc.returncode)
    return db, catalog


def verify_db(db: Path, catalog: Path) -> None:
    conn = sqlite3.connect(db)
    try:
        integrity = denul(conn.execute("PRAGMA integrity_check").fetchone()[0])
        assert_equal(integrity, "ok", "sqlite integrity")

        rows = {
            denul(path): {
                "name": denul(name),
                "album": denul(album),
                "album_pic_path": denul(album_pic_path),
                "lrc_path": denul(lrc_path),
                "format": int(format_code or 0),
                "quality": denul(quality),
            }
            for path, name, album, album_pic_path, lrc_path, format_code, quality in conn.execute(
                """
                SELECT path, name, album, album_pic_path, lrc_path, format, quality
                  FROM MEDIA_TABLE
                 ORDER BY path COLLATE NOCASE
                """
            )
        }
    finally:
        conn.close()

    first = r"a:\Audiobooks\Test Author\2020 - Test Book\01 - Opening.mp3"
    second = r"a:\Audiobooks\Test Author\2020 - Test Book\02 - Continued.m4b"
    music_iso = r"a:\Music\Test Artist\Test Album\01 - SACD.iso"
    cover = r"a:\Audiobooks\Test Author\2020 - Test Book\cover.jpg"
    lrc = r"a:\Audiobooks\Test Author\2020 - Test Book\01 - Opening.lrc"
    music_cover = r"a:\Music\Test Artist\Test Album\cover.jpg"

    assert_true(first in rows, "first audiobook row generated")
    assert_true(second in rows, "m4b audiobook row generated")
    assert_true(music_iso in rows, "iso music row generated")
    assert_equal(rows[first]["album_pic_path"], cover, "audiobook cover sidecar")
    assert_equal(rows[first]["lrc_path"], lrc, "audiobook lrc sidecar")
    assert_equal(rows[second]["format"], 255, "m4b format code")
    assert_equal(rows[music_iso]["format"], 0, "iso format code")
    assert_equal(rows[music_iso]["album_pic_path"], music_cover, "music cover sidecar")
    assert_equal(rows[music_iso]["quality"], "Lossless", "iso quality fallback")
    assert_true(catalog.exists() and catalog.stat().st_size > 0, "catalog written")

    check = subprocess.run(
        [
            sys.executable,
            str(REPO_ROOT / "tools" / "check_audiobook_release_state.py"),
            str(db),
            "--catalog",
            str(catalog),
            "--expect-audiobooks",
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    (catalog.parent / "release-check.log").write_text(check.stdout, encoding="utf-8", errors="replace")
    if check.returncode != 0:
        print(check.stdout)
        raise SystemExit(check.returncode)
    print("OK   release-state invariants")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--helper",
        type=Path,
        default=REPO_ROOT / "work" / "native-db-maint" / "r1_audiobook_db_maint_win_test.exe",
        help="Windows build of r1_audiobook_db_maint to test",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=REPO_ROOT
        / "work"
        / "native-db-maint"
        / f"enhanced-fixture-{datetime.now().strftime('%Y%m%d-%H%M%S')}",
        help="Disposable output directory",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    helper = args.helper.resolve()
    if not helper.exists():
        print(f"helper not found: {helper}", file=sys.stderr)
        return 2
    if not SEED_DB.exists():
        print(f"seed DB not found: {SEED_DB}", file=sys.stderr)
        return 2
    work_dir = args.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    db, catalog = run_helper(helper, work_dir)
    verify_db(db, catalog)
    print(f"fixture: {work_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
