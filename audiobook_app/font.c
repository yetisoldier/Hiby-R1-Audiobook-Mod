/* font.c — truetype font renderer for the audiobook UI.
 *
 * Loads the system font /usr/resource/fonts/msyh.ttf at runtime (not embedded),
 * rasterizes glyphs into a bounded LRU cache, and alpha-blends them onto an
 * RGB565 framebuffer. Public API in font.h.
 *
 * The cache is keyed by (size, codepoint) so ANY Unicode codepoint the font
 * has a glyph for (including Cyrillic U+0400..U+04FF, which msyh has) renders
 * correctly. Text paths (font_draw_text / font_text_width) decode UTF-8 and
 * feed codepoints to the rasterizer; the old ASCII-only clamp is gone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"

#include "font.h"
#include "render.h"   /* RENDER_FB_W/H, RENDER_STRIDE geometry */
#include "utf8.h"

#define FONT_PATH  "/usr/resource/fonts/msyh.ttf"

#define N_SIZES   4
static const int g_px[N_SIZES] = { FONT_PX_1, FONT_PX_2, FONT_PX_3, FONT_PX_4 };

/* Bounded glyph cache. A flat array with linear-scan lookup + LRU eviction.
 * Cap 512 holds the ASCII working set across all 4 sizes with headroom for
 * ~100 Cyrillic glyphs. Memory: ~32 B struct + alpha bitmap per entry; worst
 * case ~1 MB across the cache, far less in practice. Misses are rare (each
 * (size, codepoint) rasterized once ever), so the linear scan cost is
 * negligible on the MIPS render thread. */
#define GLYPH_CACHE_CAP 512

typedef struct {
    unsigned char *bmp;  /* alpha bitmap, w*h bytes; NULL if glyph is empty */
    int w, h;
    int xoff, yoff;       /* offset from pen (x, baseline+yoff = bmp top) */
    int advance;          /* x advance in pixels */
    int cached;           /* 1 once rasterized (so empty glyphs aren't re-rasterized) */
} glyph_t;

typedef struct {
    uint32_t key;     /* (size_index << 21) | codepoint; 0 == empty slot */
    glyph_t   g;
    uint32_t  lru;    /* tick stamp for LRU eviction */
} centry_t;

static stbtt_fontinfo g_info;
static unsigned char *g_ttf_data = NULL;
static int g_ok = 0;

static centry_t g_cache[GLYPH_CACHE_CAP];
static int      g_count = 0;
static uint32_t g_tick  = 0;

static int size_index(int px) {
    for (int i = 0; i < N_SIZES; i++) if (g_px[i] == px) return i;
    return -1;
}

int font_init(void) {
    int fd = open(FONT_PATH, O_RDONLY);
    if (fd < 0) { return 0; }
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz <= 0) { close(fd); return 0; }
    lseek(fd, 0, SEEK_SET);
    g_ttf_data = (unsigned char *)malloc((size_t)sz);
    if (!g_ttf_data) { close(fd); return 0; }
    ssize_t n = 0;
    while (n < sz) {
        ssize_t r = read(fd, g_ttf_data + n, (size_t)(sz - n));
        if (r <= 0) break;
        n += r;
    }
    close(fd);
    if (n != sz) { free(g_ttf_data); g_ttf_data = NULL; return 0; }

    if (!stbtt_InitFont(&g_info, g_ttf_data,
                       stbtt_GetFontOffsetForIndex(g_ttf_data, 0))) {
        free(g_ttf_data); g_ttf_data = NULL; return 0;
    }
    g_ok = 1;
    return 1;
}

int font_available(void) { return g_ok; }

int font_px_for_scale(int scale) {
    if (scale < 1 || scale > N_SIZES) return FONT_PX_1;
    return g_px[scale - 1];
}

/* Rasterize a glyph into the cache if not already present. Returns pointer or NULL. */
static glyph_t *get_glyph(int si, uint32_t cp) {
    if (!g_ok) return NULL;
    if (cp < 32) cp = ' ';   /* control chars -> space */
    uint32_t key = ((uint32_t)si << 21) | cp;

    /* lookup */
    for (int i = 0; i < GLYPH_CACHE_CAP; i++) {
        if (g_cache[i].key == key) {
            g_cache[i].lru = ++g_tick;
            return &g_cache[i].g;
        }
    }

    /* miss: find a slot — first empty, else LRU victim */
    int slot = -1;
    for (int i = 0; i < GLYPH_CACHE_CAP; i++) {
        if (g_cache[i].key == 0) { slot = i; break; }
    }
    if (slot < 0) {
        uint32_t mn = 0xFFFFFFFFu;
        for (int i = 0; i < GLYPH_CACHE_CAP; i++) {
            if (g_cache[i].lru < mn) { mn = g_cache[i].lru; slot = i; }
        }
        if (g_cache[slot].g.bmp) stbtt_FreeBitmap(g_cache[slot].g.bmp, NULL);
        memset(&g_cache[slot].g, 0, sizeof(glyph_t));
        g_cache[slot].key = 0;
        g_cache[slot].lru = 0;
        g_count--;
    }

    centry_t *e = &g_cache[slot];
    int px = g_px[si];
    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)px);
    int xoff, yoff;
    /* stbtt_GetCodepointBitmap returns alpha bitmap; yoff is from baseline.
     * For glyphs the font lacks it returns NULL with w=h=0 — that's fine, we
     * still mark cached so we don't re-rasterize every frame. msyh covers
     * Cyrillic U+0400..U+04FF and U+2026 ellipsis. */
    e->g.bmp = stbtt_GetCodepointBitmap(&g_info, scale, scale, (int)cp,
                                        &e->g.w, &e->g.h, &xoff, &yoff);
    e->g.xoff = xoff;
    e->g.yoff = yoff;
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&g_info, (int)cp, &advance, &lsb);
    e->g.advance = (int)(advance * scale);
    if (e->g.advance <= 0) e->g.advance = (e->g.w + e->g.xoff > 0) ? (e->g.w + e->g.xoff) : (px / 3);
    e->g.cached = 1;
    e->key = key;
    e->lru = ++g_tick;
    g_count++;
    return &e->g;
}

static inline void blend(uint16_t *fb, int x, int y, uint16_t color, int a) {
    if (a <= 0) return;
    if (x < 0 || x >= RENDER_FB_W || y < 0 || y >= RENDER_FB_H) return;
    int stride_px = RENDER_STRIDE / 2;
    uint16_t dst = fb[y * stride_px + x];
    int dr = (dst >> 11) & 0x1f, dg = (dst >> 5) & 0x3f, db = dst & 0x1f;
    int cr = (color >> 11) & 0x1f, cg = (color >> 5) & 0x3f, cb = color & 0x1f;
    int ia = 255 - a;
    int r = (cr * a + dr * ia) / 255;
    int g = (cg * a + dg * ia) / 255;
    int b = (cb * a + db * ia) / 255;
    fb[y * stride_px + x] = (uint16_t)((r << 11) | (g << 5) | b);
}

int font_draw_codepoint(uint16_t *fb, int x, int y, uint32_t cp, int px,
                        uint16_t color) {
    if (!g_ok) return x;
    int si = size_index(px);
    if (si < 0) si = 0;
    glyph_t *g = get_glyph(si, cp);
    if (!g) return x;
    if (!g->bmp) return x + g->advance;

    int asc = font_ascent(px);
    int base_y = y + asc;        /* baseline */
    int draw_x = x + g->xoff;
    int draw_y = base_y + g->yoff;
    for (int j = 0; j < g->h; j++) {
        for (int i = 0; i < g->w; i++) {
            int a = g->bmp[j * g->w + i];
            blend(fb, draw_x + i, draw_y + j, color, a);
        }
    }
    return x + g->advance;
}

int font_draw_char(uint16_t *fb, int x, int y, char c, int px, uint16_t color) {
    unsigned char b = (unsigned char)c;
    if (b < 0x80) return font_draw_codepoint(fb, x, y, b, px, color);
    /* stray non-ASCII byte via the legacy single-char API: callers should
     * use font_draw_text for UTF-8. Advance without drawing. */
    return x + (px > 3 ? px / 3 : 1);
}

int font_draw_text(uint16_t *fb, int x, int y, const char *s, int px,
                   uint16_t color) {
    if (!s) return x;
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += font_line_height(px); s++; continue; }
        uint32_t cp; int adv;
        if (utf8_decode(s, INT_MAX, &cp, &adv) == 0) {
            cx = font_draw_codepoint(fb, cx, y, cp, px, color);
            s += adv;
        } else {
            s++;  /* skip invalid byte */
        }
    }
    return cx;
}

int font_codepoint_width(uint32_t cp, int px) {
    if (!g_ok) return px / 2;
    int si = size_index(px);
    if (si < 0) si = 0;
    glyph_t *g = get_glyph(si, cp);
    return g ? g->advance : (px / 2);
}

int font_text_width(const char *s, int px) {
    if (!s) return 0;
    int si = size_index(px);
    if (si < 0) si = 0;
    int w = 0;
    while (*s) {
        if (*s == '\n') { s++; continue; }
        uint32_t cp; int adv;
        if (utf8_decode(s, INT_MAX, &cp, &adv) == 0) {
            glyph_t *g = get_glyph(si, cp);
            w += g ? g->advance : (px / 2);
            s += adv;
        } else {
            w += px / 2;
            s++;
        }
    }
    return w;
}

int font_ascent(int px) {
    if (!g_ok) return px;
    int si = size_index(px);
    if (si < 0) si = 0;
    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)g_px[si]);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &linegap);
    return (int)(ascent * scale + 0.5f);
}

int font_line_height(int px) {
    if (!g_ok) return px;
    int si = size_index(px);
    if (si < 0) si = 0;
    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)g_px[si]);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &linegap);
    return (int)((ascent + descent) * scale + 0.5f);
}