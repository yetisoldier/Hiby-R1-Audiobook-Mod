#ifndef R1_AB_FB_H
#define R1_AB_FB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct fb_context {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint16_t *pixels;
    size_t pixels_len;
} fb_context;

int fb_open(fb_context *fb, const char *path);
void fb_close(fb_context *fb);
void fb_clear(fb_context *fb, uint16_t color);
void fb_fill_rect(fb_context *fb, int x, int y, int w, int h, uint16_t color);
void fb_present(fb_context *fb);

#endif
