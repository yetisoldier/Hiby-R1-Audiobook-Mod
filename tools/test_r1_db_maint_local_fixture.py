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
    view_root = sd_root / "Audiobooks" / "_views"
    catalog = work_dir / "catalog.tsv"
    album_patterns = work_dir / "catalog-albums.txt"
    books_catalog = work_dir / "catalog-books.tsv"
    titles_catalog = work_dir / "catalog-view-title.tsv"
    authors_catalog = work_dir / "catalog-view-author.tsv"
    series_catalog = work_dir / "catalog-view-series.tsv"
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
    article_dir = (
        sd_root
        / "Audiobooks"
        / "Test Author"
        / "Test Series"
        / "2022 - The Amber Book [Test Series 03]"
    )
    number_dir = sd_root / "Audiobooks" / "Test Author" / "2022 - Number Book"
    long_dir = (
        sd_root
        / "Audiobooks"
        / "Author With Long Name"
        / "Long Series"
        / "2023 - Long Path Book With Fixture Coverage"
    )
    write_text(music_dir / "01 - SACD.iso", "placeholder iso")
    write_text(music_dir / "cover.jpg", "fake jpg")
    write_text(book_dir / "01 - Opening.mp3", "placeholder mp3")
    write_text(book_dir / "02 - Continued.m4b", "placeholder m4b")
    write_text(book_dir / "cover.jpg", "fake jpg")
    write_text(book_dir / "01 - Opening.lrc", "[00:00.00]Opening")
    write_text(standalone_dir / "01 - Standalone.mp3", "placeholder standalone mp3")
    write_text(article_dir / "01 - Prologue.mp3", "placeholder article mp3")
    write_text(number_dir / "1 - Start.mp3", "placeholder number mp3")
    write_text(number_dir / "10 - Later.mp3", "placeholder number mp3")
    write_text(number_dir / "2 - Middle.mp3", "placeholder number mp3")
    write_text(long_dir / "01 - Long Path.mp3", "placeholder long path mp3")
    assert_needs_maintenance(
        helper,
        db,
        runner,
        True,
        "missing audiobook rows need maintenance",
        sd_root=sd_root,
    )
    seed_problematic_stock_audiobook_rows(db)
    assert_needs_maintenance(helper, db, runner, True, "stock audiobook rows need maintenance", sd_root=sd_root)

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
        "--view-root",
        str(view_root),
        "--base-dir",
        str(base_dir),
        "--catalog",
        str(catalog),
        "--album-patterns",
        str(album_patterns),
        "--books-catalog",
        str(books_catalog),
        "--titles-catalog",
        str(titles_catalog),
        "--authors-catalog",
        str(authors_catalog),
        "--series-catalog",
        str(series_catalog),
        "--verbose",
    ]
    proc = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (work_dir / "helper.log").write_text(proc.stdout, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stdout)
        raise SystemExit(proc.returncode)
    assert_needs_maintenance(helper, db, runner, False, "repaired audiobook rows do not need maintenance", sd_root=sd_root)
    return db, catalog


def assert_needs_maintenance(
    helper: Path,
    db: Path,
    runner: list[str] | None,
    expected: bool,
    label: str,
    sd_root: Path | None = None,
) -> None:
    cmd = [
        *(runner or []),
        str(helper),
        "--db",
        str(db),
        "--needs-maintenance",
        "--verbose",
    ]
    if sd_root is not None:
        cmd.extend(
            [
                "--sd-root",
                str(sd_root),
                "--music-dir",
                str(sd_root / "Music"),
                "--audiobooks-dir",
                str(sd_root / "Audiobooks"),
            ]
        )
    proc = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if expected:
        if proc.returncode != 10:
            print(proc.stdout)
            raise AssertionError(f"{label}: expected exit 10, got {proc.returncode}")
    elif proc.returncode != 0:
        print(proc.stdout)
        raise AssertionError(f"{label}: expected exit 0, got {proc.returncode}")
    print(f"OK   {label}")


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
    article = r"a:\Audiobooks\Test Author\Test Series\2022 - The Amber Book [Test Series 03]\01 - Prologue.mp3"
    number_one = r"a:\Audiobooks\Test Author\2022 - Number Book\1 - Start.mp3"
    number_two = r"a:\Audiobooks\Test Author\2022 - Number Book\2 - Middle.mp3"
    number_ten = r"a:\Audiobooks\Test Author\2022 - Number Book\10 - Later.mp3"
    long_path = (
        r"a:\Audiobooks\Author With Long Name\Long Series"
        r"\2023 - Long Path Book With Fixture Coverage\01 - Long Path.mp3"
    )
    music_iso = r"a:\Music\Test Artist\Test Album\01 - SACD.iso"
    cover = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\cover.jpg"
    lrc = r"a:\Audiobooks\Test Author\Test Series\2020 - Test Book [Test Series 02]\01 - Opening.lrc"
    music_cover = r"a:\Music\Test Artist\Test Album\cover.jpg"

    assert_true(first in rows, "first audiobook row generated")
    assert_true(second in rows, "m4b audiobook row generated")
    assert_true(standalone in rows, "standalone audiobook row generated")
    assert_true(article in rows, "article-prefixed audiobook row generated")
    assert_true(number_one in rows and number_two in rows and number_ten in rows, "unpadded multipart rows generated")
    assert_true(long_path in rows, "long audiobook path row generated")
    assert_true(music_iso in rows, "iso music row generated")
    assert_true(r"a:\Music\*" not in rows, "stock placeholder music row replaced")
    assert_equal(rows[first]["album"], "Test Book", "guide folder fallback book title")
    assert_equal(rows[first]["artist"], "Test Author", "guide folder fallback artist")
    assert_equal(rows[first]["album_artist"], "Test Author", "guide folder fallback album artist")
    assert_equal(media2_index[first]["character"], "T", "audiobook album-route side index uses book title")
    assert_equal(media2_index[first]["pinyin"], "TEST BOOK", "audiobook album-route pinyin uses book title")
    assert_equal(rows[article]["album"], "The Amber Book", "article-prefixed book title preserved")
    assert_equal(media2_index[article]["character"], "A", "article stripped from side index")
    assert_equal(media2_index[article]["pinyin"], "AMBER BOOK", "article stripped from pinyin")
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
    catalog_entries = [
        fields
        for fields in (
            line.split("\t")
            for line in catalog.read_text(encoding="utf-8").splitlines()[1:]
            if line
        )
    ]
    catalog_rows = {fields[4]: fields for fields in catalog_entries}
    assert_equal(catalog_rows[first][header.index("series")], "Test Series", "catalog guide series")
    assert_equal(catalog_rows[first][header.index("series_part")], "02", "catalog guide series part")
    assert_equal(catalog_rows[standalone][header.index("series")], "", "standalone catalog series blank")
    assert_equal(
        catalog_rows[standalone][header.index("series_part")],
        "",
        "standalone catalog series part blank",
    )
    number_root = r"a:\Audiobooks\Test Author\2022 - Number Book"
    number_order = [
        fields[header.index("path")]
        for fields in sorted(
            (fields for fields in catalog_entries if fields[0] == number_root),
            key=lambda fields: int(fields[header.index("track_index")]),
        )
    ]
    assert_equal(
        number_order,
        [number_one, number_two, number_ten],
        "unpadded multipart catalog track order",
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
    titles_catalog = catalog.parent / "catalog-view-title.tsv"
    authors_catalog = catalog.parent / "catalog-view-author.tsv"
    series_catalog = catalog.parent / "catalog-view-series.tsv"
    assert_true(titles_catalog.exists() and titles_catalog.stat().st_size > 0, "title-view catalog written")
    assert_true(authors_catalog.exists() and authors_catalog.stat().st_size > 0, "author-view catalog written")
    assert_true(series_catalog.exists() and series_catalog.stat().st_size > 0, "series-view catalog written")

    title_lines = titles_catalog.read_text(encoding="utf-8").splitlines()
    title_header = title_lines[0].split("\t")
    title_rows = [line.split("\t") for line in title_lines[1:] if line]
    assert_equal(title_header[:4], ["character", "pinyin_charater", "album", "author"], "title-view header")
    assert_equal(title_rows[0][title_header.index("album")], "The Amber Book", "title-view strips article for sort")
    assert_equal(title_rows[0][title_header.index("character")], "A", "title-view character strips article")
    assert_equal(title_rows[0][title_header.index("pinyin_charater")], "AMBER BOOK", "title-view pinyin strips article")

    author_lines = authors_catalog.read_text(encoding="utf-8").splitlines()
    author_header = author_lines[0].split("\t")
    author_rows = [line.split("\t") for line in author_lines[1:] if line]
    assert_equal(author_header[:4], ["character", "pinyin_charater", "author", "album"], "author-view header")
    assert_equal(author_rows[0][author_header.index("author")], "Author With Long Name", "author-view sorts by author")

    series_lines = series_catalog.read_text(encoding="utf-8").splitlines()
    series_header = series_lines[0].split("\t")
    series_rows = [line.split("\t") for line in series_lines[1:] if line]
    assert_equal(series_header[:6], ["character", "pinyin_charater", "series", "series_part", "album", "author"], "series-view header")
    assert_true(all(row[series_header.index("series")] for row in series_rows), "series-view omits standalone books")
    test_series_titles = [
        row[series_header.index("album")]
        for row in series_rows
        if row[series_header.index("series")] == "Test Series"
    ]
    assert_equal(test_series_titles, ["Test Book", "The Amber Book"], "series-view sorts numeric series parts")

    view_root = catalog.parent / "sdroot" / "Audiobooks" / "_views"
    titles_view = view_root / "Titles"
    authors_view = view_root / "Authors"
    series_view_dir = view_root / "Series"
    title_playlists = sorted(titles_view.glob("*.m3u"))
    author_playlists = sorted(authors_view.glob("*/*.m3u"))
    series_playlists = sorted(series_view_dir.glob("*/*.m3u"))
    assert_equal(len(title_playlists), len(book_rows), "generated title playlists per book")
    assert_equal(len(author_playlists), len(book_rows), "generated author/book playlists per book")
    assert_equal(len(series_playlists), len(series_rows), "generated series playlists omit standalone books")
    assert_true(
        any(path.name == "Standalone Book - Test Author.m3u" for path in title_playlists),
        "standalone title playlist visible",
    )
    assert_true(
        (authors_view / "Test Author" / "Standalone Book.m3u").exists(),
        "author view nests books under author",
    )
    assert_true(
        (series_view_dir / "Test Series" / "02 - Test Book.m3u").exists(),
        "series view prefixes numeric series part",
    )
    standalone_playlist = (authors_view / "Test Author" / "Standalone Book.m3u").read_text(encoding="utf-8").splitlines()
    assert_equal(
        standalone_playlist,
        [r"..\..\..\Test Author\2021 - Standalone Book\01 - Standalone.mp3"],
        "author playlist uses path relative to generated view folder",
    )
    assert_true(
        all("_views" not in row[header.index("path")] for row in catalog_entries),
        "generated views are not scanned as audiobook tracks",
    )

    check = subprocess.run(
        [
            sys.executable,
            str(REPO_ROOT / "tools" / "check_audiobook_release_state.py"),
            str(db),
            "--catalog",
            str(catalog),
            "--books-catalog",
            str(books_catalog),
            "--titles-catalog",
            str(titles_catalog),
            "--authors-catalog",
            str(authors_catalog),
            "--series-catalog",
            str(series_catalog),
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
