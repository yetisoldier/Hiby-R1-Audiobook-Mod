#!/usr/bin/env python3
"""Validate the NativeApp SD library database used by release verification."""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("database", type=Path)
    args = parser.parse_args()

    if not args.database.is_file():
        parser.error(f"database not found: {args.database}")

    connection = sqlite3.connect(f"file:{args.database}?mode=ro", uri=True)
    try:
        integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
        tables = {
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            )
        }
        missing = {"books", "tracks"} - tables
        if integrity != "ok":
            raise RuntimeError(f"integrity_check returned {integrity!r}")
        if missing:
            raise RuntimeError(f"missing tables: {', '.join(sorted(missing))}")

        books = connection.execute("SELECT COUNT(*) FROM books").fetchone()[0]
        tracks = connection.execute("SELECT COUNT(*) FROM tracks").fetchone()[0]
        if books < 1 or tracks < 1:
            raise RuntimeError(f"empty catalog: books={books} tracks={tracks}")
    finally:
        connection.close()

    print(f"integrity={integrity} books={books} tracks={tracks}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
