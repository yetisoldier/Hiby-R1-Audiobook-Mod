#!/usr/bin/env python3
"""
Remove audiobook folders from a copied HiBy media database.

This is a safe prototype for keeping audiobooks out of the normal Music
library. It does not create a firmware image and it never edits the input DB
unless --in-place is supplied.
"""

from __future__ import annotations

import argparse
import shutil
import sqlite3
from pathlib import Path


DEFAULT_PREFIXES = (
    "a:\\Audiobooks\\",
    "a:\\Audiobook\\",
    "a:\\Audio Books\\",
)

MEDIA_LIKE_TABLES = (
    "MEDIA_TABLE",
    "MEDIA2_TABLE",
    "MEDIA3_TABLE",
    "SEARCH_TABLE",
    "HISTORY_TABLE",
    "COLLECT_TABLE",
    "COLLECT_OPERATE_TABLE",
    "RECENT_TABLE",
)

CATALOG_TABLES = {
    "ALBUM_TABLE": ("album", "album"),
    "ALBUM2_TABLE": ("album", "album"),
    "ARTIST_TABLE": ("artist", "artist"),
    "ARTIST2_TABLE": ("artist", "artist"),
    "ALBUM_ARTIST_TABLE": ("album_artist", "album_artist"),
    "ALBUM_ARTIST2_TABLE": ("album_artist", "album_artist"),
    "GENRE_TABLE": ("genre", "genre"),
    "GENRE2_TABLE": ("genre", "genre"),
}


def table_exists(conn: sqlite3.Connection, table: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (table,),
    ).fetchone()
    return row is not None


def table_columns(conn: sqlite3.Connection, table: str) -> set[str]:
    return {row[1] for row in conn.execute(f"PRAGMA table_info({table})")}


def like_patterns(prefixes: list[str]) -> list[str]:
    patterns = []
    for prefix in prefixes:
        normalized = prefix.replace("/", "\\")
        if not normalized.lower().startswith("a:\\"):
            normalized = "a:\\" + normalized.lstrip("\\")
        if not normalized.endswith("\\"):
            normalized += "\\"
        patterns.append(normalized + "%")
    return patterns


def delete_prefixed_rows(conn: sqlite3.Connection, patterns: list[str]) -> dict[str, int]:
    removed: dict[str, int] = {}
    for table in MEDIA_LIKE_TABLES:
        if not table_exists(conn, table):
            continue
        cols = table_columns(conn, table)
        if "path" not in cols:
            continue
        total = 0
        for pattern in patterns:
            cur = conn.execute(
                f"DELETE FROM {table} WHERE path LIKE ? COLLATE NOCASE",
                (pattern,),
            )
            total += cur.rowcount if cur.rowcount is not None else 0
        removed[table] = total
    return removed


def trim_manager_table(conn: sqlite3.Connection, patterns: list[str]) -> int:
    if not table_exists(conn, "MANAGER_TABLE"):
        return 0
    if "path" not in table_columns(conn, "MANAGER_TABLE"):
        return 0
    total = 0
    for pattern in patterns:
        cur = conn.execute(
            "DELETE FROM MANAGER_TABLE WHERE path LIKE ? COLLATE NOCASE",
            (pattern,),
        )
        total += cur.rowcount if cur.rowcount is not None else 0
    return total


def refresh_catalog_counts(conn: sqlite3.Connection) -> None:
    if not table_exists(conn, "MEDIA_TABLE"):
        return

    for table, (catalog_col, media_col) in CATALOG_TABLES.items():
        if not table_exists(conn, table):
            continue
        cols = table_columns(conn, table)
        if catalog_col not in cols:
            continue
        if "cn" in cols:
            conn.execute(
                f"""
                UPDATE {table}
                   SET cn = (
                       SELECT COUNT(id)
                         FROM MEDIA_TABLE
                        WHERE MEDIA_TABLE.{media_col} = {table}.{catalog_col}
                   )
                """
            )
            conn.execute(f"DELETE FROM {table} WHERE cn = 0")
        else:
            conn.execute(
                f"""
                DELETE FROM {table}
                 WHERE {catalog_col} NOT IN (
                       SELECT DISTINCT {media_col} FROM MEDIA_TABLE
                 )
                """
            )

    if table_exists(conn, "COUNT_TABLE") and "cn" in table_columns(conn, "COUNT_TABLE"):
        counts = [
            conn.execute("SELECT COUNT(id) FROM MEDIA_TABLE").fetchone()[0],
            conn.execute("SELECT COUNT(DISTINCT album) FROM MEDIA_TABLE").fetchone()[0],
            conn.execute("SELECT COUNT(DISTINCT artist) FROM MEDIA_TABLE").fetchone()[0],
            conn.execute("SELECT COUNT(DISTINCT genre) FROM MEDIA_TABLE").fetchone()[0],
            conn.execute("SELECT COUNT(DISTINCT album_artist) FROM MEDIA_TABLE").fetchone()[0]
            if "album_artist" in table_columns(conn, "MEDIA_TABLE")
            else 0,
        ]
        conn.execute("DELETE FROM COUNT_TABLE")
        conn.executemany("INSERT INTO COUNT_TABLE (cn) VALUES (?)", [(c,) for c in counts])


def filter_db(db_path: Path, output: Path, prefixes: list[str], in_place: bool) -> None:
    if in_place:
        target = db_path
    else:
        target = output
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(db_path, target)

    patterns = like_patterns(prefixes)
    conn = sqlite3.connect(target)
    try:
        conn.execute("PRAGMA foreign_keys = OFF")
        conn.execute("BEGIN")
        removed = delete_prefixed_rows(conn, patterns)
        manager_removed = trim_manager_table(conn, patterns)
        refresh_catalog_counts(conn)
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()

    print(f"database: {target}")
    print("prefixes:")
    for pattern in patterns:
        print(f"  {pattern}")
    print("removed rows:")
    for table, count in removed.items():
        print(f"  {table}: {count}")
    if manager_removed:
        print(f"  MANAGER_TABLE: {manager_removed}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("db", type=Path, help="Input usrlocal_media.db")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("work/usrlocal_media.filtered.db"),
        help="Output DB path when not using --in-place",
    )
    parser.add_argument(
        "--prefix",
        action="append",
        dest="prefixes",
        help=r"HiBy path prefix to remove, e.g. a:\Audiobooks\\",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="Edit the input DB directly. Use only after making a backup.",
    )
    args = parser.parse_args()

    if not args.db.exists():
        raise SystemExit(f"DB not found: {args.db}")

    filter_db(
        db_path=args.db,
        output=args.output,
        prefixes=args.prefixes or list(DEFAULT_PREFIXES),
        in_place=args.in_place,
    )


if __name__ == "__main__":
    main()
