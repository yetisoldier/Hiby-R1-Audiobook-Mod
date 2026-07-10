/*
 * main.c — entry point: signal setup, PID file, inherited FD cleanup,
 *          config loading, log init, main loop skeleton
 *
 * Spec section 8 (signals), section 3 (main loop).
 * Phase 2: poll_cycle() calls player functions (PID, position).
 */

#include "config.h"
#include "log.h"
#include "helpers.h"
#include "player.h"
#include "catalog.h"
#include "resume.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define VERSION "0.1.0"

/* ── Signal flags ─────────────────────────────────────────────────── */

static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t child_exited = 0;

static void sig_handler(int signo) {
    switch (signo) {
    case SIGTERM:
    case SIGINT:
        shutdown_requested = 1;
        break;
    case SIGCHLD:
        child_exited = 1;
        break;
    }
}

/* ── Inherited FD cleanup ─────────────────────────────────────────── */

static void close_inherited_socket_fds(void) {
    /* Close any inherited file descriptors > STDERR_FILENO.
       The shell daemon does this to avoid leaking fds from the
       init script.  We close everything from 3 to 20. */
    int maxfd = 20;
    for (int fd = 3; fd <= maxfd; fd++) {
        close(fd);
    }
}

/* ── PID file ────────────────────────────────────────────────────── */

static int write_pid_file(const char *path) {
    /* Ensure parent dir exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);  /* ignore error if exists */
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_msg("cannot write pid file %s: %s", path, strerror(errno));
        return -1;
    }
    fprintf(fp, "%d\n", (int)getpid());
    fclose(fp);
    return 0;
}

static void remove_pid_file(const char *path) {
    if (path && path[0]) {
        unlink(path);
    }
}

/* ── Reap zombie children ────────────────────────────────────────── */

static void reap_children(void) {
    if (!child_exited) return;
    child_exited = 0;
    /* Reap all available zombies */
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* keep reaping */
    }
}

/* ── Poll cycle (Phase 2) ──────────────────────────────────────────── */

static int diag_loops = 0;
static int diag_audiobook_loops = 0;
static int diag_non_audiobook_loops = 0;
static int diag_position_reads = 0;
static int diag_saves = 0;
static time_t diag_last_log_at = 0;

static void log_diagnostics(const daemon_config *cfg) {
    time_t now = time(NULL);
    if (now - diag_last_log_at < (time_t)cfg->diagnostics_interval_seconds) return;

    log_msg("stats loops=%d audiobook=%d non_audiobook=%d position_reads=%d saves=%d",
            diag_loops, diag_audiobook_loops, diag_non_audiobook_loops,
            diag_position_reads, diag_saves);

    diag_loops = 0;
    diag_audiobook_loops = 0;
    diag_non_audiobook_loops = 0;
    diag_position_reads = 0;
    diag_saves = 0;
    diag_last_log_at = now;
}

static void poll_cycle(const daemon_config *cfg) {
    diag_loops++;

    /* Read path preview from user.ini */
    char preview[129];
    if (current_path_slot_preview(cfg, preview, sizeof(preview)) != 0) {
        preview[0] = '\0';
    }

    /* Classify path */
    bool is_audiobook = path_preview_is_audiobook(preview);
    bool is_music = path_preview_is_music(preview);

    if (is_audiobook) {
        diag_audiobook_loops++;

        /* Find hiby_player PID */
        pid_t pid = player_pid_cached();
        if (pid < 0) {
            log_msg("hiby_player not running");
            return;
        }

        /* Read position */
        uint32_t pos = position_ms_memory(cfg);
        diag_position_reads++;

        /* Read duration */
        uint32_t dur = duration_ms_memory(cfg);

        /* Read full path */
        char full_path[512];
        if (current_path_from_hex(cfg, full_path, sizeof(full_path)) != 0) {
            full_path[0] = '\0';
        }

        /* Log audiobook state (throttled by diagnostics) */
        if (diag_loops == 1 || (diag_audiobook_loops % 10 == 0)) {
            log_msg("audiobook path=%s pos=%ums dur=%ums pid=%d",
                    full_path[0] ? full_path : "?",
                    pos, dur, (int)pid);
        }

        /* Phase 2: save/restore logic would go here (Phase 3 wiring) */

    } else if (is_music) {
        diag_non_audiobook_loops++;
        /* Idle for music — no action needed */
    } else {
        diag_non_audiobook_loops++;
        /* Other path — idle */
    }

    /* Poll book-title marker (if enabled) */
    if (cfg->book_title_autostart_enabled) {
        uint32_t seq = book_title_marker_seq(cfg);
        if (seq > 0) {
            /* Phase 3 will handle autostart trigger; Phase 2 just logs */
            static uint32_t last_seq = 0;
            if (seq != last_seq) {
                log_msg("book_title marker seq changed: %u -> %u", last_seq, seq);
                last_seq = seq;
            }
        }
    }

    /* Log diagnostics periodically */
    log_diagnostics(cfg);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* Parse simple command-line args */
    const char *config_file = NULL;
    uint32_t override_interval = 0;
    int show_help = 0;
    int show_version = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help = 1;
        } else if (strcmp(argv[i], "--version") == 0) {
            show_version = 1;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            override_interval = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--interval=", 11) == 0) {
            override_interval = (uint32_t)strtoul(argv[i] + 11, NULL, 10);
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            config_file = argv[i] + 9;
        }
    }

    if (show_help) {
        config_print_help();
        return 0;
    }

    if (show_version) {
        printf("r1_audiobook_resume_daemon %s\n", VERSION);
        return 0;
    }

    /* Load configuration */
    daemon_config cfg;
    if (config_load(&cfg, config_file) != 0) {
        fprintf(stderr, "fatal: config_load failed\n");
        return 1;
    }

    /* Apply --interval override */
    if (override_interval > 0) {
        cfg.interval_seconds = override_interval;
    }

    /* If source-only mode, exit after config load (testing) */
    if (cfg.source_only) {
        config_log_summary(&cfg);
        config_free(&cfg);
        return 0;
    }

    /* Close inherited fds */
    close_inherited_socket_fds();

    /* Signal setup */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore SIGPIPE (writes to closed pipes) */
    signal(SIGPIPE, SIG_IGN);

    /* Ensure store dir exists */
    {
        char dir[512];
        strncpy(dir, cfg.store_dir, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mkdir(dir, 0755);
        }
    }

    /* Initialize logging */
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* Write PID file */
    if (write_pid_file(cfg.pid_file) != 0) {
        log_msg("warning: could not write pid file, continuing");
    }

    /* Log startup */
    log_msg("r1_audiobook_resume_daemon v%s starting (pid=%d)", VERSION, (int)getpid());
    config_log_summary(&cfg);

    /* Main loop */
    while (!shutdown_requested) {
        /* Reap any zombie children from helper calls */
        reap_children();

        /* Run one poll cycle */
        poll_cycle(&cfg);

        /* Sleep for interval */
        unsigned int sleep_remaining = cfg.interval_seconds;
        while (sleep_remaining > 0 && !shutdown_requested) {
            sleep_remaining = sleep(sleep_remaining);
        }
    }

    /* Clean shutdown */
    log_msg("shutdown signal received");
    remove_pid_file(cfg.pid_file);
    log_close();
    config_free(&cfg);

    return 0;
}