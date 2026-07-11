#ifndef R1_AB_FONT_H
#define R1_AB_FONT_H

#include "fb.h"

typedef struct font_context {
    uint8_t glyph_w;
    uint8_t glyph_h;
} font_context;

int font_open(font_context *font, const char *path);
void font_draw_text(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text);
int font_text_width(const font_context *font, const char *text);

#endif

