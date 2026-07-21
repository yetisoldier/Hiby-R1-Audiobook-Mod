/* utf8.h — small UTF-8 / UTF-16 / Windows-1251 helpers for the audiobook app.
 *
 * Pure C99, no malloc, no globals. Used by font.c (decode UTF-8 → codepoint),
 * render.c (codepoint-aware wrap), tags.c (ID3 encodings 0/1/2/3 → UTF-8),
 * scan.c / library.c / ui.c (UTF-8-boundary-safe truncation/append).
 *
 * The on-device font (/usr/resource/fonts/msyh.ttf, Microsoft YaHei) has
 * Cyrillic glyphs (U+0400..U+04FF); these helpers make sure codepoints reach
 * the rasterizer intact and that fixed char[] buffers never split a
 * multi-byte sequence mid-codepoint.
 */

#ifndef AUDIOBOOK_UTF8_H
#define AUDIOBOOK_UTF8_H

#include <stdint.h>
#include <stddef.h>

/* Decode one UTF-8 sequence starting at s[0], not reading past s+len.
 * On success returns 0, writes the codepoint to *cp and the byte length to
 * *adv (1..4). On an invalid/overlong/truncated sequence returns -1, sets
 * *cp = 0xFFFD and *adv = 1 (caller advances one byte). */
int utf8_decode(const char *s, int len, uint32_t *cp, int *adv);

/* Expected byte length of the UTF-8 sequence starting at s[0] (1..4), or 1 if
 * s[0] is not a valid lead byte. Never reads past s+len. */
int utf8_seq_len(const char *s, int len);

/* True if b is a UTF-8 lead/initial byte (0x00..0x7F or 0xC0..0xFF). */
int utf8_is_lead(unsigned char b);

/* Copy src[0..src_len] (NUL-terminated on a boundary) into dst[dst_len] so the
 * result fits and does not end mid-codepoint: copies at most dst_len-1 bytes,
 * then backs the write head up to the last complete sequence. NUL-terminates.
 * src_len may be -1 to mean "strlen(src)". Returns bytes written (excl. NUL). */
int utf8_safe_truncate(char *dst, int dst_len, const char *src, int src_len);

/* Append src to the NUL-terminated string in buf[buf_len] without splitting a
 * UTF-8 sequence at the boundary. Returns the new total length (excl. NUL). */
int utf8_safe_append(char *buf, int buf_len, const char *src);

/* Convert a UTF-16 byte stream (src_len bytes) to NUL-terminated UTF-8 in
 * out[out_len]. big_endian selects byte order when no BOM is present; a leading
 * BOM (0xFF 0xFE or 0xFE 0xFF) overrides it and is skipped. Handles surrogate
 * pairs. Returns bytes written (excl. NUL), or -1 if out is too small. */
int utf16_to_utf8(const uint8_t *src, int src_len, int big_endian,
                  char *out, int out_len);

/* Convert a Windows-1251 byte stream (src_len bytes, 0x00..0xFF) to
 * NUL-terminated UTF-8 in out[out_len]. Bytes 0x00..0x7F pass through as ASCII;
 * 0x80..0xFF map via the embedded cp1251 table. Returns bytes written
 * (excl. NUL), or -1 if out is too small. */
int cp1251_to_utf8(const uint8_t *src, int src_len, char *out, int out_len);

/* Validate whether s[0..len] (or up to first NUL if len<0) is valid UTF-8.
 * Returns 1 if valid, 0 if any invalid/overlong/surrogate sequence found. */
int utf8_is_valid(const char *s, int len);

#endif /* AUDIOBOOK_UTF8_H */