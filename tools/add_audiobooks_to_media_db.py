#!/usr/bin/env python3
"""
Add /Audiobooks files to a copied HiBy R1 media database as real media rows.

This tool is for offline database experiments. It never edits the input database
unless --in-place is supplied. It can scan a local SD-card folder, a saved
manifest, or the connected R1 over ADB.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sqlite3
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


AUDIO_EXT = {
    ".aac",
    ".aif",
    ".ape",
    ".dff",
    ".dsf",
    ".flac",
    ".iso",
    ".m4a",
    ".m4b",
    ".mp2",
    ".mp3",
    ".oga",
    ".ogg",
    ".opus",
    ".wav",
    ".wma",
}

FORMAT_MAP = {
    ".aac": 255,
    ".aif": 1,
    ".ape": 21574,
    ".dff": 54736,
    ".dsf": 54736,
    ".flac": 61868,
    ".iso": 0,
    ".m4a": 255,
    ".m4b": 255,
    ".mp2": 80,
    ".mp3": 85,
    ".oga": 26447,
    ".ogg": 26447,
    ".opus": 28503,
    ".wav": 1,
    ".wma": 353,
}

FORMAT_NAME = {
    ".aac": "AAC",
    ".aif": "AIF",
    ".ape": "APE",
    ".dff": "DFF",
    ".dsf": "DSF",
    ".flac": "FLAC",
    ".iso": "ISO",
    ".m4a": "M4A",
    ".m4b": "M4B",
    ".mp2": "MP2",
    ".mp3": "MP3",
    ".oga": "OGA",
    ".ogg": "OGG",
    ".opus": "OPUS",
    ".wav": "WAV",
    ".wma": "WMA",
}

LOSSY_EXT = {".aac", ".m4a", ".m4b", ".mp2", ".mp3", ".oga", ".ogg", ".opus", ".wma"}
PREFIX = "a:\\Audiobooks\\"
DEVICE_AUDIOBOOKS_ROOT = "/usr/data/mnt/sd_0/Audiobooks"
COVER_NAMES = (
    "cover.jpg",
    "folder.jpg",
    "front.jpg",
    "albumart.jpg",
    "cover.jpeg",
    "folder.jpeg",
    "front.jpeg",
    "albumart.jpeg",
    "cover.png",
    "folder.png",
    "front.png",
    "albumart.png",
)
DEFAULT_FFPROBE_PATHS = (
    r"C:\Program Files\OpenAudible\bin\win_x86_64\ffprobe.exe",
    r"C:\Users\yetis\Downloads\ffmpeg-8.1.1-essentials_build\ffmpeg-8.1.1-essentials_build\bin\ffprobe.exe",
)

ARTICLES_RE = re.compile(r"^(the|der|die|das|les|il|lo|la|le|el)\s+", re.IGNORECASE)
PUNCT_CHARS = "(.\"'"
YEAR_PREFIX_RE = re.compile(r"^\s*((?:19|20)\d{2})\s*[-_. ]+\s*(.+)$")
YEAR_ANY_RE = re.compile(r"(?:19|20)\d{2}")
BRACKET_SUFFIX_RE = re.compile(r"\s+\[[^\]]+\]\s*$")
PAREN_YEAR_RE = re.compile(r"\s+\((?:19|20)\d{2}\)\s*$")
TRACK_PREFIX_RE = re.compile(r"^\s*\d{1,4}\s*[-_.]\s*")


@dataclass(frozen=True)
class AudioFile:
    hiby_path: str
    size: int = 0
    ctime: int = 0
    mtime: int = 0
    local_path: str = ""


@dataclass(frozen=True)
class AudioTags:
    title: str = ""
    album: str = ""
    artist: str = ""
    album_artist: str = ""
    composer: str = ""
    genre: str = ""
    year: int = 0
    track: int = 0


@dataclass(frozen=True)
class MediaRow:
    id: int
    path: str
    name: str
    album: str
    artist: str
    genre: str
    year: int
    dis_id: int
    ck_id: int
    size: int
    sample_rate: int
    bit_rate: int
    bit: int
    channel: int
    format_code: int
    quality: str
    ctime: int
    mtime: int
    album_pic_path: str
    lrc_path: str
    album_artist: str


def nul(value: object) -> str:
    text = "" if value is None else str(value)
    return text.rstrip("\x00") + "\x00"


def denul(value: object) -> str:
    return "" if value is None else str(value).rstrip("\x00")


def normalize_for_sort(text: str) -> str:
    stripped = denul(text).strip()
    while stripped and (stripped[0].isspace() or stripped[0] in PUNCT_CHARS):
        stripped = stripped[1:].lstrip()
    return ARTICLES_RE.sub("", stripped)


def ascii_upper(text: str) -> str:
    return text.translate(str.maketrans("abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"))


def sort_character(text: str) -> str:
    normalized = normalize_for_sort(denul(text))
    return normalized[:1].upper() if normalized else "#"


def pinyin(text: str) -> str:
    return nul(ascii_upper(normalize_for_sort(denul(text))))


def natural_key(text: str) -> tuple[object, ...]:
    parts: list[object] = []
    for part in re.split(r"(\d+)", text.lower()):
        if part.isdigit():
            parts.append(int(part))
        elif part:
            parts.append(part)
    return tuple(parts)


def sort_key(text: str) -> tuple[int, str]:
    normalized = normalize_for_sort(denul(text))
    if not normalized:
        return (0, denul(text).lower())
    ch = normalized[0]
    cp = ord(ch)
    if 0x2000 <= cp <= 0x2FFF:
        tier = 0
    elif "0" <= ch <= "9":
        tier = 1
    elif "A" <= ch <= "Z" or "a" <= ch <= "z" or cp >= 0xC0:
        tier = 2
    else:
        tier = 0
    return (tier, normalized.lower())


def table_exists(conn: sqlite3.Connection, table: str) -> bool:
    return conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (table,),
    ).fetchone() is not None


def table_columns(conn: sqlite3.Connection, table: str) -> list[str]:
    return [row[1] for row in conn.execute(f"PRAGMA table_info({table})")]


def quote_adb_shell(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def adb_find(adb: str, device_root: str, include_sizes: bool) -> list[AudioFile]:
    name_tests = []
    for ext in sorted(AUDIO_EXT):
        name_tests.extend(["-iname", f"*{ext}"])
        name_tests.append("-o")
    name_tests.pop()
    find_cmd = "find {root} -type f \\( {tests} \\)".format(
        root=quote_adb_shell(device_root),
        tests=" ".join(quote_adb_shell(part) if part.startswith("*") else part for part in name_tests),
    )
    result = subprocess.run(
        [adb, "shell", find_cmd],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    files: list[AudioFile] = []
    for line in result.stdout.splitlines():
        device_path = line.strip().rstrip("\r")
        if not device_path:
            continue
        size = adb_size(adb, device_path) if include_sizes else 0
        files.append(AudioFile(device_to_hiby_path(device_path, device_root), size=size))
    return files


def adb_size(adb: str, device_path: str) -> int:
    result = subprocess.run(
        [adb, "shell", f"wc -c < {quote_adb_shell(device_path)}"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    text = result.stdout.strip().splitlines()[0].strip().rstrip("\r")
    return int(text) if text.isdigit() else 0


def local_find(path: Path) -> list[AudioFile]:
    root = path.resolve()
    if root.name.lower() == "audiobooks":
        sd_root = root.parent
    else:
        sd_root = root
        root = sd_root / "Audiobooks"
    files: list[AudioFile] = []
    for item in root.rglob("*"):
        if not item.is_file() or item.name.startswith("."):
            continue
        if item.suffix.lower() not in AUDIO_EXT:
            continue
        stat = item.stat()
        rel = item.relative_to(sd_root)
        hiby = "a:\\" + "\\".join(rel.parts)
        files.append(
            AudioFile(hiby, stat.st_size, int(stat.st_ctime), int(stat.st_mtime), str(item))
        )
    return files


def manifest_find(path: Path) -> list[AudioFile]:
    files: list[AudioFile] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip().rstrip("\r")
        if not line or line.startswith("#"):
            continue
        size = 0
        ctime = 0
        mtime = 0
        value = line
        pieces = line.split("|", 3)
        if len(pieces) == 4 and pieces[0].isdigit():
            size = int(pieces[0])
            ctime = int(pieces[1] or 0)
            mtime = int(pieces[2] or 0)
            value = pieces[3]
        elif len(pieces) == 2 and pieces[0].isdigit():
            size = int(pieces[0])
            value = pieces[1]
        hiby = value if value.lower().startswith("a:\\") else device_to_hiby_path(value, DEVICE_AUDIOBOOKS_ROOT)
        files.append(AudioFile(hiby, size, ctime, mtime))
    return files


def device_to_hiby_path(device_path: str, device_root: str) -> str:
    normalized = device_path.replace("\\", "/").rstrip("\r")
    root = device_root.rstrip("/")
    if normalized.lower().startswith(root.lower() + "/"):
        rel = "Audiobooks/" + normalized[len(root) + 1 :]
    elif "/Audiobooks/" in normalized:
        rel = "Audiobooks/" + normalized.split("/Audiobooks/", 1)[1]
    else:
        rel = normalized.lstrip("/")
    return "a:\\" + rel.replace("/", "\\")


def hiby_sibling_path(hiby_path: str, filename: str) -> str:
    directory = hiby_path.rsplit("\\", 1)[0]
    return f"{directory}\\{filename}" if directory else filename


def find_cover_path(local_path: str, hiby_path: str) -> str:
    if not local_path:
        return ""
    directory = Path(local_path).parent
    for name in COVER_NAMES:
        if (directory / name).is_file():
            return hiby_sibling_path(hiby_path, name)
    return ""


def find_lrc_path(local_path: str, hiby_path: str) -> str:
    if not local_path:
        return ""
    lrc = Path(local_path).with_suffix(".lrc")
    return hiby_sibling_path(hiby_path, lrc.name) if lrc.is_file() else ""


def clean_book_folder(folder: str) -> tuple[str, int]:
    text = folder.strip()
    year = 0
    match = YEAR_PREFIX_RE.match(text)
    if match:
        year = int(match.group(1))
        text = match.group(2)
    else:
        year_match = YEAR_ANY_RE.search(text)
        if year_match:
            year = int(year_match.group(0))
    text = BRACKET_SUFFIX_RE.sub("", text)
    text = PAREN_YEAR_RE.sub("", text)
    return text.strip() or folder.strip(), year


def clean_track_name(stem: str) -> str:
    text = TRACK_PREFIX_RE.sub("", stem)
    text = PAREN_YEAR_RE.sub("", text)
    return text.strip() or stem


def load_book_names(conn: sqlite3.Connection) -> dict[str, str]:
    if not table_exists(conn, "BOOK_TABLE"):
        return {}
    mapping: dict[str, str] = {}
    for path, name in conn.execute("SELECT path, name FROM BOOK_TABLE"):
        clean_path = denul(path)
        clean_name = denul(name)
        if not clean_path or not clean_name:
            continue
        directory = clean_path.rsplit("\\", 1)[0].lower()
        mapping[directory] = clean_name
    return mapping


def group_files(files: list[AudioFile]) -> dict[str, list[AudioFile]]:
    groups: dict[str, list[AudioFile]] = {}
    for item in files:
        path = denul(item.hiby_path)
        ext = Path(path).suffix.lower()
        if not path.lower().startswith(PREFIX.lower()) or ext not in AUDIO_EXT:
            continue
        directory = path.rsplit("\\", 1)[0]
        groups.setdefault(directory, []).append(item)
    for rows in groups.values():
        rows.sort(key=lambda row: natural_key(denul(row.hiby_path)))
    return dict(sorted(groups.items(), key=lambda pair: natural_key(pair[0])))


def metadata_defaults(ext: str) -> tuple[int, int, int, int, str]:
    if ext in {".flac", ".wav", ".aif"}:
        return (44100, 0, 16, 2, "2")
    if ext in {".dff", ".dsf", ".iso"}:
        return (0, 0, 16, 2, "3")
    if ext in LOSSY_EXT:
        return (44100, 64000, 16, 2, "1")
    return (0, 0, 16, 2, "0")


def find_ffprobe(explicit: Path | None) -> str | None:
    if explicit:
        return str(explicit)
    found = shutil.which("ffprobe")
    if found:
        return found
    for candidate in DEFAULT_FFPROBE_PATHS:
        if Path(candidate).exists():
            return candidate
    return None


def first_tag(tags: dict[str, object], *names: str) -> str:
    for name in names:
        value = tags.get(name.lower())
        if value is None:
            continue
        text = str(value).strip().rstrip("\x00")
        if text:
            return text
    return ""


def parse_year(text: str) -> int:
    match = YEAR_ANY_RE.search(text or "")
    return int(match.group(0)) if match else 0


def parse_track(text: str) -> int:
    if not text:
        return 0
    first = text.split("/", 1)[0].strip()
    return int(first) if first.isdigit() else 0


def read_audio_tags(ffprobe: str, path: str) -> AudioTags:
    if not path:
        return AudioTags()
    result = subprocess.run(
        [ffprobe, "-v", "quiet", "-print_format", "json", "-show_format", path],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0 or not result.stdout.strip():
        return AudioTags()
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        return AudioTags()
    raw_tags = payload.get("format", {}).get("tags", {})
    if not isinstance(raw_tags, dict):
        return AudioTags()
    tags = {str(key).lower(): value for key, value in raw_tags.items()}

    year_text = first_tag(tags, "releasedate", "release_time", "tdrl", "date", "tyer", "year")
    return AudioTags(
        title=first_tag(tags, "title", "tit2"),
        album=first_tag(tags, "album", "talb"),
        artist=first_tag(tags, "artist", "tpe1"),
        album_artist=first_tag(
            tags, "album_artist", "albumartist", "album artist", "tpe2", "author"
        ),
        composer=first_tag(tags, "composer", "tcom"),
        genre=first_tag(tags, "genre", "tcon"),
        year=parse_year(year_text),
        track=parse_track(first_tag(tags, "track", "trck", "tracknumber")),
    )


def choose_first(values: list[str]) -> str:
    for value in values:
        text = value.strip()
        if text:
            return text
    return ""


def build_rows(
    conn: sqlite3.Connection,
    files: list[AudioFile],
    id_base: int | None,
    ffprobe: str | None,
) -> list[MediaRow]:
    now = int(time.time())
    book_names = load_book_names(conn)
    groups = group_files(files)
    max_id = conn.execute("SELECT COALESCE(MAX(id), 0) FROM MEDIA_TABLE").fetchone()[0]
    next_id = id_base if id_base is not None else max_id + 1000
    rows: list[MediaRow] = []
    tags_by_path = {
        item.hiby_path: read_audio_tags(ffprobe, item.local_path)
        for item in files
        if ffprobe and item.local_path
    }

    for directory, group in groups.items():
        components = directory.split("\\")
        rel_components = components[2:] if len(components) >= 2 else components
        author = rel_components[0] if rel_components else "Unknown"
        folder = rel_components[-1] if rel_components else "Unknown"
        inferred_book, year = clean_book_folder(folder)
        group_tags = [tags_by_path.get(item.hiby_path, AudioTags()) for item in group]
        tag_album = choose_first([tag.album for tag in group_tags])
        tag_album_artist = choose_first([tag.album_artist for tag in group_tags])
        tag_artist = choose_first([tag.artist for tag in group_tags])
        tag_year = next((tag.year for tag in group_tags if tag.year), 0)
        album = tag_album or book_names.get(directory.lower(), inferred_book)
        author = tag_album_artist or author
        artist = tag_artist or author
        year = tag_year or year
        total = len(group)
        for index, item in enumerate(group, 1):
            tags = tags_by_path.get(item.hiby_path, AudioTags())
            path = denul(item.hiby_path)
            filename = path.rsplit("\\", 1)[-1]
            stem = filename.rsplit(".", 1)[0]
            ext = "." + filename.rsplit(".", 1)[-1].lower() if "." in filename else ""
            tag_title = tags.title.strip()
            if tag_title and not (total > 1 and tag_title == album):
                title = tag_title
            elif total > 1:
                width = max(2, len(str(total)))
                title = f"{album} {index:0{width}d}/{total:0{width}d}"
            else:
                title = album or clean_track_name(stem)
            track_number = tags.track or index
            sample_rate, bit_rate, bit_depth, channels, quality = metadata_defaults(ext)
            timestamp = item.mtime or item.ctime or now
            album_pic_path = find_cover_path(item.local_path, path)
            lrc_path = find_lrc_path(item.local_path, path)
            rows.append(
                MediaRow(
                    id=next_id,
                    path=path,
                    name=title,
                    album=album or "Unknown",
                    artist=(tags.artist or artist or "Unknown"),
                    genre="Audiobook",
                    year=year,
                    dis_id=0,
                    ck_id=track_number,
                    size=item.size,
                    sample_rate=sample_rate,
                    bit_rate=bit_rate,
                    bit=bit_depth,
                    channel=channels,
                    format_code=FORMAT_MAP.get(ext, 0),
                    quality=quality,
                    ctime=timestamp,
                    mtime=timestamp,
                    album_pic_path=album_pic_path,
                    lrc_path=lrc_path,
                    album_artist=(tags.album_artist or author or "Unknown"),
                )
            )
            next_id += 1
    return rows


MEDIA_COLUMNS = (
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


def row_tuple(row: MediaRow, *, index_by_album: bool = False) -> tuple[object, ...]:
    index_text = row.album if index_by_album and row.album else row.name
    return (
        row.id,
        nul(row.path),
        nul(row.name),
        nul(row.album),
        nul(row.artist),
        nul(row.genre),
        row.year,
        row.dis_id,
        row.ck_id,
        0,
        0,
        -1,
        -1,
        sort_character(index_text),
        row.size,
        row.sample_rate,
        row.bit_rate,
        row.bit,
        row.channel,
        row.format_code,
        nul(row.quality),
        nul(row.album_pic_path),
        nul(row.lrc_path),
        0.0,
        0.0,
        row.ctime,
        row.mtime,
        pinyin(index_text),
        nul(row.album_artist),
    )


def require_media_schema(conn: sqlite3.Connection, table: str) -> None:
    cols = table_columns(conn, table)
    missing = [col for col in MEDIA_COLUMNS if col not in cols]
    if missing:
        raise SystemExit(f"{table} is missing expected columns: {', '.join(missing)}")


def delete_existing_audiobooks(conn: sqlite3.Connection) -> dict[str, int]:
    removed: dict[str, int] = {}
    for table in ("MEDIA_TABLE", "MEDIA2_TABLE", "MEDIA3_TABLE", "SEARCH_TABLE"):
        if not table_exists(conn, table) or "path" not in table_columns(conn, table):
            continue
        cur = conn.execute(
            f"DELETE FROM {table} WHERE path LIKE ? COLLATE NOCASE",
            (PREFIX + "%",),
        )
        removed[table] = cur.rowcount if cur.rowcount is not None else 0
    return removed


def media_where_clause(catalog_excludes_audiobooks: bool) -> str:
    if not catalog_excludes_audiobooks:
        return ""
    return f" WHERE path NOT LIKE '{PREFIX}%' COLLATE NOCASE"


def insert_rows(
    conn: sqlite3.Connection,
    rows: list[MediaRow],
    *,
    catalog_excludes_audiobooks: bool,
) -> None:
    placeholders = ",".join("?" for _ in MEDIA_COLUMNS)
    columns = ",".join(MEDIA_COLUMNS)
    sql_media = f"INSERT INTO MEDIA_TABLE ({columns}) VALUES ({placeholders})"
    sql_media2 = f"INSERT INTO MEDIA2_TABLE ({columns}) VALUES ({placeholders})"
    tuples_by_title = [row_tuple(row) for row in sorted(rows, key=lambda row: sort_key(row.name))]
    tuples_by_album = [
        row_tuple(row, index_by_album=True)
        for row in sorted(rows, key=lambda row: (sort_key(row.album), row.dis_id, row.ck_id, natural_key(row.path)))
    ]
    conn.executemany(sql_media, tuples_by_title)
    conn.executemany(sql_media2, tuples_by_album)
    if table_exists(conn, "SEARCH_TABLE"):
        cols = ",".join(MEDIA_COLUMNS)
        conn.execute("DELETE FROM SEARCH_TABLE")
        conn.execute(
            f"INSERT INTO SEARCH_TABLE ({cols}) SELECT {cols} FROM MEDIA_TABLE"
            f"{media_where_clause(catalog_excludes_audiobooks)}"
        )


def refresh_catalog(conn: sqlite3.Connection, *, catalog_excludes_audiobooks: bool) -> None:
    refresh_named_catalog(conn, "ARTIST_TABLE", "artist", False, catalog_excludes_audiobooks)
    refresh_named_catalog(conn, "ARTIST2_TABLE", "artist", False, catalog_excludes_audiobooks)
    refresh_named_catalog(conn, "ALBUM_TABLE", "album", True, catalog_excludes_audiobooks)
    refresh_named_catalog(conn, "ALBUM2_TABLE", "album", True, catalog_excludes_audiobooks)
    refresh_named_catalog(conn, "GENRE_TABLE", "genre", False, catalog_excludes_audiobooks)
    refresh_named_catalog(conn, "GENRE2_TABLE", "genre", False, catalog_excludes_audiobooks)
    refresh_named_catalog(
        conn, "ALBUM_ARTIST_TABLE", "album_artist", True, catalog_excludes_audiobooks
    )
    refresh_named_catalog(
        conn, "ALBUM_ARTIST2_TABLE", "album_artist", True, catalog_excludes_audiobooks
    )
    refresh_format_tables(conn, catalog_excludes_audiobooks)
    refresh_count_table(conn, catalog_excludes_audiobooks)
    refresh_time_tables(conn, catalog_excludes_audiobooks)


def refresh_named_catalog(
    conn: sqlite3.Connection,
    table: str,
    column: str,
    has_mqa: bool,
    catalog_excludes_audiobooks: bool,
) -> None:
    if not table_exists(conn, table):
        return
    conn.execute(f"DELETE FROM {table}")
    rows = []
    for value, count, first_id, ctime, mtime in conn.execute(
        f"""
        SELECT {column}, COUNT(*), MIN(id), MIN(COALESCE(ctime, 0)), MAX(COALESCE(mtime, 0))
          FROM MEDIA_TABLE
         {media_where_clause(catalog_excludes_audiobooks)}
         GROUP BY {column}
        """
    ):
        text = denul(value) or "Unknown"
        if has_mqa:
            rows.append((first_id, nul(text), sort_character(text), count, ctime, mtime, 0, pinyin(text)))
        else:
            rows.append((first_id, nul(text), sort_character(text), count, ctime, mtime, pinyin(text)))
    rows.sort(key=lambda row: sort_key(row[1]))
    if has_mqa:
        conn.executemany(
            f"INSERT INTO {table} (id, {column}, character, cn, ctime, mtime, mqa, pinyin_charater) "
            "VALUES (?,?,?,?,?,?,?,?)",
            rows,
        )
    else:
        conn.executemany(
            f"INSERT INTO {table} (id, {column}, character, cn, ctime, mtime, pinyin_charater) "
            "VALUES (?,?,?,?,?,?,?)",
            rows,
        )


def refresh_format_tables(conn: sqlite3.Connection, catalog_excludes_audiobooks: bool) -> None:
    values: dict[str, tuple[int, int]] = {}
    for media_id, path in conn.execute(
        f"SELECT id, path FROM MEDIA_TABLE{media_where_clause(catalog_excludes_audiobooks)}"
    ):
        ext = "." + denul(path).rsplit(".", 1)[-1].lower() if "." in denul(path) else ""
        name = FORMAT_NAME.get(ext, ext.lstrip(".").upper() or "UNKNOWN")
        first_id, count = values.get(name, (media_id, 0))
        values[name] = (min(first_id, media_id), count + 1)
    rows = [(first_id, name, sort_character(name), count) for name, (first_id, count) in values.items()]
    rows.sort(key=lambda row: sort_key(row[1]))
    for table in ("FORMAT_TABLE", "FORMAT2_TABLE"):
        if table_exists(conn, table):
            conn.execute(f"DELETE FROM {table}")
            conn.executemany(
                f"INSERT INTO {table} (id, format, character, cn) VALUES (?,?,?,?)",
                rows,
            )


def refresh_count_table(conn: sqlite3.Connection, catalog_excludes_audiobooks: bool) -> None:
    if not table_exists(conn, "COUNT_TABLE"):
        return
    where = media_where_clause(catalog_excludes_audiobooks)
    counts = [
        conn.execute(f"SELECT COUNT(*) FROM MEDIA_TABLE{where}").fetchone()[0],
        conn.execute(f"SELECT COUNT(DISTINCT album) FROM MEDIA_TABLE{where}").fetchone()[0],
        conn.execute(f"SELECT COUNT(DISTINCT artist) FROM MEDIA_TABLE{where}").fetchone()[0],
        conn.execute(f"SELECT COUNT(DISTINCT genre) FROM MEDIA_TABLE{where}").fetchone()[0],
        conn.execute(f"SELECT COUNT(DISTINCT album_artist) FROM MEDIA_TABLE{where}").fetchone()[0],
    ]
    conn.execute("DELETE FROM COUNT_TABLE")
    conn.executemany("INSERT INTO COUNT_TABLE (cn) VALUES (?)", [(count,) for count in counts])


def refresh_time_tables(conn: sqlite3.Connection, catalog_excludes_audiobooks: bool) -> None:
    where = media_where_clause(catalog_excludes_audiobooks)
    if table_exists(conn, "CTIME_TABLE"):
        conn.execute("DELETE FROM CTIME_TABLE")
        conn.execute(
            "INSERT INTO CTIME_TABLE (media_id) "
            f"SELECT id FROM MEDIA_TABLE{where} ORDER BY COALESCE(ctime, 0) ASC, id ASC"
        )
    if table_exists(conn, "MTIME_TABLE"):
        conn.execute("DELETE FROM MTIME_TABLE")
        conn.execute(
            "INSERT INTO MTIME_TABLE (media_id) "
            f"SELECT id FROM MEDIA_TABLE{where} ORDER BY COALESCE(mtime, 0) DESC, id ASC"
        )


def write_db(
    input_db: Path,
    output_db: Path,
    files: list[AudioFile],
    in_place: bool,
    id_base: int | None,
    ffprobe: str | None,
    catalog_excludes_audiobooks: bool,
) -> None:
    target = input_db if in_place else output_db
    if not in_place:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(input_db, target)

    conn = sqlite3.connect(target)
    try:
        require_media_schema(conn, "MEDIA_TABLE")
        require_media_schema(conn, "MEDIA2_TABLE")
        conn.execute("PRAGMA foreign_keys = OFF")
        conn.execute("BEGIN")
        rows = build_rows(conn, files, id_base, ffprobe)
        removed = delete_existing_audiobooks(conn)
        insert_rows(conn, rows, catalog_excludes_audiobooks=catalog_excludes_audiobooks)
        refresh_catalog(conn, catalog_excludes_audiobooks=catalog_excludes_audiobooks)
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()

    print(f"database: {target}")
    print(f"audio files: {len(files)}")
    print(f"tag reader: {'none' if not ffprobe else ffprobe}")
    print(f"music catalog excludes audiobooks: {catalog_excludes_audiobooks}")
    print(f"media rows added: {len(rows)}")
    print("removed existing audiobook rows:")
    for table, count in removed.items():
        print(f"  {table}: {count}")
    if rows:
        print(f"first id: {rows[0].id}")
        print(f"last id:  {rows[-1].id}")
        albums = sorted({row.album for row in rows})
        print(f"books:    {len(albums)}")
        for album in albums[:20]:
            print(f"  {album}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("db", type=Path, help="Input copied usrlocal_media.db")
    parser.add_argument("-o", "--output", type=Path, default=Path("work/usrlocal_media.with-audiobooks.db"))
    parser.add_argument("--in-place", action="store_true", help="Edit the input DB directly")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--local-sd", type=Path, help="Local SD root or Audiobooks folder")
    source.add_argument("--manifest", type=Path, help="File containing ADB/device paths or size|path lines")
    source.add_argument("--adb-scan", action="store_true", help="Scan connected R1 over ADB")
    parser.add_argument("--adb", default=r"C:\Program Files\Software Fix\adb.exe", help="ADB executable")
    parser.add_argument("--adb-sizes", action="store_true", help="Also read byte sizes over ADB. Slower.")
    parser.add_argument("--device-root", default=DEVICE_AUDIOBOOKS_ROOT)
    parser.add_argument("--id-base", type=int, help="First media ID for generated audiobook rows")
    parser.add_argument(
        "--music-catalog-excludes-audiobooks",
        action="store_true",
        help=(
            "Keep audiobook rows in MEDIA_TABLE for playback, but rebuild Music catalog/search/count "
            "tables from non-audiobook rows only."
        ),
    )
    parser.add_argument(
        "--read-tags",
        action="store_true",
        help="Read local audio tags with ffprobe when local paths are available.",
    )
    parser.add_argument(
        "--ffprobe",
        type=Path,
        help="Path to ffprobe.exe. If omitted, PATH and known OpenAudible/ffmpeg locations are checked.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.db.exists():
        raise SystemExit(f"DB not found: {args.db}")
    if args.local_sd:
        files = local_find(args.local_sd)
    elif args.manifest:
        files = manifest_find(args.manifest)
    else:
        files = adb_find(args.adb, args.device_root, args.adb_sizes)
    if not files:
        raise SystemExit("No audiobook audio files found")
    ffprobe = find_ffprobe(args.ffprobe) if args.read_tags else None
    if args.read_tags and not ffprobe:
        raise SystemExit("Could not find ffprobe. Pass --ffprobe or install ffprobe in PATH.")
    write_db(
        args.db,
        args.output,
        files,
        args.in_place,
        args.id_base,
        ffprobe,
        args.music_catalog_excludes_audiobooks,
    )


if __name__ == "__main__":
    main()
