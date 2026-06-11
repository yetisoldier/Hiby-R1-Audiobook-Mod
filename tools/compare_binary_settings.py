#!/usr/bin/env python3
"""Summarize byte-level differences between two small R1 settings files."""

from __future__ import annotations

import argparse
from pathlib import Path


def byte_at(data: bytes, index: int) -> int | None:
    if index >= len(data):
        return None
    return data[index]


def changed_ranges(before: bytes, after: bytes, *, merge_gap: int) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    length = max(len(before), len(after))
    index = 0
    while index < length:
        if byte_at(before, index) == byte_at(after, index):
            index += 1
            continue

        start = index
        while index < length and byte_at(before, index) != byte_at(after, index):
            index += 1
        end = index

        if ranges and start - ranges[-1][1] <= merge_gap:
            ranges[-1] = (ranges[-1][0], end)
        else:
            ranges.append((start, end))
    return ranges


def ascii_preview(chunk: bytes) -> str:
    chars = []
    for value in chunk:
        if 32 <= value <= 126:
            chars.append(chr(value))
        else:
            chars.append(".")
    return "".join(chars)


def hexdump(data: bytes, start: int, end: int) -> list[str]:
    lines = []
    for offset in range(start, end, 16):
        chunk = data[offset : min(offset + 16, end)]
        hex_part = " ".join(f"{value:02x}" for value in chunk)
        lines.append(f"0x{offset:08x}  {hex_part:<47}  {ascii_preview(chunk)}")
    return lines


def collect_ascii_strings(data: bytes, start: int, end: int, min_len: int) -> list[str]:
    found: list[str] = []
    current: list[int] = []
    for value in data[start:end]:
        if 32 <= value <= 126:
            current.append(value)
        else:
            if len(current) >= min_len:
                found.append(bytes(current).decode("ascii", "replace"))
            current = []
    if len(current) >= min_len:
        found.append(bytes(current).decode("ascii", "replace"))
    return found


def collect_utf16le_strings(data: bytes, start: int, end: int, min_len: int) -> list[str]:
    found: list[str] = []
    current: list[int] = []
    offset = start if start % 2 == 0 else start + 1
    while offset + 1 < end:
        low = data[offset]
        high = data[offset + 1]
        if high == 0 and 32 <= low <= 126:
            current.append(low)
        else:
            if len(current) >= min_len:
                found.append(bytes(current).decode("ascii", "replace"))
            current = []
        offset += 2
    if len(current) >= min_len:
        found.append(bytes(current).decode("ascii", "replace"))
    return found


def safe_string(value: str) -> str:
    return value.encode("unicode_escape", "backslashreplace").decode("ascii")


def nearby_strings(data: bytes, start: int, end: int, *, min_len: int) -> list[str]:
    strings: list[str] = []
    for prefix, collector in (
        ("ascii", collect_ascii_strings),
        ("utf16le", collect_utf16le_strings),
    ):
        for value in collector(data, start, end, min_len):
            safe = safe_string(value)
            if safe not in strings:
                strings.append(f"{prefix}: {safe}")
    return strings


def compare(before_path: Path, after_path: Path, *, context: int, limit: int, merge_gap: int) -> int:
    before = before_path.read_bytes()
    after = after_path.read_bytes()
    ranges = changed_ranges(before, after, merge_gap=merge_gap)

    print(f"before={before_path}")
    print(f"after={after_path}")
    print(f"size={len(before)}->{len(after)}")
    print(f"changed_ranges={len(ranges)}")
    if not ranges:
        return 0

    for ordinal, (start, end) in enumerate(ranges[:limit], start=1):
        window_start = max(0, start - context)
        window_end = min(max(len(before), len(after)), end + context)
        before_window_end = min(len(before), window_end)
        after_window_end = min(len(after), window_end)

        print()
        print(f"change {ordinal}: 0x{start:08x}-0x{end:08x} ({end - start} bytes)")
        print("before:")
        for line in hexdump(before, window_start, before_window_end):
            print(f"  {line}")
        print("after:")
        for line in hexdump(after, window_start, after_window_end):
            print(f"  {line}")

        string_start = max(0, start - context * 4)
        string_end = min(max(len(before), len(after)), end + context * 4)
        before_strings = nearby_strings(before, string_start, min(len(before), string_end), min_len=4)
        after_strings = nearby_strings(after, string_start, min(len(after), string_end), min_len=4)
        if before_strings or after_strings:
            print("nearby strings:")
            for value in before_strings[:8]:
                print(f"  before {value}")
            for value in after_strings[:8]:
                print(f"  after  {value}")

    if len(ranges) > limit:
        print()
        print(f"... {len(ranges) - limit} additional changed range(s) omitted")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--context", type=int, default=48)
    parser.add_argument("--limit", type=int, default=8)
    parser.add_argument("--merge-gap", type=int, default=8)
    args = parser.parse_args()
    return compare(
        args.before,
        args.after,
        context=args.context,
        limit=args.limit,
        merge_gap=args.merge_gap,
    )


if __name__ == "__main__":
    raise SystemExit(main())
