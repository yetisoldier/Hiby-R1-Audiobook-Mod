#include "font.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

int font_open(font_context *font, const char *path) {
    (void)path;
    if (!font) return -1;
    font->glyph_w = 6;
    font->glyph_h = 9;
    return 0;
}

static void draw_char(fb_context *fb, int x, int y, uint16_t color, char ch) {
    static const uint8_t font5x7[96][7] = {{0}};
    unsigned idx = (ch >= 32 && ch <= 126) ? (unsigned)(ch - 32) : (unsigned)('?' - 32);
    const uint8_t *rows = font5x7[idx < 96 ? idx : 0];
    for (int yy = 0; yy < 7; yy++) {
        uint8_t bits = rows[yy];
        for (int xx = 0; xx < 5; xx++) {
            if (bits & (1u << (4 - xx))) {
                int px = x + xx;
                int py = y + yy;
                if (px >= 0 && py >= 0 && px < (int)fb->width && py < (int)fb->height) {
                    fb->pixels[(size_t)py * fb->stride + (size_t)px] = color;
                }
            }
        }
    }
}

void font_draw_text(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text) {
    if (!fb || !font || !text) return;
    int pen = x;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            pen = x;
            y += font->glyph_h;
            continue;
        }
        draw_char(fb, pen, y, color, *p);
        pen += font->glyph_w;
    }
}

int font_text_width(const font_context *font, const char *text) {
    if (!font || !text) return 0;
    int w = 0, cur = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            if (cur > w) w = cur;
            cur = 0;
            continue;
        }
        cur += font->glyph_w;
    }
    if (cur > w) w = cur;
    return w;
}

