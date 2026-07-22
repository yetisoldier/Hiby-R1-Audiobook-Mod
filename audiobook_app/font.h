/* font.h — truetype font renderer (msyh.ttf via stb_truetype).
 *
 * Renders anti-aliased ASCII text from the system font
 * /usr/resource/fonts/msyh.ttf (Microsoft YaHei) into an RGB565 framebuffer.
 * Glyphs are cached per (pixel-size, codepoint) so repeated draws (the UI
 * redraws on every FBIOPAN_DISPLAY, ~30 fps) stay cheap.
 *
 * Coordinate convention matches render_char(): (x, y) is the TOP-LEFT of the
 * line box; the glyph is placed on a baseline at y + ascent. Text occupies
 * roughly [y, y + font_line_height(px)].
 *
 * If font_init() fails (font file missing/unreadable), font_available() is 0
 * and render.c falls back to the embedded 8x12 bitmap font.
 */

#ifndef AUDIOBOOK_FONT_H
#define AUDIOBOOK_FONT_H

#include <stdint.h>

/* Initialize: load + parse /usr/resource/fonts/msyh.ttf. Returns 1 on success. */
int font_init(void);

/* 1 if the truetype font loaded and is usable, 0 otherwise. */
int font_available(void);

/* Pixel sizes corresponding to render.h FONT_SCALE_1/2/3/4. Tuned large for
 * readability: msyh's Latin caps fill only ~70% of the EM box, so a 24 px
 * request renders ~17 px caps — smaller than the old 8x12 bitmap at scale 2.
 * These values (~2x the originals) give comfortable on-device text. */
#define FONT_PX_1  28
#define FONT_PX_2  44
#define FONT_PX_3  64
#define FONT_PX_4  36

/* Map a render scale (1/2/3) to a pixel size. */
int font_px_for_scale(int scale);

/* Draw one ASCII char at (x, y=top), alpha-blending color over the existing
 * framebuffer pixel. Returns the x advance (next pen x). Non-ASCII -> '?'. */
int font_draw_char(uint16_t *fb, int x, int y, char c, int px, uint16_t color);

/* Draw an ASCII string at (x, y=top). Returns the x after the last char. */
int font_draw_text(uint16_t *fb, int x, int y, const char *s, int px,
                   uint16_t color);

/* Pixel width of a string at the given size (sum of advances). */
int font_text_width(const char *s, int px);

/* Line height (ascent + descent) at the given size, for vertical spacing. */
int font_line_height(int px);

/* Ascent at the given size (top of line to baseline). */
int font_ascent(int px);

#endif /* AUDIOBOOK_FONT_H */