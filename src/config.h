/*
 * config.h — daemon configuration: struct, defaults, env-var overrides
 *
 * Section 2.1 of the C rewrite spec.  All 80+ configurable values are
 * defined here.  config_load() populates a daemon_config from compiled
 * defaults, an optional config file, and AUDIOBOOK_* environment variables.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    /* Paths */
    char base_dir[256];
    char store_dir[264];
    char log_path[256];
    char pid_file[256];
    char catalog_path[256];
    char catalog_albums_path[256];
    char catalog_books_path[256];
    char user_ini_path[64];
    char helper_path[256];
    char memscan_helper_path[256];
    char direct_open_helper_path[256];
    char touch_event_node[32];
    char key_next_event_node[32];
    char key_prev_event_node[32];

    /* Touch event file paths */
    char touch_next_event_file[256];
    char touch_first_track_event_file[256];
    char touch_first_track_down_event_file[256];
    char touch_first_track_move_event_file[256];
    char touch_first_track_up_event_file[256];
    char touch_back_event_file[256];
    char touch_track_row_event_files[5][256];
    char touch_track_swipe_down_event_file[256];
    char touch_track_swipe_move_event_files[6][256];
    char touch_track_swipe_up_event_file[256];
    char key_next_event_file[256];
    char key_prev_event_file[256];

    /* Timing (milliseconds or seconds, see field names) */
    uint32_t interval_seconds;
    uint32_t idle_interval_seconds;
    uint32_t book_title_marker_idle_poll_seconds;
    uint32_t book_title_marker_music_poll_seconds;
    uint32_t diagnostics_interval_seconds;
    uint32_t min_save_ms;
    uint32_t save_bucket_ms;
    uint32_t restore_only_before_ms;
    uint32_t restore_min_ms;
    uint32_t restore_rewind_ms;
    uint32_t restore_retry_after_failure_seconds;
    uint32_t restore_retry_max_after_failure_seconds;
    uint32_t failed_restore_skip_log_bucket_ms;
    uint32_t new_track_commit_ms;
    uint32_t backward_save_guard_ms;
    uint32_t completed_end_threshold_ms;
    uint32_t helper_timeout_seconds;
    uint32_t helper_max_consecutive_failures;
    uint32_t helper_failure_backoff_seconds;

    /* Memory addresses */
    uint32_t player_position_addr;
    uint32_t player_duration_addr;
    uint32_t book_title_marker_addr;
    uint32_t book_title_memscan_addr;
    uint32_t book_title_memscan_bytes;
    uint32_t book_title_source_magic;
    uint32_t direct_open_probe_addr;
    uint32_t direct_open_scratch_addr;
    uint32_t direct_open_timeout_ms;
    uint32_t direct_open_arm_delay_us;

    /* Book-title autostart */
    uint8_t  book_title_autostart_enabled;
    uint8_t  book_title_memscan_enabled;
    uint8_t  book_title_autostart_require_path;
    uint8_t  book_title_direct_track_select_enabled;
    uint8_t  book_title_direct_track_preplay_enabled;
    uint8_t  book_title_direct_track_calibrate_enabled;
    uint8_t  book_title_direct_track_recovery_transport_enabled;
    uint8_t  book_title_direct_open_enabled;
    uint8_t  book_title_direct_track_return_delay_seconds;
    uint8_t  book_title_direct_track_swipe_settle_seconds;
    uint16_t book_title_direct_track_max_swipes;
    uint8_t  book_title_direct_track_visible_rows;
    uint8_t  book_title_direct_track_rows_per_swipe;
    uint16_t book_title_direct_track_recovery_max_steps;
    uint32_t book_title_autostart_delay_seconds;
    uint32_t book_title_launcher_tracklist_wait_seconds;
    uint32_t book_title_track_list_offset;
    uint32_t book_title_track_list_scan_bytes;
    uint32_t book_title_catalog_scan_ptr_offset;
    uint32_t book_title_catalog_scan_bytes;
    uint32_t book_title_context_seconds;
    uint32_t book_title_restore_log_bucket_ms;

    /* Track restore */
    uint8_t  restore_enabled;
    uint8_t  track_restore_enabled;
    uint16_t track_restore_max_steps;
    uint8_t  track_restore_key_fallback_enabled;
    uint8_t  track_restore_near_miss_transport_enabled;
    uint8_t  track_restore_near_miss_max_steps;
    uint8_t  track_restore_first_track_entry_enabled;
    uint32_t track_restore_first_track_entry_max_ms;
    uint32_t track_switch_settle_seconds;
    uint32_t track_switch_poll_us;
    uint32_t touch_first_track_hold_us;
    uint32_t touch_track_swipe_phase_us;

    /* UI seek */
    uint8_t  ui_seek_fallback_enabled;
    uint16_t ui_seek_bar_x_min;
    uint16_t ui_seek_bar_x_max;
    uint16_t ui_seek_bar_y;
    uint32_t ui_seek_min_duration_ms;
    uint32_t ui_seek_verify_delay_seconds;
    uint32_t ui_seek_verify_tolerance_ms;
    uint8_t  ui_seek_touch_frames;
    uint8_t  ui_seek_screen_guard_enabled;
    uint16_t ui_seek_screen_min_bar_pixels;
    uint16_t ui_seek_fb_stride;

    /* Play mode */
    uint8_t  play_mode_enforce_enabled;
    uint8_t  play_mode_target;
    uint32_t play_mode_user_ini_offset;
    uint8_t  play_mode_max_taps;
    uint16_t play_mode_touch_x;
    uint16_t play_mode_touch_y;
    uint8_t  play_mode_settle_seconds;
    uint8_t  play_mode_screen_guard_enabled;

    /* Back guard */
    uint8_t  back_guard_enabled;
    uint32_t back_guard_window_seconds;
    uint32_t back_guard_after_screen_seconds;
    uint32_t back_guard_idle_interval_seconds;
    uint8_t  back_guard_settle_seconds;
    uint8_t  back_guard_extra_backs;
    uint16_t back_guard_subheader_min_white;
    uint16_t back_guard_subheader_max_white;
    uint16_t back_guard_header_min_white;
    uint16_t back_guard_back_arrow_min_white;

    /* Position source */
    uint8_t  position_source;   /* 0=memory, 1=helper */

    /* Logging */
    uint32_t log_max_bytes;

    /* Auto-tap (Phase 2: auto-tap first track when .m3u opens) */
    uint8_t  autotap_enabled;            /* default: 1 — auto-tap first track after .m3u open */
    uint32_t autotap_delay_ms;           /* default: 500 — ms to wait after playlist opens before tapping */
    uint32_t autotap_max_wait_ms;        /* default: 3000 — max wait for screen render before giving up */
    uint8_t  autotap_require_views_path; /* default: 1 — only auto-tap from _views/ paths */

    /* Shadow mode (migration) */
    uint8_t  shadow_mode;       /* 0=act, 1=log-only */

    /* Source-only (for testing) */
    uint8_t  source_only;       /* skip main() */
} daemon_config;

/* API */
int  config_load(daemon_config *cfg, const char *config_file_path);
int  config_load_file(daemon_config *cfg, const char *path);
void config_free(daemon_config *cfg);
void config_log_summary(const daemon_config *cfg);
void config_print_help(void);

#endif /* CONFIG_H */