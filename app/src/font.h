#ifndef R1_AB_FONT_H
#define R1_AB_FONT_H

#include "fb.h"

#include <stdbool.h>

#define AB_FONT_BODY_PT 18
#define AB_FONT_FOCUS_PT 22

typedef struct font_context {
    unsigned char *ttf_data;
    long ttf_len;
    int body_size;
    int focus_size;
    float body_scale;
    float focus_scale;
    int ascent;
    int descent;
    int line_gap;
    void *stbinfo; /* opaque pointer to stbtt_fontinfo (header in .c) */
} font_context;

int font_open(font_context *font, const char *path);
void font_close(font_context *font);
void font_draw_text(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text);
int font_text_width(const font_context *font, const char *text);
void font_draw_text_focus(fb_context *fb, const font_context *font, int x, int y, uint16_t color, const char *text);

#endif

