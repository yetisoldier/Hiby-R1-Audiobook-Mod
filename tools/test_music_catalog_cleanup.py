#!/usr/bin/env python3
"""Regression-test the native-app stock Music catalog cleanup."""

from __future__ import annotations

import argparse
import shutil
import sqlite3
import subprocess
import tempfile
from contextlib import closing
from pathlib import Path


AUDIOBOOK_LIKE = r"a:\Audiobooks\%"
PATH_TABLES = (
    "MEDIA_TABLE",
    "MEDIA2_TABLE",
    "MEDIA3_TABLE",
    "SEARCH_TABLE",
    "RECENT_TABLE",
    "HISTORY_TABLE",
    "COLLECT_TABLE",
    "COLLECT_OPERATE_TABLE",
)
CATALOGS = (
    ("ARTIST_TABLE", "artist"),
    ("ARTIST2_TABLE", "artist"),
    ("ALBUM_TABLE", "album"),
    ("ALBUM2_TABLE", "album"),
    ("GENRE_TABLE", "genre"),
    ("GENRE2_TABLE", "genre"),
    ("ALBUM_ARTIST_TABLE", "album_artist"),
    ("ALBUM_ARTIST2_TABLE", "album_artist"),
)


def create_synthetic_fixture(path: Path) -> None:
    media_columns = (
        "id INTEGER, path TEXT COLLATE NOCASE, album TEXT COLLATE NOCASE, "
        "artist TEXT COLLATE NOCASE, genre TEXT COLLATE NOCASE, "
        "album_artist TEXT COLLATE NOCASE, ctime INTEGER, mtime INTEGER"
    )
    with closing(sqlite3.connect(path)) as db:
        db.execute(f"CREATE TABLE MEDIA_TABLE({media_columns})")
        for table in PATH_TABLES[1:]:
            db.execute(f"CREATE TABLE {table}(id INTEGER, path TEXT COLLATE NOCASE)")
        for table, column in CATALOGS:
            db.execute(
                f"CREATE TABLE {table}(id INTEGER, {column} TEXT COLLATE NOCASE, "
                "cn INTEGER, ctime INTEGER, mtime INTEGER)"
            )
        db.execute("CREATE TABLE FORMAT_TABLE(id INTEGER,format TEXT,cn INTEGER)")
        db.execute("CREATE TABLE FORMAT2_TABLE(id INTEGER,format TEXT,cn INTEGER)")
        db.execute("CREATE TABLE COUNT_TABLE(cn INTEGER)")
        db.execute("CREATE TABLE CTIME_TABLE(media_id INTEGER)")
        db.execute("CREATE TABLE MTIME_TABLE(media_id INTEGER)")
        db.execute("CREATE TABLE INFO_TABLE(id INTEGER)")

        rows = [
            (1, r"a:\Music\Shared.mp3", "Shared", "Same Author", "Spoken", "Same Author", 10, 20),
            (2, r"a:\Music\Only Music.flac", "Music Only", "Musician", "Rock", "Musician", 11, 21),
            (100, r"a:\Audiobooks\Shared\01.mp3", "Shared", "Same Author", "Spoken", "Same Author", 12, 22),
            (101, r"a:\Audiobooks\Book Only\Book.m4b", "Book Only", "Writer", "Audiobook", "Writer", 13, 23),
        ]
        db.executemany("INSERT INTO MEDIA_TABLE VALUES (?,?,?,?,?,?,?,?)", rows)
        for table in PATH_TABLES[1:]:
            db.executemany(
                f"INSERT INTO {table}(id,path) VALUES (?,?)",
                [(row[0], row[1]) for row in rows],
            )
        for table, column in CATALOGS:
            index = {"album": 2, "artist": 3, "genre": 4, "album_artist": 5}[column]
            values: dict[str, list[int]] = {}
            for row in rows:
                values.setdefault(row[index], []).append(row[0])
            for value, ids in values.items():
                matching = [row for row in rows if row[index] == value]
                db.execute(
                    f"INSERT INTO {table}(id,{column},cn,ctime,mtime) VALUES (?,?,?,?,?)",
                    (min(ids), value, len(ids), min(row[6] for row in matching),
                     max(row[7] for row in matching)),
                )
        for table in ("FORMAT_TABLE", "FORMAT2_TABLE"):
            db.executemany(
                f"INSERT INTO {table}(id,format,cn) VALUES (?,?,?)",
                [(1, "MP3", 2), (2, "FLAC", 1), (101, "M4B", 1)],
            )
        for value in (4, 3, 3, 3, 3):
            db.execute("INSERT INTO COUNT_TABLE(cn) VALUES (?)", (value,))
        for row in rows:
            db.execute("INSERT INTO CTIME_TABLE VALUES (?)", (row[0],))
            db.execute("INSERT INTO MTIME_TABLE VALUES (?)", (row[0],))
            db.execute("INSERT INTO INFO_TABLE VALUES (?)", (row[0],))
        db.commit()


def table_exists(db: sqlite3.Connection, table: str) -> bool:
    return db.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)
    ).fetchone() is not None


def music_rows(db: sqlite3.Connection) -> list[tuple]:
    return db.execute(
        "SELECT * FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE ORDER BY id,path",
        (AUDIOBOOK_LIKE,),
    ).fetchall()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--fixture", type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="r1-music-catalog-") as temp_dir:
        db_path = Path(temp_dir) / "usrlocal_media.db"
        if args.fixture:
            shutil.copy2(args.fixture, db_path)
        else:
            create_synthetic_fixture(db_path)
        with closing(sqlite3.connect(db_path)) as db:
            before_music = music_rows(db)
            audiobook_ids = {
                row[0]
                for row in db.execute(
                    "SELECT id FROM MEDIA_TABLE WHERE path LIKE ? COLLATE NOCASE",
                    (AUDIOBOOK_LIKE,),
                )
            }
            before_audiobooks = db.execute(
                "SELECT COUNT(*) FROM MEDIA_TABLE WHERE path LIKE ? COLLATE NOCASE",
                (AUDIOBOOK_LIKE,),
            ).fetchone()[0]
        if before_audiobooks <= 0:
            raise AssertionError("fixture has no /Audiobooks rows")

        first = subprocess.run(
            [str(args.helper.resolve()), str(db_path.resolve())],
            check=True,
            text=True,
            capture_output=True,
        )
        second = subprocess.run(
            [str(args.helper.resolve()), str(db_path.resolve())],
            check=True,
            text=True,
            capture_output=True,
        )
        if f"removed={before_audiobooks}" not in first.stdout:
            raise AssertionError(f"unexpected first result: {first.stdout.strip()}")
        if "changed=0 removed=0" not in second.stdout:
            raise AssertionError(f"cleanup is not idempotent: {second.stdout.strip()}")

        with closing(sqlite3.connect(db_path)) as db:
            if music_rows(db) != before_music:
                raise AssertionError("legitimate Music rows changed")
            for table in PATH_TABLES:
                if not table_exists(db, table):
                    continue
                leaked = db.execute(
                    f"SELECT COUNT(*) FROM {table} WHERE path LIKE ? COLLATE NOCASE",
                    (AUDIOBOOK_LIKE,),
                ).fetchone()[0]
                if leaked:
                    raise AssertionError(f"{table} retains {leaked} audiobook rows")
            for table, column in CATALOGS:
                if not table_exists(db, table):
                    continue
                rows = db.execute(f"SELECT {column},cn FROM {table}").fetchall()
                for value, count in rows:
                    expected = db.execute(
                        f"SELECT COUNT(*) FROM MEDIA_TABLE WHERE "
                        f"CASE WHEN {column} IS NULL OR {column}='' THEN 'Unknown' "
                        f"ELSE {column} END = ? COLLATE NOCASE",
                        (value,),
                    ).fetchone()[0]
                    if count != expected:
                        raise AssertionError(
                            f"{table} count mismatch for {value!r}: {count} != {expected}"
                        )
            expected_counts = [
                db.execute("SELECT COUNT(*) FROM MEDIA_TABLE").fetchone()[0],
                db.execute("SELECT COUNT(DISTINCT album) FROM MEDIA_TABLE").fetchone()[0],
                db.execute("SELECT COUNT(DISTINCT artist) FROM MEDIA_TABLE").fetchone()[0],
                db.execute("SELECT COUNT(DISTINCT genre) FROM MEDIA_TABLE").fetchone()[0],
                db.execute(
                    "SELECT COUNT(DISTINCT album_artist) FROM MEDIA_TABLE"
                ).fetchone()[0],
            ]
            if table_exists(db, "COUNT_TABLE"):
                actual_counts = [row[0] for row in db.execute("SELECT cn FROM COUNT_TABLE")]
                if actual_counts != expected_counts:
                    raise AssertionError(
                        f"COUNT_TABLE mismatch: {actual_counts} != {expected_counts}"
                    )
            for table in ("FORMAT_TABLE", "FORMAT2_TABLE"):
                if not table_exists(db, table):
                    continue
                for file_format, count in db.execute(f"SELECT format,cn FROM {table}"):
                    expected = db.execute(
                        "SELECT COUNT(*) FROM MEDIA_TABLE "
                        "WHERE LOWER(path) LIKE '%.' || LOWER(?)",
                        (file_format,),
                    ).fetchone()[0]
                    if count != expected or count <= 0:
                        raise AssertionError(
                            f"{table} count mismatch for {file_format}: {count} != {expected}"
                        )
            for table in ("CTIME_TABLE", "MTIME_TABLE"):
                if not table_exists(db, table):
                    continue
                ids = {row[0] for row in db.execute(f"SELECT media_id FROM {table}")}
                media_ids = {row[0] for row in db.execute("SELECT id FROM MEDIA_TABLE")}
                if ids != media_ids or ids.intersection(audiobook_ids):
                    raise AssertionError(f"{table} does not exactly index remaining Music rows")
            integrity = db.execute("PRAGMA integrity_check").fetchone()[0]
            if integrity != "ok":
                raise AssertionError(f"integrity_check: {integrity}")

        print(first.stdout.strip())
        print(second.stdout.strip())
        print(
            f"PASS: removed {before_audiobooks} audiobook rows; "
            f"preserved {len(before_music)} Music rows"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
