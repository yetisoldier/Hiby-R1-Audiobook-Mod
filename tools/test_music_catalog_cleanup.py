#!/usr/bin/env python3
"""Regression-test the native-app stock Music catalog cleanup."""

from __future__ import annotations

import argparse
import shutil
import sqlite3
import subprocess
import tempfile
import time
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


def create_synthetic_fixture(path: Path, large: bool = False) -> None:
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
        if large:
            rows.extend(
                (
                    1000 + index,
                    rf"a:\Music\Load Test\Track {index:05d}.mp3",
                    "Large Music Library",
                    "Load Test Artist",
                    "Rock",
                    "Load Test Artist",
                    1000 + index,
                    100000 - index,
                )
                for index in range(6000)
            )
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
                [(1, "MP3", 1), (2, "FLAC", 1), (101, "M4B", 1)],
            )
        count_values = (
            len(rows),
            len({row[2] for row in rows}),
            len({row[3] for row in rows}),
            len({row[4] for row in rows}),
            len({row[5] for row in rows}),
        )
        for value in count_values:
            db.execute("INSERT INTO COUNT_TABLE(cn) VALUES (?)", (value,))
        for row in sorted(rows, key=lambda value: (value[6], value[0])):
            db.execute("INSERT INTO CTIME_TABLE VALUES (?)", (row[0],))
        for row in sorted(rows, key=lambda value: (-value[7], value[0])):
            db.execute("INSERT INTO MTIME_TABLE VALUES (?)", (row[0],))
        for row in rows:
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
    parser.add_argument("--large", action="store_true")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="r1-music-catalog-") as temp_dir:
        db_path = Path(temp_dir) / "usrlocal_media.db"
        if args.fixture:
            shutil.copy2(args.fixture, db_path)
        else:
            create_synthetic_fixture(db_path, large=args.large)
        with closing(sqlite3.connect(db_path)) as db:
            before_music = music_rows(db)
            before_time_order = {
                table: [row[0] for row in db.execute(f"SELECT media_id FROM {table}")]
                for table in ("CTIME_TABLE", "MTIME_TABLE")
                if table_exists(db, table)
            }
            before_formats = {
                table: db.execute(f"SELECT id,format,cn FROM {table}").fetchall()
                for table in ("FORMAT_TABLE", "FORMAT2_TABLE")
                if table_exists(db, table)
            }
            before_count_values = (
                [row[0] for row in db.execute("SELECT cn FROM COUNT_TABLE")]
                if table_exists(db, "COUNT_TABLE")
                else []
            )
            affected_values: dict[str, set[str]] = {}
            before_catalogs: dict[str, tuple[list[str], list[tuple]]] = {}
            for table, column in CATALOGS:
                if not table_exists(db, table):
                    continue
                columns = [row[1] for row in db.execute(f"PRAGMA table_info({table})")]
                before_catalogs[table] = (
                    columns,
                    db.execute(f"SELECT * FROM {table}").fetchall(),
                )
                affected_values.setdefault(column, set()).update(
                    (value if value else "Unknown").casefold()
                    for (value,) in db.execute(
                        f"SELECT DISTINCT {column} FROM MEDIA_TABLE "
                        "WHERE path LIKE ? COLLATE NOCASE",
                        (AUDIOBOOK_LIKE,),
                    )
                )
            audiobook_ids = {
                row[0]
                for row in db.execute(
                    "SELECT id FROM MEDIA_TABLE WHERE path LIKE ? COLLATE NOCASE",
                    (AUDIOBOOK_LIKE,),
                )
            }
            affected_formats = {
                row[0].rsplit(".", 1)[-1].casefold()
                for row in db.execute(
                    "SELECT path FROM MEDIA_TABLE WHERE path LIKE ? COLLATE NOCASE",
                    (AUDIOBOOK_LIKE,),
                )
                if "." in row[0]
            }
            exclusive_counts = {}
            for column in ("album", "artist", "genre", "album_artist"):
                audiobook_values = {
                    row[0]
                    for row in db.execute(
                        f"SELECT DISTINCT {column} FROM MEDIA_TABLE "
                        "WHERE path LIKE ? COLLATE NOCASE",
                        (AUDIOBOOK_LIKE,),
                    )
                }
                music_values = {
                    row[0]
                    for row in db.execute(
                        f"SELECT DISTINCT {column} FROM MEDIA_TABLE "
                        "WHERE path NOT LIKE ? COLLATE NOCASE",
                        (AUDIOBOOK_LIKE,),
                    )
                }
                exclusive_counts[column] = len(audiobook_values - music_values)
            before_audiobooks = db.execute(
                "SELECT COUNT(*) FROM MEDIA_TABLE WHERE path LIKE ? COLLATE NOCASE",
                (AUDIOBOOK_LIKE,),
            ).fetchone()[0]
        if before_audiobooks <= 0:
            raise AssertionError("fixture has no /Audiobooks rows")

        started = time.monotonic()
        first = subprocess.run(
            [str(args.helper.resolve()), str(db_path.resolve())],
            check=True,
            text=True,
            capture_output=True,
        )
        first_elapsed = time.monotonic() - started
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
        if len(before_music) >= 5000 and first_elapsed > 5.0:
            raise AssertionError(
                f"large-library cleanup took {first_elapsed:.2f}s; "
                "expected indexed reconciliation under 5s"
            )

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
                columns, before_rows = before_catalogs[table]
                column_index = columns.index(column)
                count_index = columns.index("cn")
                current_rows = db.execute(f"SELECT * FROM {table}").fetchall()
                current_by_name = {
                    (row[column_index] if row[column_index] else "Unknown").casefold(): row
                    for row in current_rows
                }
                for before_row in before_rows:
                    value = before_row[column_index] if before_row[column_index] else "Unknown"
                    key = value.casefold()
                    if key not in affected_values[column]:
                        if current_by_name.get(key) != before_row:
                            raise AssertionError(
                                f"{table} changed unrelated Music metadata for {value!r}"
                            )
                        continue
                    expected = db.execute(
                        f"SELECT COUNT(*) FROM MEDIA_TABLE WHERE "
                        f"CASE WHEN {column} IS NULL OR {column}='' THEN 'Unknown' "
                        f"ELSE {column} END = ? COLLATE NOCASE",
                        (value,),
                    ).fetchone()[0]
                    current = current_by_name.get(key)
                    if expected == 0 and current is not None:
                        raise AssertionError(
                            f"{table} retains audiobook-only metadata for {value!r}"
                        )
                    if expected > 0 and (
                        current is None or current[count_index] != expected
                    ):
                        raise AssertionError(
                            f"{table} affected count mismatch for {value!r}: "
                            f"{None if current is None else current[count_index]} != {expected}"
                        )
            expected_counts = [
                before_count_values[0] - before_audiobooks,
                before_count_values[1] - exclusive_counts["album"],
                before_count_values[2] - exclusive_counts["artist"],
                before_count_values[3] - exclusive_counts["genre"],
                before_count_values[4] - exclusive_counts["album_artist"],
            ] if before_count_values else []
            if table_exists(db, "COUNT_TABLE"):
                actual_counts = [row[0] for row in db.execute("SELECT cn FROM COUNT_TABLE")]
                if actual_counts != expected_counts:
                    raise AssertionError(
                        f"COUNT_TABLE mismatch: {actual_counts} != {expected_counts}"
                    )
            for table in ("FORMAT_TABLE", "FORMAT2_TABLE"):
                if not table_exists(db, table):
                    continue
                current = {
                    file_format.casefold(): (file_id, file_format, count)
                    for file_id, file_format, count in db.execute(
                        f"SELECT id,format,cn FROM {table}"
                    )
                }
                for before_row in before_formats[table]:
                    file_id, file_format, count = before_row
                    expected = db.execute(
                        "SELECT COUNT(*) FROM MEDIA_TABLE "
                        "WHERE LOWER(path) LIKE '%.' || LOWER(?)",
                        (file_format,),
                    ).fetchone()[0]
                    key = file_format.casefold()
                    after_row = current.get(key)
                    if expected == 0 and after_row is not None:
                        raise AssertionError(
                            f"{table} retains unused format {file_format}"
                        )
                    if expected > 0 and key not in affected_formats:
                        if after_row != before_row:
                            raise AssertionError(
                                f"{table} changed unrelated format {file_format}"
                            )
                    elif expected > 0:
                        if after_row is None or after_row[2] != count:
                            raise AssertionError(
                                f"{table} changed stock format flag for {file_format}"
                            )
                        representative = db.execute(
                            "SELECT 1 FROM MEDIA_TABLE WHERE id=? AND "
                            "LOWER(path) LIKE '%.' || LOWER(?)",
                            (after_row[0], file_format),
                        ).fetchone()
                        if representative is None:
                            raise AssertionError(
                                f"{table} format id is stale for {file_format}"
                            )
            for table in ("CTIME_TABLE", "MTIME_TABLE"):
                if not table_exists(db, table):
                    continue
                ordered_ids = [row[0] for row in db.execute(f"SELECT media_id FROM {table}")]
                ids = set(ordered_ids)
                media_ids = {row[0] for row in db.execute("SELECT id FROM MEDIA_TABLE")}
                if ids != media_ids or ids.intersection(audiobook_ids):
                    raise AssertionError(f"{table} does not exactly index remaining Music rows")
                expected_order = [
                    media_id
                    for media_id in before_time_order[table]
                    if media_id not in audiobook_ids
                ]
                if ordered_ids != expected_order:
                    raise AssertionError(f"{table} changed stock Music ordering")
            integrity = db.execute("PRAGMA integrity_check").fetchone()[0]
            if integrity != "ok":
                raise AssertionError(f"integrity_check: {integrity}")

        print(first.stdout.strip())
        print(f"first_cleanup_seconds={first_elapsed:.3f}")
        print(second.stdout.strip())
        print(
            f"PASS: removed {before_audiobooks} audiobook rows; "
            f"preserved {len(before_music)} Music rows"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
