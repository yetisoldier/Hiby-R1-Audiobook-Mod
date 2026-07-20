#!/usr/bin/env python3
"""Pre-flight check before reading an image file into a Claude Code session.

When the main model lacks vision support (e.g. GLM 5.2), calling Read on an
image will crash the session with an API MIME-type error.  Run this script
before any Read on a .png / .jpg / .jpeg to validate the file and surface
the vision-agent reminder so the unsafe path is never taken accidentally.

Usage:
    py -3 tools/safe_image_check.py work/adb-control/screenshots/screen.png

If the script prints the VISION SAFETY WARNING, use the vision sub-agent
instead of Read:

    Agent(description="vision-analyze-r1-screen",
          model="gemma4:26b-a4b-it-qat",
          prompt="Read the attached image and describe what UI screen it shows...")
"""

from __future__ import annotations

import sys
from pathlib import Path

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
JPEG_MAGIC = (b"\xff\xd8\xff",)
GIF_MAGIC = (b"GIF87a", b"GIF89a")
BMP_MAGIC = b"BM"
WEBP_MAGIC = b"RIFF"  # followed by file size then "WEBP"

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"}


def _magic_kind(data: bytes) -> str | None:
    if data.startswith(PNG_MAGIC):
        return "PNG"
    if any(data.startswith(m) for m in JPEG_MAGIC):
        return "JPEG"
    if any(data.startswith(m) for m in GIF_MAGIC):
        return "GIF"
    if data.startswith(BMP_MAGIC):
        return "BMP"
    if data.startswith(WEBP_MAGIC) and len(data) >= 12 and data[8:12] == b"WEBP":
        return "WEBP"
    return None


def check(path_str: str) -> int:
    path = Path(path_str)

    if not path.exists():
        print(f"[SAFE IMAGE CHECK] ERROR: {path} does not exist", file=sys.stderr)
        return 1

    data = path.read_bytes()
    ext = path.suffix.lower()
    magic = _magic_kind(data)
    looks_like_image = ext in IMAGE_EXTS or magic is not None

    if not looks_like_image:
        print(f"[SAFE IMAGE CHECK] {path} is not an image file. OK to Read.")
        return 0

    # File appears to be an image
    print(f"[SAFE IMAGE CHECK] {path}")
    print(f"  Extension: {ext if ext else 'none'}")
    print(f"  Detected type: {magic or 'UNKNOWN / possibly malformed'}")
    print(f"  Size: {len(data)} bytes")

    if magic == "PNG":
        print("  Magic bytes: VALID (89 50 4E 47)")
    elif magic is None and ext in IMAGE_EXTS:
        print(f"  WARNING: extension says {ext} but magic bytes do not match — file may be corrupt")

    print("")
    print("=" * 62)
    print("  VISION SAFETY WARNING")
    print("=" * 62)
    print("  This file is an image. If the CURRENT main model lacks")
    print("  vision support, DO NOT 'Read' it directly in the main thread.")
    print("  Doing so will crash the session with an API error.")
    print("")
    print("  Instead, delegate to the vision sub-agent:")
    print("")
    print('  Agent(description="vision-analyze-r1-screen",')
    print('        model="gemma4:26b-a4b-it-qat",')
    print('        prompt="Read the attached image and describe...")')
    print("")
    print("  Only Read the image directly if you are CERTAIN the main")
    print("  model supports image input (e.g. gemma4, qwen3-vl).")
    print("=" * 62)
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <image-path>", file=sys.stderr)
        raise SystemExit(1)
    raise SystemExit(check(sys.argv[1]))
