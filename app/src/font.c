#include "font.h"
#include "common.h"
#include "fb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void rgb565_to_rgb888(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (uint8_t)((c >> 11) << 3);
    *g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    *b = (uint8_t)((c & 0x1F) << 3);
}

static inline uint16_t blend_rgb565(uint16_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t alpha) {
    uint8_t dr, dg, db;
    rgb565_to_rgb888(dst, &dr, &dg, &db);
    uint8_t a = alpha;
    uint8_t ia = 255u - a;
    uint8_t r = (uint8_t)((sr * a + dr * ia) / 255u);
    uint8_t g = (uint8_t)((sg * a + dg * ia) / 255u);
    uint8_t b = (uint8_t)((sb * a + db * ia) / 255u);
    return rgb888_to_rgb565(r, g, b);
}

static int try_load_font(font_context *font, const char *path) {
    if (!font || !path) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long len = ftell(fp);
    if (len <= 0) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }
    unsigned char *data = ab_xcalloc((size_t)len, 1);
    if (fread(data, 1, (size_t)len, fp) != (size_t)len) {
        free(data); fclose(fp); return -1;
    }
    fclose(fp);

    stbtt_fontinfo *info = ab_xcalloc(1, sizeof(stbtt_fontinfo));
    if (!stbtt_InitFont(info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        free(info); free(data); return -1;
    }

    font->ttf_data = data;
    font->ttf_len = len;
    font->stbinfo = info;
    font->body_size = AB_FONT_BODY_PT;
    font->focus_size = AB_FONT_FOCUS_PT;
    font->body_scale = stbtt_ScaleForPixelHeight(info, (float)font->body_size);
    font->focus_scale = stbtt_ScaleForPixelHeight(info, (float)font->focus_size);
    int a, d, lg;
    stbtt_GetFontVMetrics(info, &a, &d, &lg);
    font->ascent = (int)(a * font->body_scale);
    font->descent = (int)(d * font->body_scale);
    font->line_gap = (int)(lg * font->body_scale);
    return 0;
}

int font_open(font_context *font, const char *path) {
    if (!font || !path || !path[0]) return -1;
    memset(font, 0, sizeof(*font));

    /* Device-native font preferred, then the overlay package copy. */
    static const char *const FALLBACKS[] = {
        "/usr/resource/fonts/msyh.ttf",
        "/usr/share/audiobooks/fonts/msyh.ttf",
        "/usr/resource/fonts/default.otf",
    };

    const char *candidate = path;
    for (size_t i = 0; i < sizeof(FALLBACKS) / sizeof(FALLBACKS[0]) + 1; i++) {
        if (try_load_font(font, candidate) == 0) {
            fprintf(stderr, "[font] loaded %s\n", candidate);
            return 0;
        }
        if (i < sizeof(FALLBACKS) / sizeof(FALLBACKS[0])) {
            candidate = FALLBACKS[i];
        }
    }
    fprintf(stderr, "[font] failed to load %s and all fallbacks\n", path);
    return -1;
}

void font_close(font_context *font) {
    if (!font) return;
    free(font->stbinfo);
    free(font->ttf_data);
    memset(font, 0, sizeof(*font));
}

static int utf8_decode(const char **p) {
    unsigned char c = (unsigned char)(*p)[0];
    if (c < 0x80) { (*p)++; return c; }
    if ((c & 0xE0) == 0xC0) {
        int v = (c & 0x1F) << 6;
        (*p)++;
        v |= ((unsigned char)(*p)[0] & 0x3F);
        (*p)++;
        return v;
    }
    if ((c & 0xF0) == 0xE0) {
        int v = (c & 0x0F) << 12;
        (*p)++;
        v |= ((unsigned char)(*p)[0] & 0x3F) << 6;
        (*p)++;
        v |= ((unsigned char)(*p)[0] & 0x3F);
        (*p)++;
        return v;
    }
    if ((c & 0xF8) == 0xF0) {
        int v = (c & 0x07) << 18;
        (*p)++;
        v |= ((unsigned char)(*p)[0] & 0x3F) << 12;
        (*p)++;
        v |= ((unsigned char)(*p)[0] & 0x3F) << 6;
        (*p)++;
        v |= ((unsigned char)(*p)[0] & 0x3F);
        (*p)++;
        return v;
    }
    (*p)++;
    return 0xFFFD;
}

static int text_width_internal(const font_context *font, const char *text, float scale) {
    if (!font || !font->stbinfo || !text) return 0;
    const char *p = text;
    int width = 0;
    int prev_codepoint = -1;
    while (*p) {
        const char *prev_p = p;
        int ch = utf8_decode(&p);
        if (ch < 0) { p = prev_p + 1; continue; }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font->stbinfo, ch, &adv, &lsb);
        width += (int)(adv * scale);
        if (prev_codepoint >= 0) {
            width += (int)(stbtt_GetCodepointKernAdvance(font->stbinfo, prev_codepoint, ch) * scale);
        }
        prev_codepoint = ch;
    }
    return width;
}

int font_text_width(const font_context *font, const char *text) {
    if (!font) return 0;
    return text_width_internal(font, text, font->body_scale);
}

static void draw_codepoint_aa(fb_context *fb, stbtt_fontinfo *info, int codepoint, float scale, int x, int y, uint16_t color) {
    int w, h, xoff, yoff;
    unsigned char *bmp = stbtt_GetCodepointBitmap(info, scale, scale, codepoint, &w, &h, &xoff, &yoff);
    if (!bmp) return;
    uint8_t r, g, b;
    rgb565_to_rgb888(color, &r, &g, &b);
    int start_x = x + xoff;
    int start_y = y + yoff;
    for (int yy = 0; yy < h; yy++) {
        int py = start_y + yy;
        if (py < 0 || py >= (int)fb->height) continue;
        uint16_t *row = fb->pixels + (size_t)py * fb->stride;
        for (int xx = 0; xx < w; xx++) {
            int px = start_x + xx;
            if (px < 0 || px >= (int)fb->width) continue;
            uint8_t alpha = bmp[yy * w + xx];
            if (alpha == 0) continue;
            row[px] = blend_rgb565(row[px], r, g, b, alpha);
        }
    }
    stbtt_FreeBitmap(bmp, NULL);
}

static void draw_text_internal(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text, float scale) {
    if (!fb || !font || !font->stbinfo || !text) return;
    int pen_x = x;
    int pen_y = y + font->ascent;
    int prev_codepoint = -1;
    const char *p = text;
    while (*p) {
        if (*p == '\n') {
            pen_x = x;
            pen_y += font->ascent - font->descent + font->line_gap;
            p++;
            prev_codepoint = -1;
            continue;
        }
        const char *prev_p = p;
        int ch = utf8_decode(&p);
        if (ch < 0) { p = prev_p + 1; continue; }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font->stbinfo, ch, &adv, &lsb);
        if (prev_codepoint >= 0) {
            pen_x += (int)(stbtt_GetCodepointKernAdvance(font->stbinfo, prev_codepoint, ch) * scale);
        }
        draw_codepoint_aa(fb, font->stbinfo, ch, scale, pen_x, pen_y, color);
        pen_x += (int)(adv * scale);
        prev_codepoint = ch;
    }
}

void font_draw_text(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text) {
    if (!font) return;
    draw_text_internal(fb, font, x, y, color, text, font->body_scale);
}

void font_draw_text_focus(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text) {
    if (!font) return;
    draw_text_internal(fb, font, x, y, color, text, font->focus_scale);
}
