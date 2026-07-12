#include "cover.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

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

static void cover_reset(cover_art *cover) {
    if (!cover) return;
    free(cover->pixels);
    cover->pixels = NULL;
    cover->width = 0;
    cover->height = 0;
    cover->source_path[0] = '\0';
}

static bool try_load_image(cover_art *cover, const char *path) {
    int w = 0, h = 0, n = 0;
    unsigned char *data = stbi_load(path, &w, &h, &n, 4);
    if (!data || w <= 0 || h <= 0) return false;
    cover->pixels = ab_xcalloc((size_t)w * (size_t)h, sizeof(uint16_t));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char *p = data + ((y * w + x) * 4);
            cover->pixels[y * w + x] = rgb888_to_rgb565(p[0], p[1], p[2]);
        }
    }
    stbi_image_free(data);
    cover->width = w;
    cover->height = h;
    ab_copy_str(cover->source_path, sizeof(cover->source_path), path);
    return true;
}

int cover_load_for_book(cover_art *cover, const book_row *book) {
    if (!cover || !book) return -1;
    cover_close(cover);
    cover->color = hash_color(book->book_key[0] ? book->book_key : book->title);
    cover->loaded = true;

    char candidate[512];
    if (book->cover_path[0] && try_load_image(cover, book->cover_path)) return 0;
    if (book->cover_cache_path[0] && try_load_image(cover, book->cover_cache_path)) return 0;
    if (book->root_path[0]) {
        ab_join_path(candidate, sizeof(candidate), book->root_path, "cover.jpg");
        if (try_load_image(cover, candidate)) return 0;
        ab_join_path(candidate, sizeof(candidate), book->root_path, "folder.jpg");
        if (try_load_image(cover, candidate)) return 0;
        ab_join_path(candidate, sizeof(candidate), book->root_path, "cover.png");
        if (try_load_image(cover, candidate)) return 0;
        ab_join_path(candidate, sizeof(candidate), book->root_path, "folder.png");
        if (try_load_image(cover, candidate)) return 0;
    }
    return 0;
}

void cover_close(cover_art *cover) {
    if (!cover) return;
    cover_reset(cover);
    cover->color = 0;
    cover->loaded = false;
}

void cover_draw_placeholder(fb_context *fb, const cover_art *cover, int x, int y, int w, int h) {
    if (!fb || !cover) return;
    fb_fill_rect(fb, x, y, w, h, cover->color);
}

static inline uint16_t sample_cover(const cover_art *cover, int px, int py) {
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px >= cover->width) px = cover->width - 1;
    if (py >= cover->height) py = cover->height - 1;
    return cover->pixels[py * cover->width + px];
}

void cover_draw_scaled(fb_context *fb, const cover_art *cover, int x, int y, int w, int h) {
    if (!fb || !cover) return;
    if (!cover->pixels || cover->width <= 0 || cover->height <= 0) {
        cover_draw_placeholder(fb, cover, x, y, w, h);
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)fb->width || y >= (int)fb->height) return;
    if (x + w > (int)fb->width) w = (int)fb->width - x;
    if (y + h > (int)fb->height) h = (int)fb->height - y;
    if (w <= 0 || h <= 0) return;

    for (int yy = 0; yy < h; yy++) {
        int src_y = (yy * cover->height + h / 2) / h;
        uint16_t *dst_row = fb->pixels + (size_t)(y + yy) * fb->stride;
        for (int xx = 0; xx < w; xx++) {
            int src_x = (xx * cover->width + w / 2) / w;
            dst_row[x + xx] = sample_cover(cover, src_x, src_y);
        }
    }
}
