#include "ui.h"
#include "common.h"
#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stb_image.h"

/* ------------------------------------------------------------------
 * Theme colors
 * ------------------------------------------------------------------ */
#define TH_BG               0xCE79u
#define TH_BG_WHITE         0xFFFFu
#define TH_TEXT_BLACK       0x0001u
#define TH_TEXT_WHITE       0xFFFFu
/* HiBy brand blue: 0x1062F2 (RGB888) -> RGB565 0x131E.
 * R=0x10 -> 0b00010 (5 bits), G=0x62 -> 0b011000 (6 bits), B=0xF2 -> 0b11110 (5 bits).
 */
#define TH_FOCUS_BLUE       0x131Eu
#define TH_SUBTITLE         0x6B6Du
#define TH_PROGRESS_BG      0xA534u
#define TH_PROGRESS_FILL    0x131Eu
#define TH_HEADER_BG        0x131Eu

#define ICON_SIZE           32
#define ROW_HEIGHT          140
#define ROW_THUMB_SIZE      96
#define MINI_H              96
#define MINI_THUMB_SIZE     64

/* Theme roots: prefer the stock device theme if present, fall back to our overlay. */
#define ASSET_DEVICE_ROOT  "/usr/resource/litegui/theme1"
#define ASSET_PACKAGE_ROOT "/usr/share/audiobooks/hiby-theme"

static bool asset_device_available(void) {
    return access(ASSET_DEVICE_ROOT "/playing_plane/btn_play.png", R_OK) == 0;
}

static void ui_theme_path(char *dst, size_t dst_len, const char *rel) {
    if (asset_device_available()) {
        ab_join_path(dst, dst_len, ASSET_DEVICE_ROOT "/playing_plane", rel);
    } else {
        ab_join_path(dst, dst_len, ASSET_PACKAGE_ROOT "/playing_plane", rel);
    }
}

static void ui_launcher_theme_path(char *dst, size_t dst_len, const char *rel) {
    if (asset_device_available()) {
        ab_join_path(dst, dst_len, ASSET_DEVICE_ROOT "/launcher", rel);
    } else {
        ab_join_path(dst, dst_len, ASSET_PACKAGE_ROOT "/launcher", rel);
    }
}

/* ------------------------------------------------------------------
 * Cached theme image helpers (defined in ui.h)
 * ------------------------------------------------------------------ */

static void theme_image_reset(theme_image *img) {
    if (!img) return;
    free(img->pixels);
    img->pixels = NULL;
    img->width = 0;
    img->height = 0;
    img->path[0] = '\0';
}

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void rgb565_to_rgb888(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (uint8_t)((c >> 11) << 3);
    *g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    *b = (uint8_t)((c & 0x1F) << 3);
}

static inline uint16_t alpha_blend565(uint16_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t alpha) {
    if (alpha == 255u) return rgb888_to_rgb565(sr, sg, sb);
    if (alpha == 0) return dst;
    uint8_t dr, dg, db;
    rgb565_to_rgb888(dst, &dr, &dg, &db);
    uint8_t a = alpha;
    uint8_t ia = 255u - a;
    uint8_t r = (uint8_t)((sr * a + dr * ia) / 255u);
    uint8_t g = (uint8_t)((sg * a + dg * ia) / 255u);
    uint8_t b = (uint8_t)((sb * a + db * ia) / 255u);
    return rgb888_to_rgb565(r, g, b);
}

static int load_theme_image(theme_image *img, const char *path) {
    if (!img || !path || !path[0]) return -1;
    if (img->pixels && strcmp(img->path, path) == 0) return 0;
    theme_image_reset(img);
    int w = 0, h = 0, n = 0;
    unsigned char *data = stbi_load(path, &w, &h, &n, 4);
    if (!data || w <= 0 || h <= 0) return -1;
    img->pixels = ab_xcalloc((size_t)w * (size_t)h, sizeof(uint16_t));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char *p = data + (y * w + x) * 4;
            img->pixels[y * w + x] = alpha_blend565(0, p[0], p[1], p[2], p[3]);
        }
    }
    stbi_image_free(data);
    img->width = w;
    img->height = h;
    ab_copy_str(img->path, sizeof(img->path), path);
    return 0;
}

static void blit_image_rgba(fb_context *fb, theme_image *img, int x, int y, int w, int h) {
    if (!fb || !img || !img->pixels) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)fb->width || y >= (int)fb->height) return;
    if (x + w > (int)fb->width) w = (int)fb->width - x;
    if (y + h > (int)fb->height) h = (int)fb->height - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        int src_y = (yy * img->height + h / 2) / h;
        if (src_y < 0 || src_y >= img->height) continue;
        uint16_t *src_row = img->pixels + (size_t)src_y * img->width;
        uint16_t *dst_row = fb->pixels + (size_t)(y + yy) * fb->stride;
        for (int xx = 0; xx < w; xx++) {
            int src_x = (xx * img->width + w / 2) / w;
            if (src_x < 0 || src_x >= img->width) continue;
            dst_row[x + xx] = src_row[src_x];
        }
    }
}

/* Static variant kept for possible future use but currently unused. */
#if 0
static void blit_image_rgba_bg(fb_context *fb, theme_image *img, int x, int y, int w, int h, uint16_t bg) {
    if (!fb || !img || !img->pixels) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)fb->width || y >= (int)fb->height) return;
    if (x + w > (int)fb->width) w = (int)fb->width - x;
    if (y + h > (int)fb->height) h = (int)fb->height - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        int src_y = (yy * img->height + h / 2) / h;
        if (src_y < 0 || src_y >= img->height) continue;
        uint16_t *src_row = img->pixels + (size_t)src_y * img->width;
        uint16_t *dst_row = fb->pixels + (size_t)(y + yy) * fb->stride;
        for (int xx = 0; xx < w; xx++) {
            int src_x = (xx * img->width + w / 2) / w;
            if (src_x < 0 || src_x >= img->width) continue;
            uint16_t src = src_row[src_x];
            /* Pre-multiplied: asset pixels were blended against black during load, so source alpha
             * is encoded as distance from 0x0000. Approximate alpha by treating RGB magnitude.
             * Simpler: treat src as already on black; blend with background using alpha inferred from
             * luma. We'll use the source RGB directly if non-zero, else bg. */
            uint8_t sr, sg, sb, dr, dg, db;
            rgb565_to_rgb888(src, &sr, &sg, &sb);
            rgb565_to_rgb888(bg, &dr, &dg, &db);
            uint8_t alpha = (sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb));
            if (alpha == 0) {
                dst_row[x + xx] = bg;
                continue;
            }
            uint8_t ia = 255u - alpha;
            uint8_t r = (uint8_t)((sr * alpha + dr * ia) / 255u);
            uint8_t g = (uint8_t)((sg * alpha + dg * ia) / 255u);
            uint8_t b = (uint8_t)((sb * alpha + db * ia) / 255u);
            dst_row[x + xx] = rgb888_to_rgb565(r, g, b);
        }
    }
}
#endif

static int ui_icon_button(ui_context *ui, const char *path, int x, int y, int w, int h) {
    theme_image *img = &ui->assets.icon;
    if (load_theme_image(img, path) != 0) {
        fb_fill_rect(&ui->fb, x, y, w, h, TH_FOCUS_BLUE);
        return -1;
    }
    blit_image_rgba(&ui->fb, img, x, y, w, h);
    return 0;
}

/* ------------------------------------------------------------------
 * Drawing helpers
 * ------------------------------------------------------------------ */
static void draw_header(ui_context *ui, const char *title) {
    fb_fill_rect(&ui->fb, 0, 0, AB_SCREEN_W, 100, TH_HEADER_BG);
    font_draw_text(&ui->fb, &ui->font, 18, 30, TH_TEXT_WHITE, title);
}

static void draw_hor_line(ui_context *ui, int y) {
    theme_image *img = &ui->assets.divider;
    char path[256];
    ui_launcher_theme_path(path, sizeof(path), "hor_line.png");
    if (load_theme_image(img, path) != 0) {
        fb_fill_rect(&ui->fb, 12, y, AB_SCREEN_W - 24, 2, 0xBDF7u);
        return;
    }
    /* Stretch the divider horizontally across the row width. */
    blit_image_rgba(&ui->fb, img, 12, y, AB_SCREEN_W - 24, img->height > 0 ? img->height : 2);
}

static void draw_book_row(ui_context *ui, int y, const book_row *book, bool highlighted) {
    uint16_t bg = highlighted ? TH_FOCUS_BLUE : TH_BG;
    fb_fill_rect(&ui->fb, 0, y, AB_SCREEN_W, ROW_HEIGHT, bg);
    cover_art tmp;
    memset(&tmp, 0, sizeof(tmp));
    cover_load_for_book(&tmp, book);
    int thumb_x = 12;
    int thumb_y = y + (ROW_HEIGHT - ROW_THUMB_SIZE) / 2;
    if (tmp.pixels && tmp.width > 0 && tmp.height > 0) {
        cover_draw_scaled(&ui->fb, &tmp, thumb_x, thumb_y, ROW_THUMB_SIZE, ROW_THUMB_SIZE);
    } else {
        cover_draw_placeholder(&ui->fb, &tmp, thumb_x, thumb_y, ROW_THUMB_SIZE, ROW_THUMB_SIZE);
    }
    cover_close(&tmp);
    int text_x = thumb_x + ROW_THUMB_SIZE + 14;
    uint16_t fg = highlighted ? TH_TEXT_WHITE : TH_TEXT_BLACK;
    font_draw_text(&ui->fb, &ui->font, text_x, y + 30, fg, book->title);
    if (book->author[0]) {
        font_draw_text(&ui->fb, &ui->font, text_x, y + 80, highlighted ? TH_TEXT_WHITE : TH_SUBTITLE, book->author);
    }
    draw_hor_line(ui, y + ROW_HEIGHT - 1);
}

static void draw_row_text(ui_context *ui, int y, const char *title, const char *subtitle, bool highlighted) {
    uint16_t bg = highlighted ? TH_FOCUS_BLUE : TH_BG;
    fb_fill_rect(&ui->fb, 0, y, AB_SCREEN_W, ROW_HEIGHT, bg);
    uint16_t fg = highlighted ? TH_TEXT_WHITE : TH_TEXT_BLACK;
    font_draw_text(&ui->fb, &ui->font, 24, y + 30, fg, title);
    if (subtitle && subtitle[0]) {
        font_draw_text(&ui->fb, &ui->font, 24, y + 80, highlighted ? TH_TEXT_WHITE : TH_SUBTITLE, subtitle);
    }
    draw_hor_line(ui, y + ROW_HEIGHT - 1);
}

static void draw_home_row(ui_context *ui, int y, const char *title, const char *subtitle) {
    fb_fill_rect(&ui->fb, 0, y, AB_SCREEN_W, ROW_HEIGHT, TH_BG);
    font_draw_text(&ui->fb, &ui->font, 24, y + 30, TH_TEXT_BLACK, title);
    if (subtitle && subtitle[0]) {
        font_draw_text(&ui->fb, &ui->font, 24, y + 80, TH_SUBTITLE, subtitle);
    }
    draw_hor_line(ui, y + ROW_HEIGHT - 1);
}

static void draw_mini_player(ui_context *ui, const audiobook_player *player) {
    if (!player || !player->book_loaded || ui->selected_book < 0 ||
        (size_t)ui->selected_book >= ui->books.count) {
        return;
    }
    const book_row *book = &ui->books.items[ui->selected_book];
    int y = AB_SCREEN_H - MINI_H;
    fb_fill_rect(&ui->fb, 0, y, AB_SCREEN_W, MINI_H, TH_TEXT_WHITE);
    cover_draw_scaled(&ui->fb, &ui->cover, 8, y + 8, MINI_THUMB_SIZE, MINI_THUMB_SIZE);
    font_draw_text(&ui->fb, &ui->font, MINI_THUMB_SIZE + 20, y + 20, TH_TEXT_BLACK, book->title);
    font_draw_text(&ui->fb, &ui->font, MINI_THUMB_SIZE + 20, y + 58, TH_SUBTITLE, player->state == PLAYER_PLAYING ? "Playing" : "Paused");

    char path[256];
    ui_theme_path(path, sizeof(path), player->state == PLAYER_PLAYING ? "btn_pause.png" : "btn_play.png");
    ui_icon_button(ui, path, AB_SCREEN_W - 72, y + 24, 48, 48);

    /* Treat mini-player area as interactive; rendered separately before present. */
}

static int find_book_index(const book_list *books, int64_t book_id) {
    if (!books) return -1;
    for (size_t i = 0; i < books->count; i++) {
        if (books->items[i].book_id == book_id) return (int)i;
    }
    return -1;
}

static uint64_t track_prefix_ms(const track_list *tracks, int track_ordinal) {
    if (!tracks || track_ordinal <= 1) return 0;
    size_t limit = (size_t)(track_ordinal - 1);
    if (limit > tracks->count) limit = tracks->count;
    uint64_t total = 0;
    for (size_t i = 0; i < limit; i++) {
        total += tracks->items[i].duration_ms > 0 ? (uint64_t)tracks->items[i].duration_ms : 0u;
    }
    return total;
}

static uint64_t track_duration_ms(const track_list *tracks, int track_ordinal) {
    if (!tracks || track_ordinal <= 0) return 0;
    size_t index = (size_t)(track_ordinal - 1);
    if (index >= tracks->count) return 0;
    return tracks->items[index].duration_ms > 0 ? (uint64_t)tracks->items[index].duration_ms : 0u;
}

static uint64_t resolve_resume_position_ms(const track_list *tracks, const progress_row *progress) {
    if (!tracks || !progress || progress->track_ordinal <= 0) return 0;
    size_t index = (size_t)(progress->track_ordinal - 1);
    if (index >= tracks->count) return 0;
    uint64_t local = progress->position_ms > 0 ? (uint64_t)progress->position_ms : 0u;
    uint64_t duration = track_duration_ms(tracks, progress->track_ordinal);
    if (duration > 0 && local > duration) {
        uint64_t absolute = progress->total_book_elapsed_ms > 0 ? (uint64_t)progress->total_book_elapsed_ms : local;
        uint64_t prefix = track_prefix_ms(tracks, progress->track_ordinal);
        local = absolute > prefix ? absolute - prefix : 0u;
    }
    if (duration > 0 && local > duration) local = duration;
    return local;
}

int ui_init(ui_context *ui, const audiobook_config *cfg) {
    if (!ui || !cfg) return -1;
    memset(ui, 0, sizeof(*ui));
    ui->cfg = *cfg;
    if (fb_open(&ui->fb, cfg->fb_path) != 0) return -1;
    if (font_open(&ui->font, cfg->font_path) != 0) {
        fb_close(&ui->fb);
        return -1;
    }
    if (touch_open(&ui->touch, cfg->touch_path) != 0) {
        font_close(&ui->font);
        fb_close(&ui->fb);
        return -1;
    }
    ui->screen = UI_SCREEN_HOME;
    return 0;
}

void ui_shutdown(ui_context *ui) {
    if (!ui) return;
    db_free_book_list(&ui->books);
    db_free_book_list(&ui->continue_books);
    db_free_book_list(&ui->finished_books);
    db_free_bookmark_list(&ui->bookmarks);
    db_free_track_list(&ui->tracks);
    db_free_track_list(&ui->chapters);
    db_free_string_list(&ui->authors);
    db_free_string_list(&ui->series);
    db_free_string_list(&ui->folders);
    cover_close(&ui->cover);
    theme_image_reset(&ui->assets.icon);
    theme_image_reset(&ui->assets.divider);
    font_close(&ui->font);
    fb_close(&ui->fb);
    touch_close(&ui->touch);
    memset(ui, 0, sizeof(*ui));
}

int ui_load_titles(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_book_list(&ui->books);
    return db_query_titles(db, &ui->books);
}

int ui_load_continue(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_book_list(&ui->continue_books);
    return db_query_continue(db, &ui->continue_books);
}

int ui_load_finished(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_book_list(&ui->finished_books);
    return db_query_finished(db, &ui->finished_books);
}

int ui_load_bookmarks(ui_context *ui, audiobook_db *db) {
    if (!ui || !db || ui->selected_book < 0 || (size_t)ui->selected_book >= ui->books.count) return -1;
    db_free_bookmark_list(&ui->bookmarks);
    return db_list_bookmarks(db, ui->books.items[ui->selected_book].book_id, &ui->bookmarks);
}

int ui_load_authors(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_string_list(&ui->authors);
    return db_query_authors(db, &ui->authors);
}

int ui_load_series(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_string_list(&ui->series);
    return db_query_series(db, &ui->series);
}

int ui_load_folders(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_string_list(&ui->folders);
    return db_query_folders(db, &ui->folders);
}

int ui_load_book(ui_context *ui, audiobook_db *db, int index) {
    if (!ui || !db || index < 0 || (size_t)index >= ui->books.count) return -1;
    db_free_track_list(&ui->tracks);
    book_row *book = &ui->books.items[index];
    ui->selected_book = index;
    if (db_query_tracks(db, book->book_id, &ui->tracks) != 0) return -1;
    db_free_track_list(&ui->chapters);
    if (db_query_chapters_display(db, book->book_id, &ui->chapters) != 0) {
        memset(&ui->chapters, 0, sizeof(ui->chapters));
    }
    ui->selected_track = 0;
    cover_load_for_book(&ui->cover, book);
    return 0;
}

static void draw_now_playing(ui_context *ui, const audiobook_player *player) {
    draw_header(ui, "Now Playing");

    /* Cover art on the left side of the screen */
    cover_draw_scaled(&ui->fb, &ui->cover, 20, 100, 180, 180);

    if (ui->selected_book < 0 || (size_t)ui->selected_book >= ui->books.count) return;

    book_row *book = &ui->books.items[ui->selected_book];
    font_draw_text(&ui->fb, &ui->font, 220, 120, TH_TEXT_BLACK, book->title);
    font_draw_text(&ui->fb, &ui->font, 220, 180, TH_SUBTITLE, book->author);
    if (ui->tracks.count > 0) {
        font_draw_text(&ui->fb, &ui->font, 220, 240, TH_TEXT_BLACK,
                       ui->tracks.items[ui->selected_track].title);
    }

    char status[64];
    uint64_t pos = player ? player->position_ms : 0;
    uint64_t dur = player ? player->duration_ms : 0;
    uint64_t remain = dur > pos ? dur - pos : 0;
    snprintf(status, sizeof(status), "%.2fx %llum left", player ? player->speed : 1.0f, (unsigned long long)remain);
    font_draw_text(&ui->fb, &ui->font, 220, 300, TH_TEXT_BLACK, status);

    /* Progress bar using theme PNGs if available, otherwise rectangles */
    char bg_path[256], fg_path[256];
    ui_theme_path(bg_path, sizeof(bg_path), "progress_bg.png");
    ui_theme_path(fg_path, sizeof(fg_path), "progress.png");
    int bar_x = 220, bar_y = 340, bar_w = 230, bar_h = 12;
    if (load_theme_image(&ui->assets.icon, bg_path) == 0) {
        blit_image_rgba(&ui->fb, &ui->assets.icon, bar_x, bar_y, bar_w, bar_h);
    } else {
        fb_fill_rect(&ui->fb, bar_x, bar_y, bar_w, bar_h, TH_PROGRESS_BG);
    }
    if (dur > 0) {
        int filled = (int)((uint64_t)bar_w * pos / dur);
        if (filled > 0) {
            if (load_theme_image(&ui->assets.icon, fg_path) == 0) {
                blit_image_rgba(&ui->fb, &ui->assets.icon, bar_x, bar_y, filled, bar_h);
            } else {
                fb_fill_rect(&ui->fb, bar_x, bar_y, filled, bar_h, TH_PROGRESS_FILL);
            }
        }
    }

    /* Playback controls using HiBy PNG assets */
    char path[256];
    ui_theme_path(path, sizeof(path), "btn_prev.png");
    ui_icon_button(ui, path, 20, 520, 96, 96);
    ui_theme_path(path, sizeof(path), player && player->state == PLAYER_PLAYING ? "btn_pause.png" : "btn_play.png");
    ui_icon_button(ui, path, 132, 520, 96, 96);
    ui_theme_path(path, sizeof(path), "btn_next.png");
    ui_icon_button(ui, path, 244, 520, 96, 96);
}

int ui_render(ui_context *ui, const audiobook_player *player) {
    if (!ui) return -1;
    fb_clear(&ui->fb, TH_BG);
    switch (ui->screen) {
    case UI_SCREEN_HOME:
        draw_header(ui, "Audiobooks");
        draw_home_row(ui, 100, "Continue Listening", "Open your current book");
        draw_home_row(ui, 100 + ROW_HEIGHT, "Titles", "All books in the library");
        draw_home_row(ui, 100 + ROW_HEIGHT * 2, "Finished", "Books you've completed");
        draw_home_row(ui, 100 + ROW_HEIGHT * 3, "Authors", "Browse by author");
        draw_home_row(ui, 100 + ROW_HEIGHT * 4, "Series", "Browse by series");
        draw_home_row(ui, 100 + ROW_HEIGHT * 5, "Folders", "Browse by library folder");
        draw_home_row(ui, 100 + ROW_HEIGHT * 6, "Settings", "Speed, sleep timer, refresh");
        if (player && player->book_loaded) {
            draw_mini_player(ui, player);
        }
        break;
    case UI_SCREEN_CONTINUE:
        draw_header(ui, "Continue");
        for (size_t i = 0; i < ui->continue_books.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "Last played %lld", (long long)ui->continue_books.items[i].last_played_at);
            draw_book_row(ui, 100 + (int)i * ROW_HEIGHT, &ui->continue_books.items[i], (int)i == ui->selected_book);
        }
        break;
    case UI_SCREEN_TITLES:
        draw_header(ui, "Titles");
        for (size_t i = 0; i < ui->books.count && i < 6; i++) {
            draw_book_row(ui, 100 + (int)i * ROW_HEIGHT, &ui->books.items[i], (int)i == ui->selected_book);
        }
        break;
    case UI_SCREEN_NOW_PLAYING:
        draw_now_playing(ui, player);
        break;
    case UI_SCREEN_CHAPTERS:
        draw_header(ui, "Chapters");
        for (size_t i = 0; i < ui->chapters.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "%lld ms", (long long)ui->chapters.items[i].duration_ms);
            draw_row_text(ui, 100 + (int)i * ROW_HEIGHT, ui->chapters.items[i].title, subtitle, (int)i == ui->selected_track);
        }
        break;
    case UI_SCREEN_FINISHED:
        draw_header(ui, "Finished");
        for (size_t i = 0; i < ui->finished_books.count && i < 5; i++) {
            draw_book_row(ui, 100 + (int)i * ROW_HEIGHT, &ui->finished_books.items[i], false);
        }
        break;
    case UI_SCREEN_SETTINGS:
        draw_header(ui, "Settings");
        draw_row_text(ui, 100, "Authors", "Browse by author", false);
        draw_row_text(ui, 100 + ROW_HEIGHT, "Series", "Browse by series", false);
        draw_row_text(ui, 100 + ROW_HEIGHT * 2, "Folders", "Browse by library folder", false);
        draw_row_text(ui, 100 + ROW_HEIGHT * 3, "Playback speed", "Tap speed button in Now Playing", false);
        draw_row_text(ui, 100 + ROW_HEIGHT * 4, "Sleep timer", "15 / 30 / 60 / 90 minutes", false);
        draw_row_text(ui, 100 + ROW_HEIGHT * 5, "Refresh library", "Rescan /Audiobooks", false);
        break;
    case UI_SCREEN_AUTHORS:
        draw_header(ui, "Authors");
        for (size_t i = 0; i < ui->authors.count && i < 6; i++) {
            draw_row_text(ui, 100 + (int)i * ROW_HEIGHT, ui->authors.items[i], "Tap to view books", (int)i == ui->selected_list);
        }
        break;
    case UI_SCREEN_SERIES:
        draw_header(ui, "Series");
        for (size_t i = 0; i < ui->series.count && i < 6; i++) {
            draw_row_text(ui, 100 + (int)i * ROW_HEIGHT, ui->series.items[i], "Tap to view books", (int)i == ui->selected_list);
        }
        break;
    case UI_SCREEN_FOLDERS:
        draw_header(ui, "Folders");
        for (size_t i = 0; i < ui->folders.count && i < 6; i++) {
            draw_row_text(ui, 100 + (int)i * ROW_HEIGHT, ui->folders.items[i], "Tap to view books", (int)i == ui->selected_list);
        }
        break;
    case UI_SCREEN_BOOKMARKS:
        draw_header(ui, "Bookmarks");
        for (size_t i = 0; i < ui->bookmarks.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "%lld ms", (long long)ui->bookmarks.items[i].position_ms);
            draw_row_text(ui, 100 + (int)i * ROW_HEIGHT, ui->bookmarks.items[i].label, subtitle, false);
        }
        break;
    }
    fb_present(&ui->fb);
    return 0;
}

static int open_book(ui_context *ui, audiobook_player *player, audiobook_db *db, int index, resume_state *resume) {
    if (ui_load_book(ui, db, index) != 0) return -1;
    book_row *book = &ui->books.items[index];
    progress_row prog;
    memset(&prog, 0, sizeof(prog));
    bool have_progress = db_get_progress(db, book->book_id, &prog) == 0;
    uint64_t now = ab_now_ms();
    uint64_t saved_local = resolve_resume_position_ms(&ui->tracks, &prog);
    uint64_t saved_absolute = prog.total_book_elapsed_ms > 0 ? (uint64_t)prog.total_book_elapsed_ms : (uint64_t)prog.position_ms;
    uint64_t paused_seconds = 0;
    bool rebooted = false;
    bool was_completed = prog.completed != 0;
    if (prog.last_played_at > 0) {
        if (now < (uint64_t)prog.last_played_at) {
            rebooted = true;
        } else {
            paused_seconds = (now - (uint64_t)prog.last_played_at) / 1000u;
        }
    }
    uint32_t rewinded_local = resume_smart_rewind_ms(paused_seconds, rebooted, (uint32_t)saved_local);
    progress_row play_prog = prog;
    if (was_completed) {
        play_prog.track_ordinal = 1;
        play_prog.position_ms = 0;
        play_prog.total_book_elapsed_ms = 0;
        play_prog.completed = 0;
        play_prog.completed_at = 0;
    } else {
        play_prog.position_ms = (int64_t)rewinded_local;
        play_prog.total_book_elapsed_ms = (int64_t)saved_absolute;
    }
    play_prog.protected_until_ms = (int64_t)now + 10000;
    play_prog.last_played_at = (int64_t)now;
    play_prog.last_saved_at = (int64_t)now;
    player_open_book(player, book, &ui->tracks, &play_prog);
    player_play(player);
    if (have_progress || saved_local > 0 || prog.completed) {
        progress_row write_prog = prog;
        if (was_completed) {
            write_prog.track_ordinal = 1;
            write_prog.position_ms = 0;
            write_prog.total_book_elapsed_ms = 0;
            write_prog.completed = 0;
            write_prog.completed_at = 0;
        } else {
            write_prog.position_ms = (int64_t)saved_local;
            write_prog.total_book_elapsed_ms = (int64_t)saved_absolute;
            write_prog.completed = 0;
            write_prog.completed_at = 0;
        }
        write_prog.protected_until_ms = (int64_t)now + 10000;
        write_prog.last_played_at = (int64_t)now;
        write_prog.last_saved_at = (int64_t)now;
        if (play_prog.completed) {
            (void)db_set_book_completion_txn(db, &write_prog);
        } else {
            (void)db_set_progress_txn(db, &write_prog);
        }
        (void)resume_write_record_atomic(ui->cfg.app_root, &write_prog, book);
    }
    if (resume) {
        audiobook_event ev = {0};
        ev.type = AB_EVT_BOOK_OPENED;
        ev.book_id = (uint32_t)book->book_id;
        snprintf(ev.book_key, sizeof(ev.book_key), "%s", book->book_key);
        progress_row out;
        progress_row bound_prog = was_completed ? play_prog : prog;
        resume_bind_book(resume, book, &bound_prog);
        resume_on_event(resume, &ev, &out);
    }
    ui->screen = UI_SCREEN_NOW_PLAYING;
    return 0;
}

static bool in_mini_player(const touch_event *ev) {
    return ev->y >= AB_SCREEN_H - MINI_H && ev->y < AB_SCREEN_H;
}

static bool active_book_loaded(const ui_context *ui, const audiobook_player *player) {
    (void)ui;
    return player && player->book_loaded;
}

int ui_handle_touch(ui_context *ui, const touch_event *ev, audiobook_player *player, audiobook_db *db, resume_state *resume) {
    if (!ui || !ev) return -1;

    /* Swipe right anywhere except Now Playing jumps to Now Playing if a book is active. */
    if (ev->action == TOUCH_SWIPE_RIGHT) {
        if (ui->screen != UI_SCREEN_NOW_PLAYING && active_book_loaded(ui, player)) {
            ui->screen = UI_SCREEN_NOW_PLAYING;
            return 0;
        }
    }

    if (ui->screen == UI_SCREEN_HOME && ev->action == TOUCH_TAP) {
        if (active_book_loaded(ui, player) && in_mini_player(ev)) {
            ui->screen = UI_SCREEN_NOW_PLAYING;
            return 0;
        }
        if (ev->y >= 100 && ev->y < 100 + ROW_HEIGHT) {
            ui_load_continue(ui, db);
            ui->screen = UI_SCREEN_CONTINUE;
            return 0;
        }
        if (ev->y >= 100 + ROW_HEIGHT && ev->y < 100 + ROW_HEIGHT * 2) {
            if (ui_load_titles(ui, db) == 0) ui->screen = UI_SCREEN_TITLES;
            return 0;
        }
        if (ev->y >= 100 + ROW_HEIGHT * 2 && ev->y < 100 + ROW_HEIGHT * 3) {
            if (ui_load_finished(ui, db) == 0) ui->screen = UI_SCREEN_FINISHED;
            return 0;
        }
        if (ev->y >= 100 + ROW_HEIGHT * 3 && ev->y < 100 + ROW_HEIGHT * 4) {
            if (ui_load_authors(ui, db) == 0) { ui->selected_list = 0; ui->screen = UI_SCREEN_AUTHORS; }
            return 0;
        }
        if (ev->y >= 100 + ROW_HEIGHT * 4 && ev->y < 100 + ROW_HEIGHT * 5) {
            if (ui_load_series(ui, db) == 0) { ui->selected_list = 0; ui->screen = UI_SCREEN_SERIES; }
            return 0;
        }
        if (ev->y >= 100 + ROW_HEIGHT * 5 && ev->y < 100 + ROW_HEIGHT * 6) {
            if (ui_load_folders(ui, db) == 0) { ui->selected_list = 0; ui->screen = UI_SCREEN_FOLDERS; }
            return 0;
        }
        if (ev->y >= 100 + ROW_HEIGHT * 6 && ev->y < 100 + ROW_HEIGHT * 7) {
            ui->screen = UI_SCREEN_SETTINGS;
            return 0;
        }
    }
    if (ui->screen == UI_SCREEN_CONTINUE && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row >= 0 && (size_t)row < ui->continue_books.count) {
            int64_t book_id = ui->continue_books.items[row].book_id;
            if (ui_load_titles(ui, db) == 0) {
                int index = find_book_index(&ui->books, book_id);
                if (index >= 0) open_book(ui, player, db, index, resume);
            }
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_TITLES && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row >= 0 && (size_t)row < ui->books.count) {
            open_book(ui, player, db, row, resume);
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_FINISHED && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row >= 0 && (size_t)row < ui->finished_books.count) {
            int64_t book_id = ui->finished_books.items[row].book_id;
            if (ui_load_titles(ui, db) == 0) {
                int index = find_book_index(&ui->books, book_id);
                if (index >= 0) open_book(ui, player, db, index, resume);
            }
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_SETTINGS && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row == 0) {
            if (ui_load_authors(ui, db) == 0) { ui->selected_list = 0; ui->screen = UI_SCREEN_AUTHORS; }
        } else if (row == 1) {
            if (ui_load_series(ui, db) == 0) { ui->selected_list = 0; ui->screen = UI_SCREEN_SERIES; }
        } else if (row == 2) {
            if (ui_load_folders(ui, db) == 0) { ui->selected_list = 0; ui->screen = UI_SCREEN_FOLDERS; }
        } else if (row == 5) {
            if (library_refresh(db, &ui->cfg, NULL) == 0) {
                ui_load_titles(ui, db);
                ui_load_continue(ui, db);
                ui_load_finished(ui, db);
            }
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_AUTHORS && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row >= 0 && (size_t)row < ui->authors.count) {
            ui->selected_list = row;
            const char *author = ui->authors.items[row];
            db_free_book_list(&ui->books);
            db_search(db, author, &ui->books);
            ui->screen = UI_SCREEN_TITLES;
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_SERIES && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row >= 0 && (size_t)row < ui->series.count) {
            ui->selected_list = row;
            const char *series = ui->series.items[row];
            db_free_book_list(&ui->books);
            db_search(db, series, &ui->books);
            ui->screen = UI_SCREEN_TITLES;
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_FOLDERS && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / ROW_HEIGHT;
        if (row >= 0 && (size_t)row < ui->folders.count) {
            ui->selected_list = row;
            const char *folder = ui->folders.items[row];
            db_free_book_list(&ui->books);
            db_search(db, folder, &ui->books);
            ui->screen = UI_SCREEN_TITLES;
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_SETTINGS && ev->action == TOUCH_BACK_EDGE) {
        ui->screen = UI_SCREEN_HOME;
        return 0;
    }
    if ((ui->screen == UI_SCREEN_TITLES || ui->screen == UI_SCREEN_CONTINUE || ui->screen == UI_SCREEN_FINISHED) && ev->action == TOUCH_BACK_EDGE) {
        ui->screen = UI_SCREEN_HOME;
        return 0;
    }
    if (ui->screen == UI_SCREEN_HOME && ev->action == TOUCH_BACK_EDGE) {
        g_request_exit = 1;
        return 0;
    }
    if ((ui->screen == UI_SCREEN_AUTHORS || ui->screen == UI_SCREEN_SERIES || ui->screen == UI_SCREEN_FOLDERS) && ev->action == TOUCH_BACK_EDGE) {
        ui->screen = UI_SCREEN_SETTINGS;
        return 0;
    }
    if (ui->screen == UI_SCREEN_NOW_PLAYING) {
        if (ev->action == TOUCH_BACK_EDGE) {
            ui->screen = UI_SCREEN_HOME;
            return 0;
        }
        if (ev->action == TOUCH_TAP) {
            if (ev->y >= 560 && ev->y < 660) {
                if (ev->x >= 20 && ev->x < 130) {
                    player_previous_track(player);
                } else if (ev->x >= 150 && ev->x < 260) {
                    if (player->state == PLAYER_PLAYING) player_pause(player);
                    else player_play(player);
                } else if (ev->x >= 280 && ev->x < 390) {
                    player_next_track(player);
                }
            } else if (ev->y >= 120 && ev->y < 360 && ev->x >= 20 && ev->x < 200) {
                /* tap cover to pause/play too */
                if (player->state == PLAYER_PLAYING) player_pause(player);
                else player_play(player);
            } else {
                ui->screen = UI_SCREEN_CHAPTERS;
            }
            return 0;
        }
        if (ev->action == TOUCH_SWIPE_UP) {
            player_next_track(player);
            return 0;
        }
        if (ev->action == TOUCH_SWIPE_DOWN) {
            player_previous_track(player);
            ui->screen = UI_SCREEN_NOW_PLAYING;
            return 0;
        }
    }
    if (ui->screen == UI_SCREEN_CHAPTERS && ev->action == TOUCH_BACK_EDGE) {
        ui->screen = UI_SCREEN_NOW_PLAYING;
        return 0;
    }
    if (ui->screen == UI_SCREEN_BOOKMARKS && ev->action == TOUCH_BACK_EDGE) {
        ui->screen = UI_SCREEN_NOW_PLAYING;
        return 0;
    }
    return 0;
}
