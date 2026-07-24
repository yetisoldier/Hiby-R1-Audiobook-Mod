#!/usr/bin/env python3
"""Regression fixtures for bounded ID3v2 CHAP/CTOC parsing."""

from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


def syncsafe(value: int) -> bytes:
    if value < 0 or value >= (1 << 28):
        raise ValueError("syncsafe value out of range")
    return bytes(
        (
            (value >> 21) & 0x7F,
            (value >> 14) & 0x7F,
            (value >> 7) & 0x7F,
            value & 0x7F,
        )
    )


def frame(frame_id: str, payload: bytes, major: int) -> bytes:
    size = syncsafe(len(payload)) if major == 4 else struct.pack(">I", len(payload))
    return frame_id.encode("ascii") + size + b"\x00\x00" + payload


def text_frame(title: str, major: int) -> bytes:
    return frame("TIT2", b"\x03" + title.encode("utf-8"), major)


def chap(
    element_id: str,
    start_ms: int,
    end_ms: int,
    major: int,
    title: str | None,
) -> bytes:
    nested = text_frame(title, major) if title is not None else b""
    payload = (
        element_id.encode("ascii")
        + b"\x00"
        + struct.pack(">IIII", start_ms, end_ms, 0xFFFFFFFF, 0xFFFFFFFF)
        + nested
    )
    return frame("CHAP", payload, major)


def ctoc(element_ids: list[str], major: int) -> bytes:
    payload = (
        b"toc\x00"
        + bytes((0x03, len(element_ids)))
        + b"".join(value.encode("ascii") + b"\x00" for value in element_ids)
    )
    return frame("CTOC", payload, major)


def tag(major: int, frames: list[bytes]) -> bytes:
    body = b"".join(frames)
    return b"ID3" + bytes((major, 0, 0)) + syncsafe(len(body)) + body


def run_probe(probe: Path, audio_file: Path) -> list[str]:
    result = subprocess.run(
        [str(probe), str(audio_file)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.splitlines()


def chapter_lines(lines: list[str]) -> list[str]:
    return [line for line in lines if line.startswith("chapter=")]


def assert_case(
    probe: Path, root: Path, name: str, contents: bytes, expected: list[str]
) -> None:
    path = root / name
    path.write_bytes(contents)
    actual_lines = run_probe(probe, path)
    actual = chapter_lines(actual_lines)
    if actual != expected:
        raise AssertionError(
            f"{name}: chapter mismatch\nexpected={expected!r}\nactual={actual!r}"
        )
    count_line = f"chapter_count={len(expected)}"
    if count_line not in actual_lines:
        raise AssertionError(f"{name}: missing {count_line!r}: {actual_lines!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    args = parser.parse_args()
    probe = args.probe.resolve()

    with tempfile.TemporaryDirectory(prefix="r1-mp3-chapters-") as temp:
        root = Path(temp)

        # Physical CHAP frame order differs from the ordered top-level CTOC.
        assert_case(
            probe,
            root,
            "id3v23-ctoc.mp3",
            tag(
                3,
                [
                    chap("part02", 60_000, 120_000, 3, "Second"),
                    chap("part01", 0, 60_000, 3, "First"),
                    ctoc(["part01", "part02"], 3),
                ],
            ),
            [
                "chapter=1|0|60000|First",
                "chapter=2|60000|120000|Second",
            ],
        )

        # Without a CTOC, timestamps determine order. A missing TIT2 receives
        # a stable display title.
        assert_case(
            probe,
            root,
            "id3v24-time-order.mp3",
            tag(
                4,
                [
                    chap("late", 90_000, 150_000, 4, "Closing"),
                    chap("early", 0, 0xFFFFFFFF, 4, None),
                ],
            ),
            [
                "chapter=1|0|90000|Chapter 1",
                "chapter=2|90000|150000|Closing",
            ],
        )

        assert_case(
            probe,
            root,
            "id3v24-no-chapters.mp3",
            tag(4, [text_frame("No chapters", 4)]),
            [],
        )

        # Large unrelated metadata must be skipped by seeking, then parsing
        # must continue at the following CHAP frame.
        assert_case(
            probe,
            root,
            "id3v24-large-cover-before-chapter.mp3",
            tag(
                4,
                [
                    frame("APIC", b"\x00" * (256 * 1024), 4),
                    chap("only", 0, 30_000, 4, "After cover"),
                ],
            ),
            ["chapter=1|0|30000|After cover"],
        )

        # The declared tag exceeds the file. It must be rejected rather than
        # seeking or allocating based on untrusted metadata.
        truncated = b"ID3\x04\x00\x00" + syncsafe(4096) + b"TIT2"
        assert_case(probe, root, "id3v24-truncated.mp3", truncated, [])

    print("MP3 chapter parser fixtures passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
