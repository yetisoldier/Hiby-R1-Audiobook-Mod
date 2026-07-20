/* pngdec.h — memory-safe streaming PNG -> RGB565 cover decoder.
 *
 * Counterpart to cover.c's libjpeg path. PNG covers (external cover.png /
 * folder.png, or embedded PNG extracted by tags.c) are decoded here to a
 * px*px RGB565 square via a row-by-row inflate + on-the-fly nearest-neighbor
 * downsample, so a large PNG (e.g. 2400x2400) never costs the full image in
 * RAM — only a 32 KB inflate window, two full-width row buffers, and the
 * px*px output (~150 KB peak). libpng/spng can't do this (they decode the
 * whole image to a buffer, which OOMs this ~12 MB-free device), and there is
 * no libpng on the device anyway — only libz, which we dlopen lazily.
 *
 * Supported: 8-bit, non-interlaced, color types 0 (gray), 2 (RGB), 4 (gray+alpha),
 * 6 (RGBA). Adam7 interlacing, palette (type 3), and non-8-bit PNGs bail to
 * "no cover" (return 0) — never crash the host — matching the progressive-JPEG
 * guard discipline in cover.c. Cover photos are essentially always 8-bit
 * RGB/RGBA non-interlaced, so this covers real-world covers.
 *
 * Everything untrusted (chunk lengths, width/height) is bounds-checked; any
 * malformed input returns 0.
 */
#ifndef AUDIOBOOK_PNGDEC_H
#define AUDIOBOOK_PNGDEC_H

#include <stdint.h>

/* Decode the PNG at path to a px*px RGB565 square into caller-provided out
 * (px*px uint16_t). libz is dlopen'd lazily; missing libz or any unsupported
 * PNG variant returns 0 (no cover), never crashes. Returns 1 on success,
 * 0 on any failure (out is left untouched). */
int png_decode_to_rgb565(const char *path, int px, uint16_t *out);

#endif /* AUDIOBOOK_PNGDEC_H */