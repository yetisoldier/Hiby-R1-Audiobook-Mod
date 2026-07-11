#include "ui.h"
#include "common.h"
#include "scanner.h"

#include <stdio.h>
#include <string.h>

static void draw_header(ui_context *ui, const char *title) {
    fb_fill_rect(&ui->fb, 0, 0, AB_SCREEN_W, 80, 0x18A3);
    font_draw_text(&ui->fb, &ui->font, 18, 20, 0xFFFF, title);
}

static void draw_row(ui_context *ui, int y, const char *title, const char *subtitle, bool highlighted) {
    fb_fill_rect(&ui->fb, 12, y, AB_SCREEN_W - 24, 92, highlighted ? 0x39E7 : 0x2104);
    font_draw_text(&ui->fb, &ui->font, 24, y + 18, 0xFFFF, title);
    if (subtitle && subtitle[0]) font_draw_text(&ui->fb, &ui->font, 24, y + 40, 0xC618, subtitle);
}

int ui_init(ui_context *ui, const audiobook_config *cfg) {
    if (!ui || !cfg) return -1;
    memset(ui, 0, sizeof(*ui));
    ui->cfg = *cfg;
    if (fb_open(&ui->fb, cfg->fb_path) != 0) return -1;
    font_open(&ui->font, cfg->font_path);
    if (touch_open(&ui->touch, cfg->touch_path) != 0) {
        touch_close(&ui->touch);
    }
    ui->screen = UI_SCREEN_HOME;
    return 0;
}

void ui_shutdown(ui_context *ui) {
    if (!ui) return;
    db_free_book_list(&ui->books);
    db_free_track_list(&ui->tracks);
    fb_close(&ui->fb);
    touch_close(&ui->touch);
    memset(ui, 0, sizeof(*ui));
}

int ui_load_titles(ui_context *ui, audiobook_db *db) {
    if (!ui || !db) return -1;
    db_free_book_list(&ui->books);
    return db_query_titles(db, &ui->books);
}

int ui_load_book(ui_context *ui, audiobook_db *db, int index) {
    if (!ui || !db || index < 0 || (size_t)index >= ui->books.count) return -1;
    db_free_track_list(&ui->tracks);
    book_row *book = &ui->books.items[index];
    ui->selected_book = index;
    if (db_query_chapters(db, book->book_id, &ui->tracks) != 0) return -1;
    ui->selected_track = 0;
    cover_load_for_book(&ui->cover, book);
    return 0;
}

int ui_render(ui_context *ui, const audiobook_player *player) {
    if (!ui) return -1;
    fb_clear(&ui->fb, 0x0000);
    switch (ui->screen) {
    case UI_SCREEN_HOME:
        draw_header(ui, "Audiobooks");
        draw_row(ui, 100, "Continue Listening", "Open your current book", false);
        draw_row(ui, 210, "Titles", "All books in the library", false);
        draw_row(ui, 320, "Refresh Library", "Rescan /Audiobooks", false);
        break;
    case UI_SCREEN_TITLES:
        draw_header(ui, "Titles");
        for (size_t i = 0; i < ui->books.count && i < 6; i++) {
            char subtitle[384];
            snprintf(subtitle, sizeof(subtitle), "%s", ui->books.items[i].author);
            draw_row(ui, 100 + (int)i * 102, ui->books.items[i].title, subtitle, (int)i == ui->selected_book);
        }
        break;
    case UI_SCREEN_NOW_PLAYING:
        draw_header(ui, "Now Playing");
        cover_draw_placeholder(&ui->fb, &ui->cover, 20, 100, 180, 180);
        if (ui->selected_book >= 0 && (size_t)ui->selected_book < ui->books.count) {
            book_row *book = &ui->books.items[ui->selected_book];
            font_draw_text(&ui->fb, &ui->font, 220, 120, 0xFFFF, book->title);
            font_draw_text(&ui->fb, &ui->font, 220, 150, 0xC618, book->author);
            if (ui->tracks.count > 0) {
                font_draw_text(&ui->fb, &ui->font, 220, 190, 0xFFFF, ui->tracks.items[ui->selected_track].title);
            }
            char status[64];
            snprintf(status, sizeof(status), "%.2fx %llums", player ? player->speed : 1.0f, (unsigned long long)(player ? player->position_ms : 0));
            font_draw_text(&ui->fb, &ui->font, 220, 230, 0xFFFF, status);
        }
        break;
    case UI_SCREEN_CHAPTERS:
        draw_header(ui, "Chapters");
        for (size_t i = 0; i < ui->tracks.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "%lld ms", (long long)ui->tracks.items[i].duration_ms);
            draw_row(ui, 100 + (int)i * 102, ui->tracks.items[i].title, subtitle, (int)i == ui->selected_track);
        }
        break;
    case UI_SCREEN_FINISHED:
        draw_header(ui, "Finished");
        draw_row(ui, 180, "Book completed", "Tap to start over", false);
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
    db_get_progress(db, book->book_id, &prog);
    player_open_book(player, book, &ui->tracks, &prog);
    player_play(player);
    if (resume) {
        audiobook_event ev = {0};
        ev.type = AB_EVT_BOOK_OPENED;
        ev.book_id = (uint32_t)book->book_id;
        snprintf(ev.book_key, sizeof(ev.book_key), "%s", book->book_key);
        progress_row out;
        resume_bind_book(resume, book, &prog);
        resume_on_event(resume, &ev, &out);
    }
    ui->screen = UI_SCREEN_NOW_PLAYING;
    return 0;
}

int ui_handle_touch(ui_context *ui, const touch_event *ev, audiobook_player *player, audiobook_db *db, resume_state *resume) {
    if (!ui || !ev) return -1;
    if (ui->screen == UI_SCREEN_HOME && ev->action == TOUCH_TAP) {
        if (ev->y >= 90 && ev->y < 180) {
            ui->screen = UI_SCREEN_TITLES;
            return 0;
        }
        if (ev->y >= 200 && ev->y < 290) {
            if (ui_load_titles(ui, db) == 0) ui->screen = UI_SCREEN_TITLES;
            return 0;
        }
        if (ev->y >= 310 && ev->y < 410) {
            if (library_refresh(db, &ui->cfg, NULL) == 0) ui_load_titles(ui, db);
            return 0;
        }
    }
    if (ui->screen == UI_SCREEN_TITLES && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / 102;
        if (row >= 0 && (size_t)row < ui->books.count) {
            open_book(ui, player, db, row, resume);
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_NOW_PLAYING) {
        if (ev->action == TOUCH_BACK_EDGE) {
            ui->screen = UI_SCREEN_HOME;
            return 0;
        }
        if (ev->action == TOUCH_SWIPE_UP) {
            player_next_track(player);
            return 0;
        }
        if (ev->action == TOUCH_SWIPE_DOWN) {
            player_previous_track(player);
            return 0;
        }
        if (ev->action == TOUCH_TAP) {
            if (ev->x < 140) {
                player_pause(player);
            } else if (ev->x > 320) {
                player_play(player);
            }
        }
    }
    if (ui->screen == UI_SCREEN_CHAPTERS && ev->action == TOUCH_BACK_EDGE) {
        ui->screen = UI_SCREEN_NOW_PLAYING;
        return 0;
    }
    return 0;
}
