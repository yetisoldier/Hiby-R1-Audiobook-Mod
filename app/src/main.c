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
#include <time.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_request_exit = 0;
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

static const book_row *active_book(const ui_context *ui) {
    if (!ui || ui->selected_book < 0 || (size_t)ui->selected_book >= ui->books.count) return NULL;
    return &ui->books.items[ui->selected_book];
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

static uint64_t snapshot_local_position_ms(const player_snapshot *snap, const track_list *tracks) {
    if (!snap) return 0;
    uint64_t prefix = track_prefix_ms(tracks, snap->track_ordinal);
    return snap->position_ms > prefix ? snap->position_ms - prefix : 0u;
}

static int save_snapshot_progress(audiobook_db *db, const audiobook_config *cfg, const book_row *book,
                                  const track_list *tracks, const player_snapshot *snap, bool completed,
                                  int64_t completed_at, bool force_write) {
    if (!db || !cfg || !book || !snap || snap->book_id == 0) return -1;

    progress_row existing;
    memset(&existing, 0, sizeof(existing));
    bool have_existing = db_get_progress(db, book->book_id, &existing) == 0;

    uint64_t local_position = snap->track_position_ms;
    if (local_position == 0) local_position = snapshot_local_position_ms(snap, tracks);
    uint64_t book_position = snap->position_ms;
    uint64_t duration_ms = track_duration_ms(tracks, snap->track_ordinal);
    if (duration_ms > 0 && local_position > duration_ms) local_position = duration_ms;
    uint64_t saved_book_position = have_existing && existing.total_book_elapsed_ms > 0
        ? (uint64_t)existing.total_book_elapsed_ms
        : (have_existing ? (uint64_t)existing.position_ms : 0u);

    if (!force_write && have_existing && existing.protected_until_ms > 0 &&
        (uint64_t)ab_now_ms() < (uint64_t)existing.protected_until_ms &&
        book_position <= saved_book_position) {
        return 0;
    }

    progress_row prog;
    memset(&prog, 0, sizeof(prog));
    prog.book_id = book->book_id;
    prog.track_id = snap->track_id;
    prog.track_ordinal = snap->track_ordinal > 0 ? snap->track_ordinal : 1;
    prog.position_ms = (int64_t)local_position;
    prog.total_book_elapsed_ms = (int64_t)book_position;
    prog.playback_speed = snap->speed;
    prog.last_played_at = (int64_t)ab_now_ms();
    prog.completed = completed ? 1 : 0;
    prog.completed_at = completed ? (completed_at > 0 ? completed_at : (int64_t)time(NULL)) : 0;
    prog.last_saved_at = prog.last_played_at;

    if (!completed && have_existing && existing.protected_until_ms > 0 &&
        (uint64_t)ab_now_ms() < (uint64_t)existing.protected_until_ms &&
        book_position <= saved_book_position) {
        prog.protected_until_ms = existing.protected_until_ms;
    }

    if (completed) {
        prog.protected_until_ms = 0;
        if (db_set_book_completion_txn(db, &prog) != 0) return -1;
    } else if (db_set_progress_txn(db, &prog) != 0) {
        return -1;
    }
    if (resume_write_record_atomic(cfg->app_root, &prog, book) != 0) return -1;
    return 1;
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

    /* Only auto-scan on first run when the library is empty. Refresh from the
     * Settings screen or --scan-only will still trigger a full re-scan. */
    if (!db_has_library(&db) || scan_only) {
        library_scan_incremental(&db, &cfg, NULL);
    }
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
    while (g_running && !g_request_exit) {
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
                if (!resume.bound) {
                    resume_bind_book(&resume, &ui.books.items[ui.selected_book], NULL);
                }
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
            const book_row *book = active_book(&ui);
            if (book && prev_snap.book_id == snap.book_id && prev_snap.track_ordinal > 0) {
                (void)save_snapshot_progress(&db, &cfg, book, &ui.tracks, &prev_snap, false, 0, false);
            }
            audiobook_event ev = {0};
            ev.type = AB_EVT_TRACK_CHANGED;
            ev.book_id = (uint32_t)snap.book_id;
            ev.track_ordinal = (uint32_t)snap.track_ordinal;
            ev.position_ms = (uint32_t)snap.position_ms;
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (snap.state == PLAYER_PAUSED && prev_snap.state == PLAYER_PLAYING) {
            const book_row *book = active_book(&ui);
            if (book && snap.book_id == book->book_id) {
                (void)save_snapshot_progress(&db, &cfg, book, &ui.tracks, &snap, false, 0, false);
            }
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
            const book_row *book = active_book(&ui);
            if (book && snap.book_id == book->book_id && ui.tracks.count > 0 &&
                (size_t)snap.track_ordinal == ui.tracks.count) {
                int64_t completed_at = (int64_t)time(NULL);
                if (save_snapshot_progress(&db, &cfg, book, &ui.tracks, &snap, true, completed_at, true) > 0) {
                    (void)ui_load_continue(&ui, &db);
                    (void)ui_load_finished(&ui, &db);
                }
            }
            if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
        }
        if (ab_now_ms() - last_tick > cfg.save_interval_ms) {
            const book_row *book = active_book(&ui);
            if (book && snap.book_id == book->book_id) {
                (void)save_snapshot_progress(&db, &cfg, book, &ui.tracks, &snap, false, 0, false);
                if (ipc_fd >= 0) {
                    audiobook_event ev = {0};
                    ev.type = AB_EVT_POSITION_TICK;
                    ev.book_id = (uint32_t)book->book_id;
                    ev.track_id = (uint32_t)snap.track_id;
                    ev.track_ordinal = (uint32_t)snap.track_ordinal;
                    ev.position_ms = (uint32_t)snap.position_ms;
                    ev.duration_ms = (uint32_t)snap.duration_ms;
                    ev.playback_speed_x100 = (uint32_t)(snap.speed * 100.0f);
                    ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
                }
                last_tick = ab_now_ms();
            }
        }
        prev_snap = snap;
    }

    const book_row *book = active_book(&ui);
    if (book) {
        audiobook_event ev = {.type = AB_EVT_APP_EXITING};
        ev.book_id = (uint32_t)book->book_id;
        snprintf(ev.book_key, sizeof(ev.book_key), "%s", book->book_key);
        if (snap.book_id == book->book_id) {
            (void)save_snapshot_progress(&db, &cfg, book, &ui.tracks, &snap,
                                         snap.eof_reached && ui.tracks.count > 0 &&
                                         (size_t)snap.track_ordinal == ui.tracks.count,
                                         (int64_t)time(NULL),
                                         snap.eof_reached && ui.tracks.count > 0 &&
                                         (size_t)snap.track_ordinal == ui.tracks.count);
        }
        if (ipc_fd >= 0) ipc_send_event(ipc_fd, ev.type, ab_now_ms(), &ev);
    }

    ui_shutdown(&ui);
    player_shutdown(&player);
    if (ipc_fd >= 0) close(ipc_fd);
    db_close(&db);
    return 0;
}
