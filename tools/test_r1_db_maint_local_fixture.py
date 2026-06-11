#!/usr/bin/env python3
"""Run a local fixture test for r1_audiobook_db_maint.

The native helper is built for the R1, but the same C source can also be built
as a Windows test executable. This test uses a disposable seed DB and fake SD
card tree to verify fallback scans without touching a device.
"""

from __future__ import annotations

import argparse
import shutil
import shlex
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


def run_helper(helper: Path, work_dir: Path, runner: list[str] | None = None) -> tuple[Path, Path]:
    sd_root = work_dir / "sdroot"
    db = work_dir / "usrlocal_media.db"
    base_dir = work_dir / "state"
    catalog = work_dir / "catalog.tsv"
    album_patterns = work_dir / "catalog-albums.txt"
    books_catalog = work_dir / "catalog-books.tsv"
    shutil.copy2(SEED_DB, db)

    music_dir = sd_root / "Music" / "Test Artist" / "Test Album"
    book_dir = (
        sd_root
        / "Audiobooks"
        / "Test Author"
        / "Test Series"
        / "2020 - Test Book [Test Series 02]"
    )
    standalone_dir = sd_root / "Audiobooks" / "Test Author" / "2021 - Standalone Book"
    write_text(music_dir / "01 - SACD.iso", "placeholder iso")
    write_text(music_dir / "cover.jpg", "fake jpg")
    write_text(book_dir / "01 - Opening.mp3", "placeholder mp3")
    write_text(book_dir / "02 - Continued.m4b", "placeholder m4b")
    write_text(book_dir / "cover.jpg", "fake jpg")
    write_text(book_dir / "01 - Opening.lrc", "[00:00.00]Opening")
    write_text(standalone_dir / "01 - Standalone.mp3", "placeholder standalone mp3")
    seed_problematic_stock_audiobook_rows(db)

    cmd = [
        *(runner or []),
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
        "--books-catalog",
        str(books_catalog),
        "--verbose",
    ]
    proc = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (work_dir / "helper.log").write_text(proc.stdout, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stdout)
        raise SystemExit(proc.returncode)
    return db, catalog


def seed_problematic_stock_audiobook_rows(db: Path) -> None:
    """Simulate stock scanner rows whose genre tags would otherwise break routing."""
    columns = (
        "id",
        "path",
        "name",
        "album",
        "artist",
        "genre",
        "year",
        "dis_id",
        "ck_id",
        "has_child_file",
        "begin_time",
        "end_time",
        "cue_id",
        "character",
        "size",
        "sample_rate",
        "bit_rate",
        "bit",
        "channel",
        "format",
        "quality",
        "album_pic_path",
        "lrc_path",
        "track_gain",
        "track_peak",
        "ctime",
        "mtime",
        "pinyin_charater",
        "album_artist",
    )
    rows = [
        (
            900,
            r"a:\Music\*",
            "Music",
            "Unknown",
            "Unknown",
            "Unknown",
            0,
            0,
            0,
            0,
            0,
            -1,
            -1,
            "M",
            12,
            0,
            0,
            0,
            0,
            0,
            "",
            "",
            "",
            0.0,
            0.0,
            1,
            1,
            "MUSIC",
            "",
        ),
        (
            1001,
            r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\02 - Continued.m4b",
            "Continued",
            "Tagged Tolkien Book",
            "Tagged Author",
            "Tolkien Audiobook",
            2020,
            0,
            2,
            0,
            0,
            -1,
            -1,
            "",
            12,
            0,
            0,
            0,
            0,
            255,
            "Lossy",
            "",
            "",
            0.0,
            0.0,
            1,
            1,
            "",
            "Tagged Author",
        ),
        (
            1002,
            r"a:\Audiobooks\Test Author\2021 - Standalone Book\01 - Standalone.mp3",
            "Standalone",
            "",
            "",
            "",
            2021,
            0,
            1,
            0,
            0,
            -1,
            -1,
            "",
            12,
            0,
            0,
            0,
            0,
            1,
            "Lossy",
            "",
            "",
            0.0,
            0.0,
            1,
            1,
            "",
            "",
        ),
    ]
    placeholders = ",".join("?" for _ in columns)
    column_list = ",".join(columns)
    conn = sqlite3.connect(db)
    try:
        conn.executemany(f"INSERT INTO MEDIA_TABLE ({column_list}) VALUES ({placeholders})", rows)
        conn.executemany(f"INSERT INTO MEDIA2_TABLE ({column_list}) VALUES ({placeholders})", rows)
        conn.execute(
            "INSERT INTO GENRE_TABLE (id, genre, character, cn, ctime, mtime, pinyin_charater) "
            "VALUES (1, 'Tolkien Audiobook', '', 1, 1, 1, '')"
        )
        conn.execute(
            "INSERT INTO GENRE2_TABLE (id, genre, character, cn, ctime, mtime, pinyin_charater) "
            "VALUES (1, 'Tolkien Audiobook', '', 1, 1, 1, '')"
        )
        conn.commit()
    finally:
        conn.close()


def verify_db(db: Path, catalog: Path) -> None:
    conn = sqlite3.connect(db)
    try:
        integrity = denul(conn.execute("PRAGMA integrity_check").fetchone()[0])
        assert_equal(integrity, "ok", "sqlite integrity")

        rows = {
            denul(path): {
                "name": denul(name),
                "album": denul(album),
                "artist": denul(artist),
                "album_artist": denul(album_artist),
                "genre": denul(genre),
                "album_pic_path": denul(album_pic_path),
                "lrc_path": denul(lrc_path),
                "format": int(format_code or 0),
                "quality": denul(quality),
            }
            for path, name, album, artist, album_artist, genre, album_pic_path, lrc_path, format_code, quality in conn.execute(
                """
                SELECT path, name, album, artist, album_artist, genre, album_pic_path, lrc_path, format, quality
                  FROM MEDIA_TABLE
                 ORDER BY path COLLATE NOCASE
                """
            )
        }
        media2_index = {
            denul(path): {
                "character": denul(character),
                "pinyin": denul(pinyin_charater),
            }
            for path, character, pinyin_charater in conn.execute(
                """
                SELECT path, character, pinyin_charater
                  FROM MEDIA2_TABLE
                 WHERE path LIKE 'a:\\Audiobooks\\%' COLLATE NOCASE
                """
            )
        }
        normal_genres = {
            denul(genre)
            for (genre,) in conn.execute(
                "SELECT genre FROM GENRE_TABLE UNION SELECT genre FROM GENRE2_TABLE"
            )
        }
        search_audiobooks = conn.execute(
            "SELECT COUNT(*) FROM SEARCH_TABLE WHERE path LIKE 'a:\\Audiobooks\\%' COLLATE NOCASE"
        ).fetchone()[0]
    finally:
        conn.close()

    first = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\01 - Opening.mp3"
    second = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\02 - Continued.m4b"
    standalone = r"a:\Audiobooks\Test Author\2021 - Standalone Book\01 - Standalone.mp3"
    music_iso = r"a:\Music\Test Artist\Test Album\01 - SACD.iso"
    cover = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\cover.jpg"
    lrc = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\01 - Opening.lrc"
    music_cover = r"a:\Music\Test Artist\Test Album\cover.jpg"

    assert_true(first in rows, "first audiobook row generated")
    assert_true(second in rows, "m4b audiobook row generated")
    assert_true(standalone in rows, "standalone audiobook row generated")
    assert_true(music_iso in rows, "iso music row generated")
    assert_true(r"a:\Music\*" not in rows, "stock placeholder music row replaced")
    assert_equal(rows[first]["album"], "Test Book", "guide folder fallback book title")
    assert_equal(rows[first]["artist"], "Test Author", "guide folder fallback artist")
    assert_equal(rows[first]["album_artist"], "Test Author", "guide folder fallback album artist")
    assert_equal(media2_index[first]["character"], "T", "audiobook album-route side index uses book title")
    assert_equal(media2_index[first]["pinyin"], "TEST BOOK", "audiobook album-route pinyin uses book title")
    assert_equal(rows[second]["genre"], "Audiobook", "custom m4b genre normalized")
    assert_equal(rows[standalone]["genre"], "Audiobook", "blank audiobook genre normalized")
    assert_true("Tolkien Audiobook" not in normal_genres, "custom audiobook genre removed from music genres")
    assert_equal(search_audiobooks, 0, "audiobooks excluded from search table")
    assert_equal(rows[standalone]["album"], "Standalone Book", "standalone folder fallback book title")
    assert_equal(rows[standalone]["artist"], "Test Author", "standalone folder fallback artist")
    assert_equal(rows[first]["album_pic_path"], cover, "audiobook cover sidecar")
    assert_equal(rows[first]["lrc_path"], lrc, "audiobook lrc sidecar")
    assert_equal(rows[second]["format"], 255, "m4b format code")
    assert_equal(rows[music_iso]["format"], 0, "iso format code")
    assert_equal(rows[music_iso]["album_pic_path"], music_cover, "music cover sidecar")
    assert_equal(rows[music_iso]["quality"], "Lossless", "iso quality fallback")
    assert_true(catalog.exists() and catalog.stat().st_size > 0, "catalog written")
    header = catalog.read_text(encoding="utf-8").splitlines()[0].split("\t")
    assert_true("book_key" in header, "catalog book_key column")
    assert_true("series" in header and "series_part" in header, "catalog guide series columns")
    catalog_rows = {
        fields[4]: fields
        for fields in (
            line.split("\t")
            for line in catalog.read_text(encoding="utf-8").splitlines()[1:]
            if line
        )
    }
    assert_equal(catalog_rows[first][header.index("series")], "Test Series", "catalog guide series")
    assert_equal(catalog_rows[first][header.index("series_part")], "02", "catalog guide series part")
    assert_equal(catalog_rows[standalone][header.index("series")], "", "standalone catalog series blank")
    assert_equal(
        catalog_rows[standalone][header.index("series_part")],
        "",
        "standalone catalog series part blank",
    )
    books_catalog = catalog.parent / "catalog-books.tsv"
    assert_true(books_catalog.exists() and books_catalog.stat().st_size > 0, "book-level catalog written")
    books_lines = books_catalog.read_text(encoding="utf-8").splitlines()
    books_header = books_lines[0].split("\t")
    assert_true("author" in books_header and "series" in books_header, "book-level catalog view columns")
    book_rows = {
        fields[0]: fields
        for fields in (
            line.split("\t")
            for line in books_lines[1:]
            if line
        )
    }
    series_root = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]"
    standalone_root = r"a:\Audiobooks\Test Author\2021 - Standalone Book"
    assert_equal(book_rows[series_root][books_header.index("album")], "Test Book", "book-level catalog title")
    assert_equal(book_rows[series_root][books_header.index("author")], "Test Author", "book-level catalog author")
    assert_equal(book_rows[series_root][books_header.index("series")], "Test Series", "book-level catalog series")
    assert_equal(book_rows[series_root][books_header.index("series_part")], "02", "book-level catalog series part")
    assert_equal(book_rows[series_root][books_header.index("track_count")], "2", "book-level catalog track count")
    assert_equal(book_rows[standalone_root][books_header.index("series")], "", "standalone book-level series blank")
    assert_equal(book_rows[standalone_root][books_header.index("series_part")], "", "standalone book-level series part blank")

    check = subprocess.run(
        [
            sys.executable,
            str(REPO_ROOT / "tools" / "check_audiobook_release_state.py"),
            str(db),
            "--catalog",
            str(catalog),
            "--books-catalog",
            str(books_catalog),
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
    parser.add_argument(
        "--runner",
        help="Optional command prefix, for example qemu-mipsel-static",
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
    runner = shlex.split(args.runner) if args.runner else None
    work_dir = args.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    db, catalog = run_helper(helper, work_dir, runner)
    verify_db(db, catalog)
    print(f"fixture: {work_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
