/* utf8.c — UTF-8 / UTF-16 / Windows-1251 helpers. See utf8.h. */

#include "utf8.h"

#include <string.h>

/* ---- UTF-8 ---- */

static int is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

int utf8_is_lead(unsigned char b) {
    return b < 0x80 || (b >= 0xC0 && b < 0xF8);
}

int utf8_seq_len(const char *s, int len) {
    if (len < 1) return 1;
    unsigned char b = (unsigned char)s[0];
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;  /* invalid lead byte */
}

int utf8_decode(const char *s, int len, uint32_t *cp, int *adv) {
    *cp = 0xFFFD;
    *adv = 1;
    if (len < 1) return -1;
    unsigned char b = (unsigned char)s[0];

    if (b < 0x80) { *cp = b; *adv = 1; return 0; }
    if (is_cont(b)) return -1;

    uint32_t v;
    int n;
    if ((b & 0xE0) == 0xC0)      { v = b & 0x1F; n = 2; }
    else if ((b & 0xF0) == 0xE0) { v = b & 0x0F; n = 3; }
    else if ((b & 0xF8) == 0xF0) { v = b & 0x07; n = 4; }
    else return -1;

    if (len < n) return -1;
    for (int i = 1; i < n; i++) {
        if (!is_cont((unsigned char)s[i])) return -1;
        v = (v << 6) | ((unsigned char)s[i] & 0x3F);
    }

    /* reject overlong and surrogate */
    if (n == 2 && v < 0x80) return -1;
    if (n == 3 && (v < 0x800 || (v >= 0xD800 && v < 0xE000))) return -1;
    if (n == 4 && (v < 0x10000 || v > 0x10FFFF)) return -1;

    *cp = v;
    *adv = n;
    return 0;
}

int utf8_safe_truncate(char *dst, int dst_len, const char *src, int src_len) {
    if (!dst || dst_len <= 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }
    if (src_len < 0) src_len = (int)strlen(src);

    int cap = dst_len - 1;
    int n = src_len < cap ? src_len : cap;
    if (n <= 0) { dst[0] = '\0'; return 0; }

    memcpy(dst, src, (size_t)n);
    /* Back up only if the copy ended mid-sequence: scan back over trailing
     * continuation bytes to the lead, and if that lead's full sequence didn't
     * fit, drop the partial lead too. A complete trailing sequence is kept. */
    int last = n;
    int i = n;
    while (i > 0 && is_cont((unsigned char)dst[i - 1])) i--;
    if (i > 0) {
        unsigned char b = (unsigned char)dst[i - 1];
        if (b >= 0x80) {  /* a lead byte (or stray high byte) */
            int expect = utf8_seq_len(dst + i - 1, 4);
            int have = n - (i - 1);
            if (have < expect) last = i - 1;  /* incomplete sequence: drop lead */
        }
        /* else last byte is ASCII (complete 1-byte) -> keep n */
    } else {
        last = 0;  /* only continuation bytes were copied */
    }
    dst[last] = '\0';
    return last;
}

int utf8_safe_append(char *buf, int buf_len, const char *src) {
    if (!buf || buf_len <= 0) return 0;
    int cur = 0;
    while (cur < buf_len - 1 && buf[cur]) cur++;
    if (!src) return cur;
    int slen = (int)strlen(src);
    int room = buf_len - 1 - cur;
    int n = slen < room ? slen : room;
    if (n <= 0) return cur;
    memcpy(buf + cur, src, (size_t)n);
    /* Same boundary-safe back-off as truncate, on the tail we just wrote. */
    int end = cur + n;
    int i = end;
    while (i > cur && is_cont((unsigned char)buf[i - 1])) i--;
    if (i > cur) {
        unsigned char b = (unsigned char)buf[i - 1];
        if (b >= 0x80) {
            int expect = utf8_seq_len(buf + i - 1, 4);
            int have = end - (i - 1);
            if (have < expect) end = i - 1;
        }
    } else {
        end = cur;
    }
    buf[end] = '\0';
    return end;
}

/* ---- UTF-16 -> UTF-8 ---- */

static int emit_utf8(char *out, int out_len, int *o, uint32_t cp) {
    char *p = out + *o;
    int room = out_len - *o - 1;
    if (cp <= 0x7F) {
        if (room < 1) return -1;
        p[0] = (char)cp; *o += 1;
    } else if (cp <= 0x7FF) {
        if (room < 2) return -1;
        p[0] = (char)(0xC0 | (cp >> 6));
        p[1] = (char)(0x80 | (cp & 0x3F));
        *o += 2;
    } else if (cp <= 0xFFFF) {
        if (room < 3) return -1;
        p[0] = (char)(0xE0 | (cp >> 12));
        p[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        p[2] = (char)(0x80 | (cp & 0x3F));
        *o += 3;
    } else {
        if (room < 4) return -1;
        p[0] = (char)(0xF0 | (cp >> 18));
        p[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        p[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        p[3] = (char)(0x80 | (cp & 0x3F));
        *o += 4;
    }
    return 0;
}

int utf16_to_utf8(const uint8_t *src, int src_len, int big_endian,
                  char *out, int out_len) {
    if (!out || out_len <= 0) return -1;
    int i = 0, o = 0;
    out[0] = '\0';
    if (!src || src_len < 2) return 0;

    /* BOM override */
    if (src_len >= 2 && src[0] == 0xFF && src[1] == 0xFE) { big_endian = 0; i = 2; }
    else if (src_len >= 2 && src[0] == 0xFE && src[1] == 0xFF) { big_endian = 1; i = 2; }

    while (i + 1 < src_len) {
        uint16_t u;
        if (big_endian) u = (uint16_t)((src[i] << 8) | src[i + 1]);
        else           u = (uint16_t)((src[i + 1] << 8) | src[i]);
        i += 2;

        /* Stop at a NUL code unit (common in ID3 frames) */
        if (u == 0) break;

        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < src_len) {
            uint16_t lo;
            if (big_endian) lo = (uint16_t)((src[i] << 8) | src[i + 1]);
            else           lo = (uint16_t)((src[i + 1] << 8) | src[i]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                i += 2;
                uint32_t cp = 0x10000 + (((u - 0xD800) << 10) | (lo - 0xDC00));
                if (emit_utf8(out, out_len, &o, cp) < 0) return -1;
                continue;
            }
        }
        if (emit_utf8(out, out_len, &o, u) < 0) return -1;
    }
    out[o] = '\0';
    return o;
}

/* ---- Windows-1251 -> UTF-8 ---- */

/* cp1251 codepoints for 0x80..0xFF, from the WHATWG encoding index. */
static const uint16_t cp1251_tab[128] = {
    0x0402,0x0403,0x201A,0x0453,0x201E,0x2026,0x2020,0x2021,  /* 80-87 */
    0x20AC,0x2030,0x0409,0x2039,0x040A,0x040C,0x040B,0x040F,  /* 88-8F */
    0x0452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,  /* 90-97 */
    0x0098,0x2122,0x0459,0x203A,0x045A,0x045C,0x045B,0x045F,  /* 98-9F */
    0x00A0,0x040E,0x045E,0x0408,0x00A4,0x0490,0x00A6,0x00A7,  /* A0-A7 */
    0x0401,0x00A9,0x0404,0x00AB,0x00AC,0x00AD,0x00AE,0x0407,  /* A8-AF */
    0x00B0,0x00B1,0x0406,0x0456,0x0491,0x00B5,0x00B6,0x00B7,  /* B0-B7 */
    0x0451,0x2116,0x0454,0x00BB,0x0458,0x0405,0x0455,0x0457,  /* B8-BF */
    0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,  /* C0-C7 */
    0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,  /* C8-CF */
    0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,  /* D0-D7 */
    0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,  /* D8-DF */
    0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,  /* E0-E7 */
    0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,  /* E8-EF */
    0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,  /* F0-F7 */
    0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F,  /* F8-FF */
};

int cp1251_to_utf8(const uint8_t *src, int src_len, char *out, int out_len) {
    if (!out || out_len <= 0) return -1;
    int o = 0;
    out[0] = '\0';
    if (!src) return 0;
    for (int i = 0; i < src_len; i++) {
        uint8_t b = src[i];
        if (b == 0) break;
        uint32_t cp = (b < 0x80) ? b : cp1251_tab[b - 0x80];
        if (emit_utf8(out, out_len, &o, cp) < 0) return -1;
    }
    out[o] = '\0';
    return o;
}