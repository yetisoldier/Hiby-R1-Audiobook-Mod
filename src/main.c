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
#include "state.h"
#include "shadow.h"

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

/* ── Poll cycle (Phase 4: state machine) ───────────────────────────── */

static daemon_runtime g_rt;
static catalog_db     g_cat;

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* Parse simple command-line args */
    const char *config_file = NULL;
    uint32_t override_interval = 0;
    int show_help = 0;
    int show_version = 0;
    int shadow_mode = 0;

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
        } else if (strcmp(argv[i], "--shadow") == 0) {
            shadow_mode = 1;
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

    /* Apply --shadow override */
    if (shadow_mode) {
        cfg.shadow_mode = 1;
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

    /* Initialize runtime state */
    state_init(&g_rt);

    /* Load catalog */
    if (catalog_load(&g_cat, cfg.catalog_path, cfg.catalog_albums_path,
                     cfg.catalog_books_path) != 0) {
        log_msg("warning: catalog load failed, continuing with empty catalog");
    }
    refresh_catalog_album_patterns(&g_cat);

    /* Initialize last marker seq */
    g_rt.last_book_title_seq = book_title_marker_seq(&cfg);

    /* Main loop */
    while (!shutdown_requested) {
        /* Reap any zombie children from helper calls */
        reap_children();

        /* Run one state machine poll cycle */
        uint32_t sleep_secs = state_poll_cycle(&g_rt, &cfg, &g_cat);

        /* Sleep for the recommended interval */
        unsigned int sleep_remaining = sleep_secs;
        while (sleep_remaining > 0 && !shutdown_requested) {
            sleep_remaining = sleep(sleep_remaining);
        }
    }

    /* Clean shutdown */
    log_msg("shutdown signal received");
    remove_pid_file(cfg.pid_file);
    catalog_free(&g_cat);
    log_close();
    config_free(&cfg);

    return 0;
}