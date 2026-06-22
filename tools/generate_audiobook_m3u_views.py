#!/usr/bin/env python3
"""Generate experimental audiobook M3U view folders from R1 catalog TSVs.

This is a host-side prototype for testing whether the R1's existing playlist
support can provide lower-risk audiobook Title / Author / Series views before
we attempt a deeper native list implementation.
"""

from __future__ import annotations

import argparse
import csv
import ntpath
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


DEFAULT_PLAYLIST_ROOT = "playlist_data\\R1 Audiobooks"
INVALID_FILENAME = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


@dataclass(frozen=True)
class Track:
    root_hiby_path: str
    track_index: int
    track_count: int
    media_id: int
    path: str
    title: str
    album: str
    author: str
    book_key: str
    series: str
    series_part: str


def clean_filename(value: str, *, fallback: str) -> str:
    value = INVALID_FILENAME.sub(" ", value).strip()
    value = re.sub(r"\s+", " ", value)
    value = value.rstrip(". ")
    return value or fallback


def unique_path(path: Path, seen: set[Path]) -> Path:
    if path not in seen and not path.exists():
        seen.add(path)
        return path
    stem = path.stem
    suffix = path.suffix
    for index in range(2, 10000):
        candidate = path.with_name(f"{stem} ({index}){suffix}")
        if candidate not in seen and not candidate.exists():
            seen.add(candidate)
            return candidate
    raise RuntimeError(f"could not make unique playlist path for {path}")


def read_tracks(path: Path) -> list[Track]:
    tracks: list[Track] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            tracks.append(
                Track(
                    root_hiby_path=row.get("root_hiby_path", ""),
                    track_index=int(row.get("track_index") or 0),
                    track_count=int(row.get("track_count") or 0),
                    media_id=int(row.get("media_id") or 0),
                    path=row.get("path", ""),
                    title=row.get("title", ""),
                    album=row.get("album", ""),
                    author=row.get("author", ""),
                    book_key=row.get("book_key", ""),
                    series=row.get("series", ""),
                    series_part=row.get("series_part", ""),
                )
            )
    tracks.sort(key=lambda t: (t.root_hiby_path.casefold(), t.track_index, t.media_id))
    return tracks


def hiby_relative_path(track_path: str, playlist_dir_hiby: str) -> str:
    if not track_path.lower().startswith("a:\\"):
        raise ValueError(f"expected HiBy SD path starting with a:\\, got {track_path!r}")
    target = track_path[3:]
    base = playlist_dir_hiby.replace("/", "\\").strip("\\")
    return ntpath.relpath(target, base)


def write_playlist(path: Path, tracks: list[Track], playlist_dir_hiby: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [hiby_relative_path(track.path, playlist_dir_hiby) for track in tracks]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def group_by_book(tracks: list[Track]) -> dict[str, list[Track]]:
    grouped: dict[str, list[Track]] = defaultdict(list)
    for track in tracks:
        grouped[track.root_hiby_path].append(track)
    return dict(grouped)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True, help="catalog.tsv from r1_audiobook_db_maint")
    parser.add_argument("--out-dir", type=Path, required=True, help="local output directory")
    parser.add_argument(
        "--playlist-root",
        default=DEFAULT_PLAYLIST_ROOT,
        help=r"HiBy SD-relative playlist root, e.g. playlist_data\R1 Audiobooks",
    )
    args = parser.parse_args()

    tracks = read_tracks(args.catalog)
    by_book = group_by_book(tracks)
    by_author: dict[str, list[Track]] = defaultdict(list)
    by_series: dict[str, list[Track]] = defaultdict(list)
    for book_tracks in by_book.values():
        first = book_tracks[0]
        author = first.author or "Unknown Author"
        by_author[author].extend(book_tracks)
        if first.series:
            by_series[first.series].extend(book_tracks)

    written: list[Path] = []
    seen: set[Path] = set()

    title_dir_hiby = f"{args.playlist_root}\\Titles"
    for book_tracks in by_book.values():
        first = book_tracks[0]
        label = f"{first.author} - {first.album}" if first.author else first.album
        name = clean_filename(label, fallback=first.book_key or "Audiobook")
        path = unique_path(args.out_dir / "Titles" / f"{name}.m3u", seen)
        write_playlist(path, book_tracks, title_dir_hiby)
        written.append(path)

    author_dir_hiby = f"{args.playlist_root}\\Authors"
    for author, author_tracks in sorted(by_author.items(), key=lambda item: item[0].casefold()):
        name = clean_filename(author, fallback="Unknown Author")
        path = unique_path(args.out_dir / "Authors" / f"{name}.m3u", seen)
        author_tracks.sort(key=lambda t: (t.root_hiby_path.casefold(), t.track_index, t.media_id))
        write_playlist(path, author_tracks, author_dir_hiby)
        written.append(path)

    series_dir_hiby = f"{args.playlist_root}\\Series"
    for series, series_tracks in sorted(by_series.items(), key=lambda item: item[0].casefold()):
        name = clean_filename(series, fallback="Series")
        path = unique_path(args.out_dir / "Series" / f"{name}.m3u", seen)
        series_tracks.sort(
            key=lambda t: (
                t.series.casefold(),
                int(t.series_part) if t.series_part.isdigit() else 999999,
                t.album.casefold(),
                t.track_index,
                t.media_id,
            )
        )
        write_playlist(path, series_tracks, series_dir_hiby)
        written.append(path)

    print(f"tracks: {len(tracks)}")
    print(f"books: {len(by_book)}")
    print(f"authors: {len(by_author)}")
    print(f"series: {len(by_series)}")
    print(f"playlists: {len(written)}")
    print(f"output: {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
