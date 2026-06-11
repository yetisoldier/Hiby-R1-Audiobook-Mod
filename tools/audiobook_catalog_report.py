#!/usr/bin/env python3
"""Summarize an R1 audiobook book-level catalog.

The firmware writes /usr/data/audiobooks/catalog-books.tsv with one row per
book. This helper turns that sidecar into a quick human-readable report for
author/title/series experiments and metadata QA.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REQUIRED_COLUMNS = {
    "root_hiby_path",
    "album",
    "author",
    "book_key",
    "series",
    "series_part",
    "track_count",
    "first_media_id",
}


@dataclass(frozen=True)
class Book:
    root_hiby_path: str
    title: str
    author: str
    book_key: str
    series: str
    series_part: str
    track_count: int
    first_media_id: int


def natural_key(text: str) -> list[object]:
    return [int(part) if part.isdigit() else part.casefold() for part in re.split(r"(\d+)", text)]


def parse_int(text: str) -> int:
    try:
        return int(text)
    except (TypeError, ValueError):
        return 0


def read_books(path: Path) -> list[Book]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        missing = sorted(REQUIRED_COLUMNS.difference(reader.fieldnames or []))
        if missing:
            raise SystemExit(f"{path} is missing required columns: {', '.join(missing)}")
        books = [
            Book(
                root_hiby_path=(row.get("root_hiby_path") or "").strip(),
                title=(row.get("album") or "").strip() or "Unknown",
                author=(row.get("author") or "").strip() or "Unknown",
                book_key=(row.get("book_key") or "").strip(),
                series=(row.get("series") or "").strip(),
                series_part=(row.get("series_part") or "").strip(),
                track_count=parse_int(row.get("track_count") or ""),
                first_media_id=parse_int(row.get("first_media_id") or ""),
            )
            for row in reader
        ]
    return books


def sorted_books(books: Iterable[Book]) -> list[Book]:
    return sorted(books, key=lambda book: (natural_key(book.author), natural_key(book.title), book.first_media_id))


def series_sort_key(book: Book) -> tuple[list[object], list[object], int]:
    return (natural_key(book.series_part), natural_key(book.title), book.first_media_id)


def to_summary(books: list[Book]) -> dict[str, object]:
    authors: dict[str, list[Book]] = defaultdict(list)
    series: dict[str, list[Book]] = defaultdict(list)
    for book in books:
        authors[book.author].append(book)
        if book.series:
            series[book.series].append(book)
    return {
        "book_count": len(books),
        "author_count": len(authors),
        "series_count": len(series),
        "standalone_count": sum(1 for book in books if not book.series),
        "multipart_count": sum(1 for book in books if book.track_count > 1),
        "authors": {
            author: [book.title for book in sorted_books(items)]
            for author, items in sorted(authors.items(), key=lambda item: natural_key(item[0]))
        },
        "series": {
            name: [
                {
                    "title": book.title,
                    "part": book.series_part,
                    "author": book.author,
                }
                for book in sorted(items, key=series_sort_key)
            ]
            for name, items in sorted(series.items(), key=lambda item: natural_key(item[0]))
        },
        "titles": [book.title for book in sorted_books(books)],
    }


def print_text_report(books: list[Book], *, limit: int) -> None:
    summary = to_summary(books)
    print(f"Books:      {summary['book_count']}")
    print(f"Authors:    {summary['author_count']}")
    print(f"Series:     {summary['series_count']}")
    print(f"Standalone: {summary['standalone_count']}")
    print(f"Multipart:  {summary['multipart_count']}")
    print()

    authors: dict[str, list[Book]] = defaultdict(list)
    series: dict[str, list[Book]] = defaultdict(list)
    for book in books:
        authors[book.author].append(book)
        if book.series:
            series[book.series].append(book)

    print("Authors")
    for author, items in sorted(authors.items(), key=lambda item: natural_key(item[0]))[:limit]:
        print(f"- {author} ({len(items)})")
        for book in sorted_books(items)[:limit]:
            print(f"  - {book.title} [{book.track_count} track{'s' if book.track_count != 1 else ''}]")
    if not authors:
        print("- none")
    print()

    print("Series")
    if series:
        for name, items in sorted(series.items(), key=lambda item: natural_key(item[0]))[:limit]:
            print(f"- {name} ({len(items)})")
            for book in sorted(items, key=series_sort_key)[:limit]:
                part = f" #{book.series_part}" if book.series_part else ""
                print(f"  -{part} {book.title} - {book.author}")
    else:
        print("- none")
    print()

    print("Titles")
    for book in sorted_books(books)[:limit]:
        series_text = f" / {book.series} {book.series_part}".rstrip() if book.series else ""
        print(f"- {book.title} - {book.author}{series_text}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path, help="Path to catalog-books.tsv")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON")
    parser.add_argument("--limit", type=int, default=20, help="Maximum entries per section")
    args = parser.parse_args()

    books = read_books(args.catalog)
    if args.json:
        print(json.dumps(to_summary(books), indent=2, ensure_ascii=False))
    else:
        print_text_report(books, limit=max(args.limit, 1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
