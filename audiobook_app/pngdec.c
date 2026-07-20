/* pngdec.c — memory-safe streaming PNG -> RGB565 cover decoder.
 *
 * Why a hand-rolled decoder instead of libpng/spng: there is no libpng on the
 * device (only libz.so.1.2.11), and libpng/spng decode the WHOLE image into a
 * contiguous buffer — a 2400x2400 RGBA PNG is ~23 MB, instant OOM at ~12 MB
 * free while hiby_player runs. There is no libpng equivalent of libjpeg's
 * scale_denom DCT-skip. So we own the decode loop: inflate IDAT row-by-row over
 * dlopen'd libz, reconstruct PNG filters using two full-width row buffers, and
 * nearest-neighbor downsample into the px*px RGB565 output on the fly. Peak RAM
 * is the ~32 KB inflate window + two rows (~20 KB for 2400-wide RGBA) + the
 * px*px output (~97 KB at 220px) ≈ 150 KB.
 *
 * libz is dlopen'd OPTIONAL (same pattern as libjpeg / fdk-aac): missing libz
 * or any unsupported PNG variant returns 0 (no cover), never crashes the host
 * — same discipline as cover.c's progressive-JPEG guard.
 *
 * Supported: 8-bit, non-interlaced, color types 0 (gray), 2 (RGB), 4 (gray+alpha),
 * 6 (RGBA). Adam7 interlacing, palette (type 3), and non-8-bit bail to 0.
 * Every chunk length / width / height is bounds-checked; any malformed input
 * returns 0.
 */
#include "pngdec.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>

/* ---- RGB888 -> RGB565 (identical to cover.c) ----------------------------- */
static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* ---- z_stream ABI (stable since zlib 1.0; declared locally, dlsym'd) ----- */
typedef unsigned char Bytef;
typedef unsigned int  uInt;
typedef unsigned long uLong;
typedef void *voidpf;
typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void  (*free_func)(voidpf opaque, voidpf address);

typedef struct {
    const Bytef *next_in;
    uInt  avail_in;
    uLong total_in;
    Bytef *next_out;
    uInt  avail_out;
    uLong total_out;
    const char *msg;
    struct internal_state *state;
    alloc_func zalloc;
    free_func  zfree;
    voidpf     opaque;
    int  data_type;
    uLong adler;
    uLong reserved;
} z_stream;

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NO_FLUSH      0
#define Z_DATA_ERROR   (-3)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

typedef int (*pfn_inflateInit_)(z_stream *, const char *, int);
typedef int (*pfn_inflate)(z_stream *, int);
typedef int (*pfn_inflateEnd)(z_stream *);
typedef const char *(*pfn_zlibVersion)(void);

static void *s_lib = NULL;
static pfn_inflateInit_  x_inflateInit_;
static pfn_inflate       x_inflate;
static pfn_inflateEnd    x_inflateEnd;
static pfn_zlibVersion   x_zlibVersion;
static int s_lib_tried = 0;

static int load_libz(void) {
    if (s_lib_tried) return s_lib != NULL;
    s_lib_tried = 1;
    s_lib = dlopen("//usr/lib/libz.so", RTLD_NOW);
    if (!s_lib) s_lib = dlopen("libz.so.1", RTLD_NOW);
    if (!s_lib) return 0;
    #define LOAD(dst, name) do { dst = (__typeof__(dst))dlsym(s_lib, name); \
        if (!dst) { dlclose(s_lib); s_lib = NULL; return 0; } } while (0)
    LOAD(x_inflateInit_, "inflateInit_");
    LOAD(x_inflate,      "inflate");
    LOAD(x_inflateEnd,   "inflateEnd");
    LOAD(x_zlibVersion,  "zlibVersion");
    #undef LOAD
    return 1;
}

/* ---- big-endian chunk readers ------------------------------------------- */
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* ---- streaming file reader ---------------------------------------------- *
 * One 64 KB input buffer shared by the chunk walker and inflate. in_pos is
 * the authoritative cursor; z_stream.next_in is set fresh each time we hand
 * inflate a run of bytes, and only when avail_in == 0 — so a buffer shift
 * (ensure) never invalidates a pointer inflate is still using. */
#define INBUF_SZ 65536
static FILE    *g_fp;
static uint8_t  inbuf[INBUF_SZ];
static size_t   in_pos, in_len;     /* valid bytes are [in_pos, in_len) */

static int ensure(size_t n) {
    if (in_len - in_pos >= n) return 1;
    size_t rem = in_len - in_pos;
    memmove(inbuf, inbuf + in_pos, rem);
    in_pos = 0; in_len = rem;
    in_len += fread(inbuf + in_len, 1, sizeof(inbuf) - in_len, g_fp);
    return in_len >= n;              /* in_pos is 0 here */
}

/* Read n exact bytes into dst. Only called when inflate has no pending input
 * (avail_in == 0), so the buffer shift is safe. 1 ok, 0 EOF/short. */
static int read_bytes(uint8_t *dst, size_t n) {
    if (!ensure(n)) return 0;
    memcpy(dst, inbuf + in_pos, n);
    in_pos += n;
    return 1;
}

/* Skip n bytes (a chunk's data + CRC, or an IDAT's trailing CRC). */
static int skip_bytes(size_t n) {
    while (n > 0) {
        size_t avail = in_len - in_pos;
        if (avail == 0) { if (!ensure(1)) return 0; avail = in_len - in_pos; }
        size_t take = n < avail ? n : avail;
        in_pos += take; n -= take;
    }
    return 1;
}

/* ---- IDAT inflate feed --------------------------------------------------- *
 * The zlib stream is the concatenation of ALL consecutive IDAT chunk data
 * (with each IDAT's trailing 4-byte CRC skipped). We hand inflate runs of
 * bytes straight from inbuf; when an IDAT's data is exhausted we skip its CRC
 * and walk to the next chunk, continuing only on IDAT (other chunks skipped),
 * stopping at IEND. State: idat_left = bytes remaining in the current IDAT. */
static uint32_t idat_left;   /* remaining bytes in the current IDAT's data */
static int had_idat;         /* the chunk we just finished feeding was an IDAT
                              * (so its 4-byte CRC must be skipped next) */

/* Ensure z_stream has input (avail_in > 0) for inflate. Returns:
 *  1 = input available (next_in/avail_in set),
 *  0 = end of all IDAT data (IEND reached / EOF) — inflate should flush,
 * -1 = error. */
static int fill_input(z_stream *s) {
    s->next_in = NULL; s->avail_in = 0;
    for (;;) {
        if (idat_left > 0) {
            size_t avail = in_len - in_pos;
            if (avail == 0) { if (!ensure(1)) return -1; avail = in_len - in_pos; }
            size_t take = idat_left < avail ? idat_left : avail;
            s->next_in = inbuf + in_pos;
            s->avail_in = (uInt)take;
            return 1;
        }
        /* current IDAT exhausted: skip its 4-byte CRC (only if the chunk we
         * just finished was an IDAT — the FIRST chunk after IHDR is not). */
        if (had_idat) {
            if (!skip_bytes(4)) return -1;
            had_idat = 0;
        }
        uint8_t hdr[8];
        if (!read_bytes(hdr, 8)) return 0;   /* EOF: no more chunks -> flush */
        uint32_t clen = be32(hdr);
        uint32_t ctype = be32(hdr + 4);
        if (ctype == 0x49444154u) { idat_left = clen; had_idat = 1; continue; } /* "IDAT" */
        if (ctype == 0x49454e44u) return 0;                          /* "IEND" */
        /* any other chunk: skip its data + 4-byte CRC */
        if (!skip_bytes((size_t)clen + 4)) return -1;
    }
}

/* ---- PNG signature ------------------------------------------------------- */
static const uint8_t PNG_SIG[8] = { 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A };

/* ---- Paeth predictor ----------------------------------------------------- */
static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p - a; if (pa < 0) pa = -pa;
    int pb = p - b; if (pb < 0) pb = -pb;
    int pc = p - c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* Decode path -> px*px RGB565 into caller-provided out. Returns 1 on success,
 * 0 on any failure (out may be partially written on failure, same as the
 * libjpeg path — caller frees it). */
int png_decode_to_rgb565(const char *path, int px, uint16_t *out) {
    if (!path || !out || px <= 0) return 0;
    if (!load_libz()) return 0;

    g_fp = fopen(path, "rb");
    if (!g_fp) return 0;
    in_pos = 0; in_len = 0;
    idat_left = 0;
    had_idat = 0;

    int rv = 0;            /* assume failure until full success */
    z_stream zs;
    uint8_t *rawbuf = NULL, *prev = NULL, *cur = NULL;
    int strm_inited = 0;

    /* PNG signature */
    uint8_t sig[8];
    if (!read_bytes(sig, 8) || memcmp(sig, PNG_SIG, 8) != 0) goto done;

    /* IHDR must be first */
    uint8_t ihdr[25];      /* 8 header + 13 data + 4 CRC */
    if (!read_bytes(ihdr, 25)) goto done;
    if (be32(ihdr + 4) != 0x49484452u) goto done;     /* "IHDR" */
    uint32_t width  = be32(ihdr + 8);
    uint32_t height = be32(ihdr + 12);
    uint8_t  depth    = ihdr[16];
    uint8_t  colortype = ihdr[17];
    uint8_t  comp   = ihdr[18];
    uint8_t  filt   = ihdr[19];
    uint8_t  interl = ihdr[20];

    /* Validate. Bails on interlaced / palette / non-8-bit (covers are photos). */
    if (width == 0 || height == 0 || width > 4096 || height > 4096) goto done;
    if (depth != 8) goto done;
    if (interl != 0) goto done;       /* Adam7 — too complex, bail */
    if (comp != 0 || filt != 0) goto done;
    int ch;
    switch (colortype) {
        case 0: ch = 1; break;  /* gray */
        case 2: ch = 3; break;  /* RGB */
        case 4: ch = 2; break;  /* gray + alpha */
        case 6: ch = 4; break;  /* RGBA */
        default: goto done;     /* palette (3) or unknown — bail */
    }
    int bpp = ch;
    /* row_bytes = width * bpp, overflow-checked (width<=4096, bpp<=4 => < 64KB) */
    size_t row_bytes = (size_t)width * bpp;
    if (row_bytes > 65536) goto done;   /* paranoia cap */
    size_t scanline = row_bytes + 1;    /* 1 filter byte + data */

    rawbuf = malloc(scanline);
    prev   = calloc(1, row_bytes);      /* zeroed: first row's "up" is 0 */
    cur    = malloc(row_bytes);
    if (!rawbuf || !prev || !cur) goto done;

    /* inflate init: pass the runtime version string (self-accepted), not a
     * hardcoded constant (a mismatch yields Z_VERSION_ERROR). */
    memset(&zs, 0, sizeof(zs));
    if (x_inflateInit_(&zs, x_zlibVersion(), (int)sizeof(z_stream)) != Z_OK) goto done;
    strm_inited = 1;

    int ow = (int)width, oh = (int)height;
    size_t rawfill = 0;     /* decoded bytes accumulated toward current scanline */
    int stream_ended = 0;

    for (int y = 0; y < oh; y++) {
        /* Pump inflate until one full scanline is ready. */
        while (rawfill < scanline) {
            if (zs.avail_in == 0 && !stream_ended) {
                int fr = fill_input(&zs);
                if (fr < 0) goto done;
                if (fr == 0) stream_ended = 1;   /* no more IDAT input; flush */
                if (fr == 0) { zs.next_in = NULL; zs.avail_in = 0; }
            }
            zs.next_out  = rawbuf + rawfill;
            zs.avail_out = (uInt)(scanline - rawfill);
            uInt gave_in = zs.avail_in;
            int r = x_inflate(&zs, Z_NO_FLUSH);
            /* account consumed input against our in_pos/idat_left */
            uInt consumed_in = gave_in - zs.avail_in;
            in_pos    += consumed_in;
            idat_left -= consumed_in;
            rawfill   += (scanline - rawfill) - zs.avail_out;  /* produced */
            if (r == Z_STREAM_END) { stream_ended = 1; break; }
            if (r == Z_DATA_ERROR || r == Z_VERSION_ERROR) goto done;
            if (r == Z_BUF_ERROR) {
                /* no progress: if we have output space and no input and stream
                 * not ended, that's truncation -> bail. */
                if (zs.avail_in == 0 && stream_ended && zs.avail_out > 0) goto done;
                /* otherwise loop to refill/continue */
            }
        }
        if (rawfill < scanline) goto done;   /* truncated stream */

        /* Reconstruct the filter into cur (in place — left-to-right, reads
         * only already-recon bytes (i-bpp) and the separate prev row). */
        int ftype = rawbuf[0];
        if (ftype > 4) goto done;            /* bad filter byte */
        for (size_t i = 0; i < row_bytes; i++) {
            int x = rawbuf[1 + i];
            int a = (i >= (size_t)bpp) ? cur[i - bpp] : 0;
            int b = prev[i];
            int c = (i >= (size_t)bpp) ? prev[i - bpp] : 0;
            int v;
            switch (ftype) {
                case 0: v = x;                  break;  /* None */
                case 1: v = x + a;               break;  /* Sub */
                case 2: v = x + b;               break;  /* Up */
                case 3: v = x + ((a + b) >> 1);  break;  /* Average */
                default: v = x + paeth(a, b, c); break;  /* Paeth (4) */
            }
            cur[i] = (uint8_t)(v & 0xFF);
        }

        /* Downsample into dest row(s) that map to this source row, exactly as
         * cover.c's decode_cover_to does for libjpeg. */
        for (int dy = 0; dy < px; dy++) {
            int sy = dy * oh / px;
            if (sy >= oh) sy = oh - 1;
            if (sy != y) continue;
            uint16_t *drow = out + (size_t)dy * px;
            for (int dx = 0; dx < px; dx++) {
                int sx = dx * ow / px;
                if (sx >= ow) sx = ow - 1;
                uint8_t *p = cur + (size_t)sx * bpp;
                uint8_t r, g, b;
                if (ch == 3) { r = p[0]; g = p[1]; b = p[2]; }       /* RGB */
                else if (ch == 4) { r = p[0]; g = p[1]; b = p[2]; }  /* RGBA, a dropped */
                else if (ch == 2) { r = g = b = p[0]; }             /* gray+a, a dropped */
                else            { r = g = b = p[0]; }               /* gray */
                drow[dx] = rgb888_to_565(r, g, b);
            }
        }

        /* swap prev/cur: next row's "up" is this reconstructed row */
        uint8_t *t = prev; prev = cur; cur = t;
        rawfill = 0;
    }

    rv = 1;   /* all rows decoded */

done:
    if (strm_inited) x_inflateEnd(&zs);
    free(rawbuf);
    free(prev);
    free(cur);
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return rv;
}