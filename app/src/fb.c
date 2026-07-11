#include "fb.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

int fb_open(fb_context *fb, const char *path) {
    if (!fb || !path) return -1;
    memset(fb, 0, sizeof(*fb));
    fb->fd = open(path, O_RDWR | O_CLOEXEC);
    fb->width = AB_SCREEN_W;
    fb->height = AB_SCREEN_H;
    fb->stride = AB_SCREEN_W;
    fb->pixels_len = (size_t)fb->stride * fb->height;
    fb->pixels = ab_xcalloc(fb->pixels_len, sizeof(uint16_t));
    if (fb->fd < 0) return 0;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) != 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
        close(fb->fd);
        fb->fd = -1;
        return 0;
    }
    fb->width = vinfo.xres;
    fb->height = vinfo.yres;
    fb->stride = finfo.line_length / 2u;
    fb->pixels_len = (size_t)fb->stride * fb->height;
    free(fb->pixels);
    fb->pixels = ab_xcalloc(fb->pixels_len, sizeof(uint16_t));
    return 0;
}

void fb_close(fb_context *fb) {
    if (!fb) return;
    free(fb->pixels);
    if (fb->fd >= 0) close(fb->fd);
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
}

void fb_clear(fb_context *fb, uint16_t color) {
    if (!fb || !fb->pixels) return;
    for (size_t i = 0; i < fb->pixels_len; i++) fb->pixels[i] = color;
}

void fb_fill_rect(fb_context *fb, int x, int y, int w, int h, uint16_t color) {
    if (!fb || !fb->pixels || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)fb->width || y >= (int)fb->height) return;
    if (x + w > (int)fb->width) w = (int)fb->width - x;
    if (y + h > (int)fb->height) h = (int)fb->height - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = fb->pixels + (size_t)yy * fb->stride;
        for (int xx = x; xx < x + w; xx++) row[xx] = color;
    }
}

void fb_present(fb_context *fb) {
    if (!fb || fb->fd < 0 || !fb->pixels) return;
    size_t bytes = fb->pixels_len * sizeof(uint16_t);
    lseek(fb->fd, 0, SEEK_SET);
    if (write(fb->fd, fb->pixels, bytes) < 0) {
        /* Best-effort flush only. */
    }
}
