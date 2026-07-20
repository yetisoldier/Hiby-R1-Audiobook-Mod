#!/usr/bin/env python3
"""Capture the HiBy R1 framebuffer over ADB and write a PNG.

The R1 framebuffer is 480x800 RGB565 with a 960-byte stride in the stock UI.
This tool is read-only on the device apart from writing a temporary raw file
under /usr/data.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import zlib
from pathlib import Path


DEFAULT_ADB = r"C:\Program Files\Software Fix\adb.exe"
WIDTH = 480
HEIGHT = 800
STRIDE = WIDTH * 2


def run(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if check and proc.returncode != 0:
        raise RuntimeError(f"{' '.join(args)} failed with {proc.returncode}\n{proc.stdout}")
    return proc


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def rgb565_to_png(raw: bytes) -> bytes:
    if len(raw) < STRIDE * HEIGHT:
        raise ValueError(f"framebuffer capture too short: {len(raw)} bytes")
    rows: list[bytes] = []
    for y in range(HEIGHT):
        row = bytearray()
        offset = y * STRIDE
        row.append(0)  # PNG filter type 0
        for x in range(WIDTH):
            value = raw[offset + x * 2] | (raw[offset + x * 2 + 1] << 8)
            r5 = (value >> 11) & 0x1F
            g6 = (value >> 5) & 0x3F
            b5 = value & 0x1F
            row.extend(
                (
                    (r5 << 3) | (r5 >> 2),
                    (g6 << 2) | (g6 >> 4),
                    (b5 << 3) | (b5 >> 2),
                )
            )
        rows.append(bytes(row))
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(b"".join(rows), level=6))
        + png_chunk(b"IEND", b"")
    )


PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def validate_png(path: Path) -> None:
    """Verify the file starts with valid PNG magic bytes.

    Raises RuntimeError if validation fails so callers can catch it before
    the bad file ever reaches an API or image viewer.
    """
    data = path.read_bytes()
    if len(data) < len(PNG_MAGIC):
        raise RuntimeError(f"PNG validation failed: {path} is only {len(data)} bytes")
    if not data.startswith(PNG_MAGIC):
        raise RuntimeError(
            f"PNG validation failed: {path} does not start with PNG magic bytes "
            f"(expected {PNG_MAGIC.hex()}, got {data[:len(PNG_MAGIC)].hex()})"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--output", type=Path, default=Path("work/fb0_capture.png"))
    parser.add_argument("--raw-output", type=Path)
    parser.add_argument("--remote-raw", default="/usr/data/fb0-codex-capture.raw")
    parser.add_argument("--verify", action="store_true", default=True, help="Validate PNG magic bytes after capture (default: on)")
    parser.add_argument("--no-verify", action="store_true", help="Skip PNG validation")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    raw_output = args.raw_output or args.output.with_suffix(".raw")
    raw_output.parent.mkdir(parents=True, exist_ok=True)

    run(
        [
            args.adb,
            "shell",
            f"rm -f {args.remote_raw}; dd if=/dev/fb0 of={args.remote_raw} bs={STRIDE} count={HEIGHT}",
        ]
    )
    run([args.adb, "pull", args.remote_raw, str(raw_output)])
    raw = raw_output.read_bytes()
    png = rgb565_to_png(raw)
    args.output.write_bytes(png)

    do_verify = args.verify and not args.no_verify
    if do_verify:
        validate_png(args.output)
        print(f"png: {args.output} ({len(png)} bytes) [validated OK]")
    else:
        print(f"png: {args.output} ({len(png)} bytes) [validation skipped]")

    print(f"raw: {raw_output} ({len(raw)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
