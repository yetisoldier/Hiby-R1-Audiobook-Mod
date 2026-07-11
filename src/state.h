/*
 * state.h - daemon runtime state machine and polling loop.
 */

#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#include "catalog.h"
#include "config.h"
#include "player.h"
#include "resume.h"

/* New refactor enum values plus compatibility aliases for the older state.c. */
typedef enum {
    STATE_IDLE = 0,
    STATE_BOOK_OPENED,
    STATE_TRACK_LOADING,
    STATE_TRACK_READY,
    STATE_TRACKING,
    STATE_BOOK_COMPLETED,
    STATE_AUDIOBOOK_TRACKING = STATE_TRACKING,
    STATE_AUTOSTART_TRIGGERED = STATE_BOOK_OPENED,
    STATE_RESTORE_IN_PROGRESS = STATE_TRACK_LOADING,
    STATE_RESTORE_FAILED = STATE_TRACK_READY,
    STATE_COMPLETED = STATE_BOOK_COMPLETED,
} daemon_state;

typedef struct {
    daemon_state state;

    /* Path tracking */
    char last_path[512];
    char restored_path[512];
    char completed_saved_path[512];
    char completed_start_over_path[512];
    char deferred_overwrite_path[512];

    /* Active book context */
    char active_root[512];
    char active_book_key[128];
    int  active_track_index;
    int  active_track_count;
    int  active_media_id;
    uint32_t active_saved_pos;
    uint32_t active_target_pos;

    /* State timing */
    time_t state_entered_at;
    time_t loading_deadline_at;
    time_t last_save_at;
    uint32_t last_position_ms;

    /* Save bucketing */
    int last_saved_bucket;
    uint32_t position_protected_until_ms;
    time_t last_paused_at;

    /* Compatibility fields retained for the older state.c implementation. */
    time_t   book_title_autostart_until;
    uint32_t book_title_autostart_seq;
    char     book_title_autostart_reset_key[32];
    time_t   book_title_context_until;
    char     book_title_restore_wait_log_key[128];
    char     book_title_pre_restore_log_key[128];
    uint32_t last_book_title_seq;
    time_t   last_book_title_marker_poll_at;
    uint64_t book_title_arm_deadline_ms;
    uint64_t book_title_arm_next_poll_ms;
    bool     book_title_arm_active;

    char     restore_failed_path[512];
    time_t   restore_failed_at;
    char     restore_failed_kind[16];
    uint32_t restore_failed_saved_pos;
    char     restore_seek_failed_key[128];
    int      restore_seek_failed_count;
    char     failed_restore_skip_log_bucket[128];

    time_t   audiobook_back_guard_until;
    time_t   audiobook_back_guard_seen_at;
    time_t   audiobook_back_guard_last_fire_at;

    int      helper_failures;

    char     autotap_last_path[512];
    time_t   autotap_fired_at;
    time_t   autotap_fast_poll_until;
    bool     idle_autotap_fired;

    /* Restore / loading attempts */
    int loading_retries;
    int restore_attempts;
    bool restore_requested;
    bool restore_applied;
    bool restore_verified;

    /* Diagnostics */
    time_t diag_last_log_at;
    int diag_loops;
    int diag_audiobook_loops;
    int diag_non_audiobook_loops;
    int diag_path_previews;
    int diag_marker_polls;
    int diag_marker_skips;
    int diag_position_reads;
    int diag_saves;
    int diag_state_transitions;
    int diag_restore_attempts;
    int diag_restore_failures;
    int diag_autotap_fired;
    int diag_autotap_skipped;
} daemon_runtime;

void state_init(daemon_runtime *rt);
uint32_t state_poll_cycle(daemon_runtime *rt, const daemon_config *cfg,
                          catalog_db *cat);

/* Compatibility helpers used by the older state.c implementation. */
bool state_autostart_active(const daemon_runtime *rt);
bool state_context_active(const daemon_runtime *rt, time_t now);
bool state_should_poll_marker(const daemon_runtime *rt,
                              const daemon_config *cfg,
                              const char *path_preview, time_t now);
int state_maybe_autostart(daemon_runtime *rt, const daemon_config *cfg,
                          catalog_db *cat, uint32_t seq);
void state_clear_autostart(daemon_runtime *rt);
int state_direct_geometry(const daemon_config *cfg,
                          int saved_index,
                          int *out_swipes, int *out_row);
int state_tap_track_index(daemon_runtime *rt, const daemon_config *cfg,
                          int saved_index, const char *log_label);
int state_verify_selected_track(daemon_runtime *rt, const daemon_config *cfg,
                                catalog_db *cat,
                                const char *saved_path, int saved_index,
                                int selected_row, const char *log_label,
                                const char *path_before_select);
int state_direct_open_trigger(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              pid_t pid, int saved_index,
                              const char *saved_path,
                              const char *path_before_select,
                              const char *log_label);
int state_direct_track_select(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              int current_index, int saved_index,
                              const char *saved_path);
int state_visible_track_select(daemon_runtime *rt, const daemon_config *cfg,
                               catalog_db *cat,
                               int current_index, int saved_index,
                               const char *saved_path);
int state_direct_start_saved(daemon_runtime *rt, const daemon_config *cfg,
                             catalog_db *cat,
                             pid_t pid, uint32_t track_list_ptr,
                             uint32_t catalog_scan_ptr,
                             int allow_memscan_root);
int state_near_miss_transport(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              const char *saved_path, int saved_index,
                              const char *reason);
int state_maybe_restore_track(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat,
                              const char *current_path, uint32_t pos);
int state_log_bucket(const daemon_config *cfg, uint32_t pos);
void state_log_restore_wait(daemon_runtime *rt, const daemon_config *cfg,
                           const char *path, uint32_t pos);
void state_log_pre_restore_skip(daemon_runtime *rt, const daemon_config *cfg,
                                 const char *path, uint32_t pos);
int state_settle_ticks(const daemon_config *cfg);

void state_book_root(const char *path, char *out_root, size_t out_len);
bool state_same_book_root(const char *path, const char *root2);
void state_diag_inc(daemon_runtime *rt, int *counter);
void state_diag_log(daemon_runtime *rt, const daemon_config *cfg, time_t now);

#endif /* STATE_H */
