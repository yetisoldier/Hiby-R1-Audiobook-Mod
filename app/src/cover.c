#include "cover.h"
#include "common.h"

#include <string.h>

static uint16_t hash_color(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    uint8_t r = (h >> 16) & 0x1f;
    uint8_t g = (h >> 8) & 0x3f;
    uint8_t b = h & 0x1f;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

int cover_load_for_book(cover_art *cover, const book_row *book) {
    if (!cover || !book) return -1;
    cover->color = hash_color(book->book_key[0] ? book->book_key : book->title);
    cover->loaded = true;
    return 0;
}

void cover_draw_placeholder(fb_context *fb, const cover_art *cover, int x, int y, int w, int h) {
    if (!fb || !cover) return;
    fb_fill_rect(fb, x, y, w, h, cover->color);
}

