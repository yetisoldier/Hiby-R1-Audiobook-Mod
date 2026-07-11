/*
 * state.h — state machine, daemon runtime state, diagnostics
 *
 * Spec section 2.4 (daemon_runtime), section 3 (state machine),
 * section 16 (diagnostics).
 *
 * This module orchestrates all other modules: player reads, resume
 * record I/O, UI actions, and helper invocations.
 */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

#include "config.h"
#include "player.h"
#include "catalog.h"
#include "resume.h"

/* ── Daemon state enum ────────────────────────────────────────────── */

typedef enum {
    STATE_IDLE = 0,                /* non-audiobook path, idle polling */
    STATE_AUDIOBOOK_TRACKING,      /* audiobook active, reading position */
    STATE_AUTOSTART_TRIGGERED,     /* book-title marker changed, autostart window */
    STATE_RESTORE_IN_PROGRESS,     /* track or position restore underway */
    STATE_RESTORE_FAILED,          /* restore failed, in backoff */
    STATE_COMPLETED,               /* book marked completed, waiting for start-over */
} daemon_state;

/* ── Runtime state (spec 2.4) ─────────────────────────────────────── */

typedef struct {
    daemon_state state;

    /* Path tracking */
    char     last_path[512];
    char     restored_path[512];
    char     completed_saved_path[512];
    char     completed_start_over_path[512];
    char     deferred_overwrite_path[512];

    /* Save bucketing */
    int      last_saved_bucket;

    /* Autostart window */
    time_t   book_title_autostart_until;
    uint32_t book_title_autostart_seq;
    char     book_title_autostart_reset_key[32];
    time_t   book_title_context_until;
    char     book_title_restore_wait_log_key[128];
    char     book_title_pre_restore_log_key[128];
    uint32_t last_book_title_seq;
    time_t   last_book_title_marker_poll_at;

    /* Restore failure tracking */
    char     restore_failed_path[512];
    time_t   restore_failed_at;
    char     restore_failed_kind[16];   /* "seek" or "track" */
    uint32_t restore_failed_saved_pos;
    char     restore_seek_failed_key[128];
    int      restore_seek_failed_count;
    char     failed_restore_skip_log_bucket[128];

    /* Back guard */
    time_t   audiobook_back_guard_until;
    time_t   audiobook_back_guard_seen_at;
    time_t   audiobook_back_guard_last_fire_at;

    /* Helper failure tracking */
    int      helper_failures;

    /* Auto-tap (Phase 2: track-once-per-playlist) */
    char     autotap_last_path[512];   /* path we already auto-tapped for (prevent double-tap) */
    time_t   autotap_fired_at;         /* when we last fired an auto-tap (for rate-limit/debug) */

    /* Diagnostics */
    time_t   diag_last_log_at;
    int      diag_loops;
    int      diag_audiobook_loops;
    int      diag_non_audiobook_loops;
    int      diag_path_previews;
    int      diag_marker_polls;
    int      diag_marker_skips;
    int      diag_position_reads;
    int      diag_saves;
    int      diag_autotap_fired;       /* count of auto-tap attempts */
    int      diag_autotap_skipped;     /* count of auto-tap skips */
} daemon_runtime;

/* ── Initialization ───────────────────────────────────────────────── */

/* Initialize a daemon_runtime struct to zero/defaults. */
void state_init(daemon_runtime *rt);

/* ── Main poll cycle ──────────────────────────────────────────────── */

/* Run one iteration of the main loop.  Called by main.c each interval.
 * Returns the recommended sleep seconds for the next interval. */
uint32_t state_poll_cycle(daemon_runtime *rt, const daemon_config *cfg,
                          catalog_db *cat);

/* ── Autostart orchestration ──────────────────────────────────────── */

/* Check if the autostart window is currently active. */
bool state_autostart_active(const daemon_runtime *rt);

/* Check if the book-title context window is still active. */
bool state_context_active(const daemon_runtime *rt, time_t now);

/* Determine if the marker should be polled this cycle. */
bool state_should_poll_marker(const daemon_runtime *rt,
                              const daemon_config *cfg,
                              const char *path_preview, time_t now);

/* Detect marker change and orchestrate autostart sequence.
 * Returns 0 on success, -1 on failure. */
int state_maybe_autostart(daemon_runtime *rt, const daemon_config *cfg,
                         catalog_db *cat, uint32_t seq);

/* Clear autostart state. */
void state_clear_autostart(daemon_runtime *rt);

/* ── Direct track select ──────────────────────────────────────────── */

/* Compute the geometry (swipes + row) for a given saved track index.
 * Writes swipes and row to out_swipes/out_row.
 * Returns 0 on success, -1 if too many swipes needed. */
int state_direct_geometry(const daemon_config *cfg,
                          int saved_index,
                          int *out_swipes, int *out_row);

/* Tap the track list at the given index, performing swipes as needed.
 * Returns 0 on success, -1 on failure. */
int state_tap_track_index(daemon_runtime *rt, const daemon_config *cfg,
                          int saved_index, const char *log_label);

/* Verify that the correct track is now playing after a selection.
 * Returns 0 on success (correct track), 2 on near-miss (same book wrong track),
 * -1 on failure. */
int state_verify_selected_track(daemon_runtime *rt, const daemon_config *cfg,
                                catalog_db *cat,
                                const char *saved_path, int saved_index,
                                int selected_row, const char *log_label,
                                const char *path_before_select);

/* Direct-open trigger: use the helper to directly open a track.
 * Returns 0 on success, -1 on failure. */
int state_direct_open_trigger(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              pid_t pid, int saved_index,
                              const char *saved_path,
                              const char *path_before_select,
                              const char *log_label);

/* Direct track select: try direct-open then swipe+tap.
 * Returns 0 on success, 2 on near-miss, -1 on failure. */
int state_direct_track_select(daemon_runtime *rt, const daemon_config *cfg,
                               catalog_db *cat,
                               int current_index, int saved_index,
                               const char *saved_path);

/* Visible track select: compute row from visible indices and tap.
 * Returns 0 on success, 2 on near-miss, -1 on failure. */
int state_visible_track_select(daemon_runtime *rt, const daemon_config *cfg,
                               catalog_db *cat,
                               int current_index, int saved_index,
                               const char *saved_path);

/* Direct-start saved track: memscan + catalog lookup + direct-open or swipe.
 * Returns 0 on success, -1 on failure. */
int state_direct_start_saved(daemon_runtime *rt, const daemon_config *cfg,
                             catalog_db *cat,
                             pid_t pid, uint32_t track_list_ptr,
                             uint32_t catalog_scan_ptr,
                             int allow_memscan_root);

/* ── Near-miss transport ──────────────────────────────────────────── */

/* Use key-based next/prev to step to the correct track after a near-miss.
 * Returns 0 on success, -1 on failure. */
int state_near_miss_transport(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              const char *saved_path, int saved_index,
                              const char *reason);

/* ── Track restore ────────────────────────────────────────────────── */

/* Attempt track restore for a path.
 * Returns 0 on success, -1 if not needed or failed. */
int state_maybe_restore_track(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              const char *current_path, uint32_t pos);

/* ── Diagnostics ──────────────────────────────────────────────────── */

/* Increment a diagnostic counter. */
void state_diag_inc(daemon_runtime *rt, int *counter);

/* Log diagnostics if the interval has elapsed. */
void state_diag_log(daemon_runtime *rt, const daemon_config *cfg, time_t now);

/* ── Logging helpers ──────────────────────────────────────────────── */

/* Compute the log bucket for a position. */
int state_log_bucket(const daemon_config *cfg, uint32_t pos);

/* Log a restore-wait message (throttled by bucket). */
void state_log_restore_wait(daemon_runtime *rt, const daemon_config *cfg,
                           const char *path, uint32_t pos);

/* Log a pre-restore skip message (throttled by bucket). */
void state_log_pre_restore_skip(daemon_runtime *rt, const daemon_config *cfg,
                                 const char *path, uint32_t pos);

/* ── Book root helper ─────────────────────────────────────────────── */

/* Extract the book root from a path (strip last \component). */
void state_book_root(const char *path, char *out_root, size_t out_len);

/* Check if two paths share the same book root. */
bool state_same_book_root(const char *path1, const char *root2);

/* ── Track switch settle ──────────────────────────────────────────── */

/* Compute the number of poll ticks for track switch settle. */
int state_settle_ticks(const daemon_config *cfg);

#endif /* STATE_H */