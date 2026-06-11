#!/usr/bin/env python3
"""
Write a small TSV catalog for the on-device audiobook resume daemon.

The R1 does not ship sqlite3, so the daemon cannot cheaply ask the media
database which file is track 17 of a book. This helper exports the useful
parts from a copied usrlocal_media.db on the PC and the installer pushes the
result to /usr/data/audiobooks/catalog.tsv.
"""

from __future__ import annotations

import argparse
import re
import sqlite3
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


PREFIX = "a:\\Audiobooks\\"


@dataclass(frozen=True)
class CatalogRow:
    media_id: int
    path: str
    title: str
    album: str
    author: str
    ck_id: int
    dis_id: int

    @property
    def root(self) -> str:
        return self.path.rsplit("\\", 1)[0]


def denul(value: object) -> str:
    return "" if value is None else str(value).rstrip("\x00")


def clean_field(value: object) -> str:
    return denul(value).replace("\t", " ").replace("\r", " ").replace("\n", " ")


def natural_key(text: str) -> tuple[object, ...]:
    parts: list[object] = []
    for part in re.split(r"(\d+)", text.lower()):
        if part.isdigit():
            parts.append(int(part))
        elif part:
            parts.append(part)
    return tuple(parts)


def stable_slug(text: str) -> str:
    value = re.sub(r"[^0-9a-z]+", "_", text.strip().lower()).strip("_")
    return value


def book_key_for_row(row: CatalogRow) -> str:
    author = stable_slug(row.author)
    album = stable_slug(row.album)
    if author and album:
        return f"v1_{author}_{album}"
    fallback = stable_slug(row.root) or "unknown"
    return f"root_{fallback}"


def load_rows(db: Path) -> list[CatalogRow]:
    conn = sqlite3.connect(db)
    try:
        rows = []
        for media_id, path, title, album, author, ck_id, dis_id in conn.execute(
            """
            SELECT id, path, name, album, album_artist, ck_id, dis_id
              FROM MEDIA_TABLE
             WHERE path LIKE ? COLLATE NOCASE
            """,
            (PREFIX + "%",),
        ):
            clean_path = clean_field(path)
            if not clean_path:
                continue
            rows.append(
                CatalogRow(
                    media_id=int(media_id),
                    path=clean_path,
                    title=clean_field(title),
                    album=clean_field(album),
                    author=clean_field(author),
                    ck_id=int(ck_id or 0),
                    dis_id=int(dis_id or 0),
                )
            )
        return rows
    finally:
        conn.close()


def write_catalog(rows: list[CatalogRow], output: Path) -> None:
    groups: dict[str, list[CatalogRow]] = defaultdict(list)
    for row in rows:
        groups[row.root].append(row)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(
            "root_hiby_path\ttrack_index\ttrack_count\tmedia_id\tpath\ttitle\talbum\tauthor\tbook_key\n"
        )
        for root in sorted(groups, key=natural_key):
            group = sorted(
                groups[root],
                key=lambda row: (
                    row.dis_id,
                    row.ck_id,
                    natural_key(row.path),
                    row.media_id,
                ),
            )
            track_count = len(group)
            for index, row in enumerate(group, 1):
                handle.write(
                    "\t".join(
                        [
                            row.root,
                            str(index),
                            str(track_count),
                            str(row.media_id),
                            row.path,
                            row.title,
                            row.album,
                            row.author,
                            book_key_for_row(row),
                        ]
                    )
                    + "\n"
                )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("db", type=Path, help="Copied usrlocal_media.db")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("work/audiobook-resume-catalog.tsv"),
        help="Output TSV path",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.db.exists():
        raise SystemExit(f"DB not found: {args.db}")
    rows = load_rows(args.db)
    if not rows:
        raise SystemExit("No audiobook media rows found")
    write_catalog(rows, args.output)
    books = {row.root for row in rows}
    print(f"catalog: {args.output}")
    print(f"books:   {len(books)}")
    print(f"tracks:  {len(rows)}")


if __name__ == "__main__":
    main()
