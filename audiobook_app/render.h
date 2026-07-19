/* render.h — RGB565 framebuffer renderer with bitmap font.
 *
 * Draws to hiby_player's mmap'd framebuffer (480x800 RGB565, stride 960).
 * Includes an embedded 8x12 ASCII bitmap font (printable 32-126).
 */

#ifndef AUDIOBOOK_RENDER_H
#define AUDIOBOOK_RENDER_H

#include <stdint.h>

/* ---- FB geometry (from smoke test + memory) ---------------------------- */
#define RENDER_FB_W      480
#define RENDER_FB_H      800
#define RENDER_STRIDE    960   /* bytes per line */
#define RENDER_BUF_SIZE  (RENDER_STRIDE * RENDER_FB_H)  /* 768000 */

/* ---- Colors (RGB565) --------------------------------------------------- */
/* Palette matched to the HiBy R1 system launcher: black background, white
 * text, gray chrome (#5A5559), and the device's focus blue (#1062f2). */
#define COL_BLACK     0x0000
#define COL_WHITE     0xFFFF
#define COL_RED       0xF800
#define COL_GREEN     0x07E0
#define COL_BLUE      0x001F
#define COL_YELLOW    0xFFE0
#define COL_CYAN      0x07FF
#define COL_MAGENTA   0xF81F
#define COL_GRAY_DK   0x4208  /* ~33% gray */
#define COL_GRAY      0x8410  /* ~50% gray (secondary text) */
#define COL_GRAY_LT   0xC618  /* ~75% gray */
#define COL_ORANGE    0xFD20
#define COL_BG        0x0000  /* background: black (#000000) */
#define COL_TEXT      0xFFFF  /* text: white (#FFFFFF) */
#define COL_ACCENT    0x131E  /* accent/focus blue (#1062f2) */
#define COL_DIVIDER   0x5AAB  /* thin divider gray (#5A5559, system chrome) */
#define COL_ITEM_BG   0x0000  /* list item background: black (no fill) */
#define COL_ITEM_SEL  0x1A2B  /* selected row: dim blue tint */

/* ---- Font sizes -------------------------------------------------------- */
/* FONT_SCALE_* are size INDICES (kept for call-site stability); the truetype
 * renderer maps them to pixel heights 18/24/32 via font_px_for_scale(). The
 * 8x12 bitmap fallback (when msyh.ttf is unavailable) still treats these as
 * pixel-repetition multipliers (8x12 / 16x24 / 24x36). */
#define FONT_W        8
#define FONT_H        12
#define FONT_SCALE_1  1   /* truetype 28px / bitmap 8x12  (small/secondary) */
#define FONT_SCALE_2  2   /* truetype 44px / bitmap 16x24 (primary body)   */
#define FONT_SCALE_3  3   /* truetype 64px / bitmap 24x36 (hero)           */
#define FONT_SCALE_4  4   /* truetype 36px / bitmap 32x48 (button labels)  */

/* ---- Renderer context -------------------------------------------------- */
typedef struct {
    uint16_t *fb;       /* framebuffer base pointer */
    int fb_fd;          /* framebuffer fd (for panning), -1 if unknown */
    int pan_yoffset;     /* current pan y-offset (0 or RENDER_FB_H) */
    int needs_redraw;   /* set when screen needs re-rendering */
} renderer_t;

/* Initialize renderer with hiby_player's fb pointer + fd. */
void renderer_init(renderer_t *r, uint16_t *fb, int fb_fd);

/* Clear the entire framebuffer (both buffers). */
void render_clear(renderer_t *r);

/* Fill a rectangle. */
void render_fill_rect(renderer_t *r, int x, int y, int w, int h,
                      uint16_t color);

/* Draw a 1px rectangle outline. */
void render_draw_rect(renderer_t *r, int x, int y, int w, int h,
                      uint16_t color);

/* Draw a horizontal divider line (1px tall, w wide) at y. */
void render_draw_hline(renderer_t *r, int x, int y, int w, uint16_t color);

/* Draw a filled circle centered at (cx, cy) with the given radius. Used for
 * the draggable progress handle on Now Playing. Clips to the screen. */
void render_fill_circle(renderer_t *r, int cx, int cy, int rad, uint16_t color);

/* Draw a single character at (x, y) with given scale and color. */
void render_char(renderer_t *r, int x, int y, char c, int scale,
                 uint16_t color);

/* Draw a string at (x, y) with given scale and color. Does not wrap.
 * Returns the x position after the last character. */
int render_text(renderer_t *r, int x, int y, const char *s, int scale,
                uint16_t color);

/* Draw text centered in a w-wide region at y. */
void render_text_centered(renderer_t *r, int x, int y, int w,
                           const char *s, int scale, uint16_t color);

/* Draw text right-aligned at (x_right, y). */
void render_text_right(renderer_t *r, int x_right, int y,
                       const char *s, int scale, uint16_t color);

/* Draw wrapped text within a box. Returns the y after the last line. */
int render_text_wrap(renderer_t *r, int x, int y, int w, int max_lines,
                     const char *s, int scale, uint16_t color);

/* Format and draw a time string "H:MM:SS" at (x, y). */
void render_time(renderer_t *r, int x, int y, int64_t ms, int scale,
                 uint16_t color);

/* Draw a horizontal progress bar. */
void render_progress_bar(renderer_t *r, int x, int y, int w, int h,
                         double fraction, uint16_t fill_color,
                         uint16_t bg_color);

/* Nearest-neighbor scaled blit of an RGB565 source (sw x sh pixels, row-major)
 * into a dw x dh rectangle at (dx, dy). src is a packed uint16_t buffer
 * (sw*sh entries). Clips to the screen. Used for cover art on Now Playing. */
void render_blit_rgb565(renderer_t *r, int dx, int dy, int dw, int dh,
                        const uint16_t *src, int sw, int sh);

/* Pan to the buffer we just drew to (flip display). */
void render_flip(renderer_t *r);

/* Get text width for a string at given scale. */
int render_text_width(const char *s, int scale);

#endif /* AUDIOBOOK_RENDER_H */