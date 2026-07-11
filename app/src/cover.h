#ifndef R1_AB_COVER_H
#define R1_AB_COVER_H

#include "db.h"
#include "fb.h"

typedef struct cover_art {
    uint16_t color;
    bool loaded;
} cover_art;

int cover_load_for_book(cover_art *cover, const book_row *book);
void cover_draw_placeholder(fb_context *fb, const cover_art *cover, int x, int y, int w, int h);

#endif

