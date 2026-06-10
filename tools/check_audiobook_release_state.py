#!/usr/bin/env python3
"""Check HiBy R1 audiobook release database/catalog invariants."""

from __future__ import annotations

import argparse
import sqlite3
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


PREFIX = "a:\\Audiobooks\\"


@dataclass(frozen=True)
class MediaRow:
    media_id: int
    path: str
    title: str
    album: str
    artist: str
    album_artist: str
    genre: str

    @property
    def root(self) -> str:
        return self.path.rsplit("\\", 1)[0]


def denul(value: object) -> str:
    return "" if value is None else str(value).rstrip("\x00")


def table_exists(conn: sqlite3.Connection, table: str) -> bool:
    return (
        conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
            (table,),
        ).fetchone()
        is not None
    )


def require_table(conn: sqlite3.Connection, table: str, failures: list[str]) -> bool:
    if table_exists(conn, table):
        return True
    failures.append(f"missing table: {table}")
    return False


def load_audiobook_rows(conn: sqlite3.Connection, table: str, failures: list[str]) -> list[MediaRow]:
    if not require_table(conn, table, failures):
        return []
    rows: list[MediaRow] = []
    for media_id, path, title, album, artist, album_artist, genre in conn.execute(
        f"""
        SELECT id, path, name, album, artist, album_artist, genre
          FROM {table}
         WHERE path LIKE ? COLLATE NOCASE
         ORDER BY path COLLATE NOCASE
        """,
        (PREFIX + "%",),
    ):
        rows.append(
            MediaRow(
                media_id=int(media_id),
                path=denul(path),
                title=denul(title),
                album=denul(album),
                artist=denul(artist),
                album_artist=denul(album_artist),
                genre=denul(genre),
            )
        )
    return rows


def count_audiobook_paths(conn: sqlite3.Connection, table: str, failures: list[str]) -> int:
    if not require_table(conn, table, failures):
        return 0
    return int(
        conn.execute(
            f"SELECT COUNT(*) FROM {table} WHERE path LIKE ? COLLATE NOCASE",
            (PREFIX + "%",),
        ).fetchone()[0]
    )


def catalog_values(conn: sqlite3.Connection, table: str, column: str, failures: list[str]) -> set[str]:
    if not require_table(conn, table, failures):
        return set()
    return {denul(row[0]) for row in conn.execute(f"SELECT {column} FROM {table}")}


def load_catalog(path: Path) -> dict[str, list[tuple[int, int, str]]]:
    by_root: dict[str, list[tuple[int, int, str]]] = defaultdict(list)
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        header = handle.readline().rstrip("\n").split("\t")
        expected = [
            "root_hiby_path",
            "track_index",
            "track_count",
            "media_id",
            "path",
            "title",
            "album",
            "author",
        ]
        if header != expected:
            raise ValueError(f"unexpected catalog header: {header}")
        for line_number, raw in enumerate(handle, 2):
            line = raw.rstrip("\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) != len(expected):
                raise ValueError(f"catalog line {line_number}: expected 8 fields, got {len(fields)}")
            root, track_index, track_count, _media_id, media_path, *_rest = fields
            by_root[root].append((int(track_index), int(track_count), media_path))
    return by_root


def check_catalog(catalog: Path, media_rows: list[MediaRow], failures: list[str]) -> None:
    if not catalog.exists():
        failures.append(f"catalog not found: {catalog}")
        return
    try:
        catalog_roots = load_catalog(catalog)
    except Exception as exc:
        failures.append(f"catalog parse failed: {exc}")
        return

    media_by_root: dict[str, list[str]] = defaultdict(list)
    for row in media_rows:
        media_by_root[row.root].append(row.path)

    if set(catalog_roots) != set(media_by_root):
        missing = sorted(set(media_by_root) - set(catalog_roots))
        extra = sorted(set(catalog_roots) - set(media_by_root))
        if missing:
            failures.append(f"catalog missing roots: {missing[:5]}")
        if extra:
            failures.append(f"catalog has extra roots: {extra[:5]}")

    for root, entries in catalog_roots.items():
        paths = [entry[2] for entry in entries]
        expected_paths = sorted(media_by_root.get(root, []), key=str.lower)
        if sorted(paths, key=str.lower) != expected_paths:
            failures.append(f"catalog path mismatch for root: {root}")
            continue
        count_values = {entry[1] for entry in entries}
        if count_values != {len(entries)}:
            failures.append(f"catalog track_count mismatch for root: {root}")
        indices = sorted(entry[0] for entry in entries)
        if indices != list(range(1, len(entries) + 1)):
            failures.append(f"catalog track_index sequence mismatch for root: {root}")


def check_database(db: Path, catalog: Path | None, *, expect_audiobooks: bool) -> int:
    failures: list[str] = []
    conn = sqlite3.connect(db)
    try:
        integrity = denul(conn.execute("PRAGMA integrity_check").fetchone()[0])
        if integrity.lower() != "ok":
            failures.append(f"sqlite integrity_check failed: {integrity}")

        media_rows = load_audiobook_rows(conn, "MEDIA_TABLE", failures)
        media2_rows = load_audiobook_rows(conn, "MEDIA2_TABLE", failures)
        search_audiobooks = count_audiobook_paths(conn, "SEARCH_TABLE", failures)

        audiobook_albums = {row.album for row in media_rows if row.album}
        audiobook_genres = {row.genre for row in media_rows if row.genre}
        album_catalog = catalog_values(conn, "ALBUM_TABLE", "album", failures)
        album2_catalog = catalog_values(conn, "ALBUM2_TABLE", "album", failures)
        genre_catalog = catalog_values(conn, "GENRE_TABLE", "genre", failures)
        genre2_catalog = catalog_values(conn, "GENRE2_TABLE", "genre", failures)

        if expect_audiobooks and not media_rows:
            failures.append("MEDIA_TABLE has no audiobook rows")
        if len(media_rows) != len(media2_rows):
            failures.append(
                f"MEDIA_TABLE/MEDIA2_TABLE audiobook row count mismatch: {len(media_rows)} vs {len(media2_rows)}"
            )
        if search_audiobooks:
            failures.append(f"SEARCH_TABLE contains audiobook rows: {search_audiobooks}")

        leaked_albums = sorted(audiobook_albums & album_catalog)
        leaked_albums2 = sorted(audiobook_albums & album2_catalog)
        if leaked_albums:
            failures.append(f"ALBUM_TABLE leaks audiobook albums: {leaked_albums[:5]}")
        if leaked_albums2:
            failures.append(f"ALBUM2_TABLE leaks audiobook albums: {leaked_albums2[:5]}")
        if "Audiobook" in genre_catalog:
            failures.append("GENRE_TABLE contains Audiobook")
        if "Audiobook" in genre2_catalog:
            failures.append("GENRE2_TABLE contains Audiobook")
        bad_genres = sorted(genre for genre in audiobook_genres if genre != "Audiobook")
        if bad_genres:
            failures.append(f"audiobook media rows have non-Audiobook genres: {bad_genres[:5]}")

        ids = [row.media_id for row in media_rows]
        duplicate_ids = [item for item, count in Counter(ids).items() if count > 1]
        if duplicate_ids:
            failures.append(f"duplicate audiobook media ids: {duplicate_ids[:5]}")

        if catalog is not None:
            check_catalog(catalog, media_rows, failures)

        roots = {row.root for row in media_rows}
        print(f"database: {db}")
        print(f"integrity: {integrity}")
        print(f"audiobook media rows: {len(media_rows)}")
        print(f"audiobook media2 rows: {len(media2_rows)}")
        print(f"audiobook books: {len(roots)}")
        print(f"SEARCH_TABLE audiobook rows: {search_audiobooks}")
        print(f"ALBUM_TABLE audiobook leaks: {len(audiobook_albums & album_catalog)}")
        print(f"GENRE_TABLE has Audiobook: {'Audiobook' in genre_catalog}")
        if catalog is not None:
            print(f"catalog: {catalog}")
        if failures:
            print("\nFailures:")
            for failure in failures:
                print(f"- {failure}")
            return 1
        print("\nRelease-state check passed.")
        return 0
    finally:
        conn.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("db", type=Path, help="usrlocal_media.db to check")
    parser.add_argument("--catalog", type=Path, help="Optional audiobook resume catalog TSV")
    parser.add_argument("--expect-audiobooks", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.db.exists():
        print(f"DB not found: {args.db}", file=sys.stderr)
        return 2
    return check_database(args.db, args.catalog, expect_audiobooks=args.expect_audiobooks)


if __name__ == "__main__":
    raise SystemExit(main())
