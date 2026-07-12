#ifndef R1_AB_COVER_H
#define R1_AB_COVER_H

#include "db.h"
#include "fb.h"

#include <stddef.h>
#include <stdint.h>

typedef struct cover_art {
    uint16_t color;
    bool loaded;
    uint16_t *pixels;
    int width;
    int height;
    char source_path[512];
} cover_art;

int cover_load_for_book(cover_art *cover, const book_row *book);
void cover_close(cover_art *cover);
void cover_draw_placeholder(fb_context *fb, const cover_art *cover, int x, int y, int w, int h);
void cover_draw_scaled(fb_context *fb, const cover_art *cover, int x, int y, int w, int h);

#endif
