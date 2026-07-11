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

static void draw_button(ui_context *ui, int x, int y, int w, int h, const char *label, uint16_t fg, uint16_t bg) {
    fb_fill_rect(&ui->fb, x, y, w, h, bg);
    font_draw_text(&ui->fb, &ui->font, x + 10, y + 18, fg, label);
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
    font_open(&ui->font, cfg->font_path);
    if (touch_open(&ui->touch, cfg->touch_path) != 0) {
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
        draw_row(ui, 320, "Finished", "Books you've completed", false);
        draw_row(ui, 430, "Settings", "Speed, sleep timer, refresh", false);
        break;
    case UI_SCREEN_CONTINUE:
        draw_header(ui, "Continue");
        for (size_t i = 0; i < ui->continue_books.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "Last played %lld", (long long)ui->continue_books.items[i].last_played_at);
            draw_row(ui, 100 + (int)i * 102, ui->continue_books.items[i].title, subtitle, (int)i == ui->selected_book);
        }
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
            uint64_t pos = player ? player->position_ms : 0;
            uint64_t dur = player ? player->duration_ms : 0;
            uint64_t remain = dur > pos ? dur - pos : 0;
            snprintf(status, sizeof(status), "%.2fx %llums left", player ? player->speed : 1.0f, (unsigned long long)remain);
            font_draw_text(&ui->fb, &ui->font, 220, 230, 0xFFFF, status);
            fb_fill_rect(&ui->fb, 220, 260, 230, 12, 0x39E7);
            if (dur > 0) {
                int filled = (int)((230ull * pos) / dur);
                fb_fill_rect(&ui->fb, 220, 260, filled, 12, 0xFD20);
            }
            draw_button(ui, 220, 290, 100, 50, "Back 15s", 0xFFFF, 0x4208);
            draw_button(ui, 330, 290, 100, 50, "Fwd 30s", 0xFFFF, 0x4208);
            draw_button(ui, 220, 350, 100, 50, "Pause", 0xFFFF, 0x4208);
            draw_button(ui, 330, 350, 100, 50, "Play", 0xFFFF, 0x4208);
            draw_button(ui, 220, 410, 100, 50, "Chapters", 0xFFFF, 0x4208);
            draw_button(ui, 330, 410, 100, 50, "Speed", 0xFFFF, 0x4208);
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
        for (size_t i = 0; i < ui->finished_books.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "%s", ui->finished_books.items[i].author);
            draw_row(ui, 100 + (int)i * 102, ui->finished_books.items[i].title, subtitle, false);
        }
        break;
    case UI_SCREEN_SETTINGS:
        draw_header(ui, "Settings");
        draw_row(ui, 120, "Playback speed", "Tap speed button in Now Playing", false);
        draw_row(ui, 230, "Sleep timer", "15 / 30 / 60 / 90 minutes", false);
        draw_row(ui, 340, "Refresh library", "Rescan /Audiobooks", false);
        break;
    case UI_SCREEN_BOOKMARKS:
        draw_header(ui, "Bookmarks");
        for (size_t i = 0; i < ui->bookmarks.count && i < 5; i++) {
            char subtitle[128];
            snprintf(subtitle, sizeof(subtitle), "%lld ms", (long long)ui->bookmarks.items[i].position_ms);
            draw_row(ui, 100 + (int)i * 102, ui->bookmarks.items[i].label, subtitle, false);
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

int ui_handle_touch(ui_context *ui, const touch_event *ev, audiobook_player *player, audiobook_db *db, resume_state *resume) {
    if (!ui || !ev) return -1;
    if (ui->screen == UI_SCREEN_HOME && ev->action == TOUCH_TAP) {
        if (ev->y >= 90 && ev->y < 180) {
            ui_load_continue(ui, db);
            ui->screen = UI_SCREEN_CONTINUE;
            return 0;
        }
        if (ev->y >= 200 && ev->y < 290) {
            if (ui_load_titles(ui, db) == 0) ui->screen = UI_SCREEN_TITLES;
            return 0;
        }
        if (ev->y >= 310 && ev->y < 400) {
            if (ui_load_finished(ui, db) == 0) ui->screen = UI_SCREEN_FINISHED;
            return 0;
        }
        if (ev->y >= 420 && ev->y < 520) {
            ui->screen = UI_SCREEN_SETTINGS;
            return 0;
        }
    }
    if (ui->screen == UI_SCREEN_CONTINUE && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / 102;
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
        int row = (ev->y - 100) / 102;
        if (row >= 0 && (size_t)row < ui->books.count) {
            open_book(ui, player, db, row, resume);
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_FINISHED && ev->action == TOUCH_TAP) {
        int row = (ev->y - 100) / 102;
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
        if (ev->y >= 320 && ev->y < 420) {
            if (library_refresh(db, &ui->cfg, NULL) == 0) {
                ui_load_titles(ui, db);
                ui_load_continue(ui, db);
                ui_load_finished(ui, db);
            }
        }
        return 0;
    }
    if (ui->screen == UI_SCREEN_NOW_PLAYING) {
        if (ev->action == TOUCH_BACK_EDGE) {
            ui->screen = UI_SCREEN_HOME;
            return 0;
        }
        if (ev->action == TOUCH_TAP) {
            if (ev->x < 120) {
                player_pause(player);
            } else if (ev->x > 360) {
                player_play(player);
            } else if (ev->y > 390) {
                player_set_speed(player, player->speed < 1.75f ? player->speed + 0.25f : 0.75f);
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
