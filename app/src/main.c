#include "common.h"
#include "config.h"
#include "db.h"
#include "ipc.h"
#include "player.h"
#include "resume.h"
#include "scanner.h"
#include "touch.h"
#include "ui.h"

#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
static void on_signal(int signo) {
    (void)signo;
    g_running = 0;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s [--scan-only]\n", argv0);
}

static void ensure_dir(const char *path) {
    if (!path || !path[0]) return;
    mkdir(path, 0755);
}

int main(int argc, char **argv) {
    bool scan_only = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scan-only") == 0) scan_only = true;
        else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    audiobook_config cfg;
    config_init(&cfg);
    config_load_env(&cfg);
    ensure_dir(cfg.app_root);
    ensure_dir("/usr/data/audiobooks/cache");
    ensure_dir(cfg.cover_cache_dir);
    ensure_dir("/usr/data/audiobooks/run");

    audiobook_db db;
    if (db_open(&db, cfg.db_path) != 0) {
        fprintf(stderr, "failed to open db: %s\n", cfg.db_path);
        return 1;
    }
    if (db_migrate(&db) != 0) {
        fprintf(stderr, "failed to migrate db\n");
        db_close(&db);
        return 1;
    }

    library_refresh(&db, &cfg, NULL);
    if (scan_only) {
        db_close(&db);
        return 0;
    }

    ui_context ui;
    if (ui_init(&ui, &cfg) != 0) {
        fprintf(stderr, "failed to initialize UI\n");
        db_close(&db);
        return 1;
    }

    audiobook_player player;
    if (player_init(&player, &cfg) != 0) {
        fprintf(stderr, "failed to initialize player\n");
        ui_shutdown(&ui);
        db_close(&db);
        return 1;
    }
    resume_state resume;
    resume_init(&resume);
    int ipc_fd = ipc_client_connect(cfg.resume_socket);

    ui_load_titles(&ui, &db);
    ui_render(&ui, NULL);

    uint64_t last_tick = ab_now_ms();
    player_snapshot prev_snap;
    player_snapshot snap;
    memset(&prev_snap, 0, sizeof(prev_snap));
    while (g_running) {
        touch_event tev;
        if (touch_poll(&ui.touch, &tev, 200) == 0) {
            ui_handle_touch(&ui, &tev, &player, &db, &resume);
            ui_render(&ui, &player);
        }
        player_poll(&player, &snap);
        if (snap.book_id != prev_snap.book_id && snap.book_id != 0) {
            audiobook_event ev = {0};
            ev.type = AB_EVT_BOOK_OPENED;
            ev.book_id = (uint32_t)snap.book_id;
            if (ui.selected_book >= 0 && (size_t)ui.selected_book < ui.books.count) {
                snprintf(ev.book_key, sizeof(ev.book_key), "%s", ui.books.items[ui.selected_book].book_key);
                resume_bind_book(&resume, &ui.books.items[ui.selected_book], NULL);
                resume_on_event(&resume, &ev, &(progress_row){0});
            }
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (snap.state == PLAYER_PLAYING && prev_snap.state != PLAYER_PLAYING) {
            audiobook_event ev = {0};
            ev.type = AB_EVT_PLAYBACK_STARTED;
            ev.book_id = (uint32_t)snap.book_id;
            ev.track_id = (uint32_t)snap.track_id;
            ev.track_ordinal = (uint32_t)snap.track_ordinal;
            ev.position_ms = (uint32_t)snap.position_ms;
            ev.duration_ms = (uint32_t)snap.duration_ms;
            ev.playback_speed_x100 = (uint32_t)(snap.speed * 100.0f);
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (snap.track_ordinal != prev_snap.track_ordinal && snap.track_ordinal > 0) {
            audiobook_event ev = {0};
            ev.type = AB_EVT_TRACK_CHANGED;
            ev.book_id = (uint32_t)snap.book_id;
            ev.track_ordinal = (uint32_t)snap.track_ordinal;
            ev.position_ms = (uint32_t)snap.position_ms;
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (snap.state == PLAYER_PAUSED && prev_snap.state == PLAYER_PLAYING) {
            audiobook_event ev = {0};
            ev.type = AB_EVT_PAUSED;
            ev.book_id = (uint32_t)snap.book_id;
            ev.track_id = (uint32_t)snap.track_id;
            ev.track_ordinal = (uint32_t)snap.track_ordinal;
            ev.position_ms = (uint32_t)snap.position_ms;
            ev.playback_speed_x100 = (uint32_t)(snap.speed * 100.0f);
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (snap.eof_reached && !prev_snap.eof_reached) {
            audiobook_event ev = {0};
            ev.type = AB_EVT_EOF_REACHED;
            ev.book_id = (uint32_t)snap.book_id;
            ev.track_ordinal = (uint32_t)snap.track_ordinal;
            ev.position_ms = (uint32_t)snap.position_ms;
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (ab_now_ms() - last_tick > cfg.save_interval_ms && ui.selected_book >= 0 &&
            (size_t)ui.selected_book < ui.books.count) {
            progress_row prog;
            memset(&prog, 0, sizeof(prog));
            prog.book_id = snap.book_id ? snap.book_id : ui.books.items[ui.selected_book].book_id;
            prog.track_id = snap.track_id;
            prog.track_ordinal = snap.track_ordinal;
            prog.position_ms = (int64_t)snap.position_ms;
            prog.playback_speed = snap.speed;
            prog.last_played_at = (int64_t)ab_now_ms();
            db_set_progress_txn(&db, &prog);
            resume_write_record_atomic(cfg.app_root, &prog, &ui.books.items[ui.selected_book]);
            if (ipc_fd >= 0) {
                audiobook_event ev = {0};
                ev.type = AB_EVT_POSITION_TICK;
                ev.book_id = (uint32_t)prog.book_id;
                ev.track_id = (uint32_t)prog.track_id;
                ev.track_ordinal = (uint32_t)prog.track_ordinal;
                ev.position_ms = (uint32_t)prog.position_ms;
                ev.duration_ms = (uint32_t)snap.duration_ms;
                ev.playback_speed_x100 = (uint32_t)(prog.playback_speed * 100.0f);
                ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
            }
            last_tick = ab_now_ms();
        }
        prev_snap = snap;
    }

    if (ui.selected_book >= 0 && (size_t)ui.selected_book < ui.books.count) {
        audiobook_event ev = {.type = AB_EVT_APP_EXITING};
        ev.book_id = (uint32_t)ui.books.items[ui.selected_book].book_id;
        snprintf(ev.book_key, sizeof(ev.book_key), "%s", ui.books.items[ui.selected_book].book_key);
        progress_row prog;
        memset(&prog, 0, sizeof(prog));
        prog.book_id = snap.book_id ? snap.book_id : ui.books.items[ui.selected_book].book_id;
        prog.track_id = snap.track_id;
        prog.track_ordinal = snap.track_ordinal > 0 ? snap.track_ordinal : 1;
        prog.position_ms = (int64_t)snap.position_ms;
        prog.playback_speed = snap.speed;
        db_set_progress_txn(&db, &prog);
        resume_write_record_atomic(cfg.app_root, &prog, &ui.books.items[ui.selected_book]);
        if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
    }

    ui_shutdown(&ui);
    player_shutdown(&player);
    if (ipc_fd >= 0) close(ipc_fd);
    db_close(&db);
    return 0;
}
