#!/usr/bin/env python3
"""Generate same-size audiobook launcher icons for the R1 resource tree."""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        raise ValueError(f"not a PNG with IHDR header: {path}")
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, width: int, height: int, pixels: list[tuple[int, int, int, int]]) -> None:
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            row.extend(pixels[y * width + x])
        rows.append(bytes(row))
    png = (
        PNG_SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"".join(rows), level=9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def blend(dst: tuple[int, int, int, int], src: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    sr, sg, sb, sa = src
    if sa <= 0:
        return dst
    if sa >= 255:
        return src
    dr, dg, db, da = dst
    inv = 255 - sa
    out_a = sa + da * inv // 255
    if out_a <= 0:
        return (0, 0, 0, 0)
    out_r = (sr * sa + dr * da * inv // 255) // out_a
    out_g = (sg * sa + dg * da * inv // 255) // out_a
    out_b = (sb * sa + db * da * inv // 255) // out_a
    return (out_r, out_g, out_b, out_a)


def draw_rounded_rect(
    pixels: list[tuple[int, int, int, int]],
    width: int,
    height: int,
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    radius: float,
    color: tuple[int, int, int, int],
) -> None:
    min_x = max(0, int(math.floor(x0)))
    max_x = min(width, int(math.ceil(x1)))
    min_y = max(0, int(math.floor(y0)))
    max_y = min(height, int(math.ceil(y1)))
    for y in range(min_y, max_y):
        for x in range(min_x, max_x):
            cx = min(max(x + 0.5, x0 + radius), x1 - radius)
            cy = min(max(y + 0.5, y0 + radius), y1 - radius)
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= radius * radius:
                idx = y * width + x
                pixels[idx] = blend(pixels[idx], color)


def draw_line(
    pixels: list[tuple[int, int, int, int]],
    width: int,
    height: int,
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    stroke: float,
    color: tuple[int, int, int, int],
) -> None:
    min_x = max(0, int(math.floor(min(x0, x1) - stroke)))
    max_x = min(width, int(math.ceil(max(x0, x1) + stroke)))
    min_y = max(0, int(math.floor(min(y0, y1) - stroke)))
    max_y = min(height, int(math.ceil(max(y0, y1) + stroke)))
    dx = x1 - x0
    dy = y1 - y0
    length_sq = dx * dx + dy * dy
    radius = stroke / 2
    for y in range(min_y, max_y):
        for x in range(min_x, max_x):
            px = x + 0.5
            py = y + 0.5
            if length_sq:
                t = max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / length_sq))
            else:
                t = 0.0
            cx = x0 + t * dx
            cy = y0 + t * dy
            if (px - cx) ** 2 + (py - cy) ** 2 <= radius * radius:
                idx = y * width + x
                pixels[idx] = blend(pixels[idx], color)


def draw_arc(
    pixels: list[tuple[int, int, int, int]],
    width: int,
    height: int,
    cx: float,
    cy: float,
    radius: float,
    stroke: float,
    start_deg: float,
    end_deg: float,
    color: tuple[int, int, int, int],
) -> None:
    min_x = max(0, int(math.floor(cx - radius - stroke)))
    max_x = min(width, int(math.ceil(cx + radius + stroke)))
    min_y = max(0, int(math.floor(cy - radius - stroke)))
    max_y = min(height, int(math.ceil(cy + radius + stroke)))
    start = math.radians(start_deg)
    end = math.radians(end_deg)
    half = stroke / 2
    for y in range(min_y, max_y):
        for x in range(min_x, max_x):
            px = x + 0.5
            py = y + 0.5
            angle = math.atan2(py - cy, px - cx)
            if angle < 0:
                angle += math.tau
            angle_ok = start <= angle <= end if start <= end else angle >= start or angle <= end
            if not angle_ok:
                continue
            dist = math.hypot(px - cx, py - cy)
            if abs(dist - radius) <= half:
                idx = y * width + x
                pixels[idx] = blend(pixels[idx], color)


def downsample(
    high: list[tuple[int, int, int, int]],
    width: int,
    height: int,
    scale: int,
) -> list[tuple[int, int, int, int]]:
    out: list[tuple[int, int, int, int]] = []
    hw = width * scale
    area = scale * scale
    for y in range(height):
        for x in range(width):
            totals = [0, 0, 0, 0]
            for sy in range(scale):
                offset = (y * scale + sy) * hw + x * scale
                for sx in range(scale):
                    r, g, b, a = high[offset + sx]
                    totals[0] += r
                    totals[1] += g
                    totals[2] += b
                    totals[3] += a
            out.append(tuple(min(255, value // area) for value in totals))  # type: ignore[arg-type]
    return out


def render_icon(width: int, height: int, *, selected: bool) -> list[tuple[int, int, int, int]]:
    scale = 4
    hw = width * scale
    hh = height * scale
    pixels: list[tuple[int, int, int, int]] = [(0, 0, 0, 0)] * (hw * hh)
    unit = min(hw, hh)
    ox = (hw - unit) / 2
    oy = (hh - unit) / 2

    navy = (50, 82, 108, 255)
    navy_dark = (33, 56, 74, 255)
    page = (235, 247, 252, 255)
    page_shadow = (193, 218, 229, 255)
    accent = (49, 170, 154, 255) if selected else (67, 140, 162, 255)
    highlight = (255, 255, 255, 72)

    def sx(value: float) -> float:
        return ox + value * unit

    def sy(value: float) -> float:
        return oy + value * unit

    draw_arc(pixels, hw, hh, sx(0.50), sy(0.47), unit * 0.34, unit * 0.075, 202, 338, navy, )
    draw_rounded_rect(pixels, hw, hh, sx(0.16), sy(0.44), sx(0.30), sy(0.72), unit * 0.045, navy, )
    draw_rounded_rect(pixels, hw, hh, sx(0.70), sy(0.44), sx(0.84), sy(0.72), unit * 0.045, navy, )
    draw_rounded_rect(pixels, hw, hh, sx(0.20), sy(0.69), sx(0.80), sy(0.80), unit * 0.04, page_shadow, )
    draw_rounded_rect(pixels, hw, hh, sx(0.20), sy(0.38), sx(0.50), sy(0.74), unit * 0.055, page, )
    draw_rounded_rect(pixels, hw, hh, sx(0.50), sy(0.38), sx(0.80), sy(0.74), unit * 0.055, page, )
    draw_line(pixels, hw, hh, sx(0.50), sy(0.40), sx(0.50), sy(0.75), unit * 0.035, navy_dark, )
    draw_line(pixels, hw, hh, sx(0.29), sy(0.49), sx(0.43), sy(0.49), unit * 0.025, accent, )
    draw_line(pixels, hw, hh, sx(0.29), sy(0.58), sx(0.43), sy(0.58), unit * 0.025, accent, )
    draw_line(pixels, hw, hh, sx(0.57), sy(0.49), sx(0.71), sy(0.49), unit * 0.025, accent, )
    draw_line(pixels, hw, hh, sx(0.57), sy(0.58), sx(0.71), sy(0.58), unit * 0.025, accent, )
    draw_rounded_rect(pixels, hw, hh, sx(0.57), sy(0.32), sx(0.67), sy(0.64), unit * 0.02, accent, )
    draw_line(pixels, hw, hh, sx(0.27), sy(0.41), sx(0.42), sy(0.41), unit * 0.018, highlight, )
    draw_line(pixels, hw, hh, sx(0.58), sy(0.41), sx(0.73), sy(0.41), unit * 0.018, highlight, )
    if selected:
        draw_arc(pixels, hw, hh, sx(0.50), sy(0.47), unit * 0.41, unit * 0.035, 205, 335, accent, )

    return downsample(pixels, width, height, scale)


def patch_icons(rootfs: Path, *, dry_run: bool) -> int:
    litegui = rootfs / "usr" / "resource" / "litegui"
    if not litegui.is_dir():
        raise SystemExit(f"litegui resource directory not found: {litegui}")
    targets = sorted(litegui.rglob("launcher/book*.png"))
    if not targets:
        raise SystemExit(f"no launcher book icons found under {litegui}")
    for path in targets:
        width, height = read_png_size(path)
        selected = path.stem.endswith("_s")
        print(f"{'would patch' if dry_run else 'patch'} {path} ({width}x{height})")
        if not dry_run:
            write_rgba_png(path, width, height, render_icon(width, height, selected=selected))
    return len(targets)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rootfs", type=Path, help="Extracted rootfs containing usr/resource/litegui.")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    count = patch_icons(args.rootfs, dry_run=args.dry_run)
    print(f"{'checked' if args.dry_run else 'patched'} {count} launcher icons")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
