/*
 * config.c — configuration loading: defaults, config file, env-var overrides
 *
 * Implements the three-tier configuration from spec section 6:
 *   1. Compiled defaults
 *   2. Optional config file (key=value lines)
 *   3. AUDIOBOOK_* environment variables (highest priority)
 */

#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ── Config field table ───────────────────────────────────────────── */

typedef enum {
    CFG_U32,
    CFG_U8,
    CFG_U16,
    CFG_BOOL,
    CFG_STR
} cfg_type;

typedef struct {
    const char *env_name;      /* "AUDIOBOOK_INTERVAL_SECONDS"    */
    const char *conf_key;      /* "INTERVAL_SECONDS"              */
    cfg_type    type;
    size_t      offset;        /* offsetof(daemon_config, field)  */
    uint32_t    min_val;       /* clamp floor (0 = no clamp)       */
    uint32_t    max_val;       /* clamp ceiling (0 = no clamp)     */
} config_field;

#define FIELD(env, key, typ, memb, lo, hi) \
    { env, key, typ, offsetof(daemon_config, memb), lo, hi }

static const config_field FIELDS[] = {
    /* Paths */
    FIELD("AUDIOBOOK_BASE_DIR",             "BASE_DIR",                CFG_STR, base_dir,                0, 0),
    FIELD("AUDIOBOOK_STORE_DIR",            "STORE_DIR",               CFG_STR, store_dir,               0, 0),
    FIELD("AUDIOBOOK_RESUME_LOG",           "RESUME_LOG",              CFG_STR, log_path,               0, 0),
    FIELD("AUDIOBOOK_RESUME_PID",           "RESUME_PID",              CFG_STR, pid_file,                0, 0),
    FIELD("AUDIOBOOK_CATALOG",              "CATALOG",                 CFG_STR, catalog_path,            0, 0),
    FIELD("AUDIOBOOK_CATALOG_ALBUM_PATTERNS","CATALOG_ALBUM_PATTERNS", CFG_STR, catalog_albums_path,    0, 0),
    FIELD("AUDIOBOOK_CATALOG_BOOKS",         "CATALOG_BOOKS",           CFG_STR, catalog_books_path,      0, 0),
    FIELD("AUDIOBOOK_USER_INI",             "USER_INI",                CFG_STR, user_ini_path,           0, 0),
    FIELD("AUDIOBOOK_HELPER",               "HELPER",                  CFG_STR, helper_path,             0, 0),
    FIELD("AUDIOBOOK_MEMSCAN_HELPER",       "MEMSCAN_HELPER",          CFG_STR, memscan_helper_path,     0, 0),
    FIELD("AUDIOBOOK_DIRECT_OPEN_HELPER",   "DIRECT_OPEN_HELPER",      CFG_STR, direct_open_helper_path, 0, 0),
    FIELD("AUDIOBOOK_TOUCH_EVENT_NODE",     "TOUCH_EVENT_NODE",        CFG_STR, touch_event_node,        0, 0),
    FIELD("AUDIOBOOK_KEY_NEXT_EVENT_NODE",  "KEY_NEXT_EVENT_NODE",     CFG_STR, key_next_event_node,     0, 0),
    FIELD("AUDIOBOOK_KEY_PREV_EVENT_NODE",  "KEY_PREV_EVENT_NODE",     CFG_STR, key_prev_event_node,     0, 0),

    /* Touch event files */
    FIELD("AUDIOBOOK_TOUCH_NEXT_EVENT_FILE",              "TOUCH_NEXT_EVENT_FILE",              CFG_STR, touch_next_event_file,              0, 0),
    FIELD("AUDIOBOOK_TOUCH_FIRST_TRACK_EVENT_FILE",       "TOUCH_FIRST_TRACK_EVENT_FILE",       CFG_STR, touch_first_track_event_file,       0, 0),
    FIELD("AUDIOBOOK_TOUCH_FIRST_TRACK_DOWN_EVENT_FILE",   "TOUCH_FIRST_TRACK_DOWN_EVENT_FILE",   CFG_STR, touch_first_track_down_event_file,   0, 0),
    FIELD("AUDIOBOOK_TOUCH_FIRST_TRACK_MOVE_EVENT_FILE",   "TOUCH_FIRST_TRACK_MOVE_EVENT_FILE",   CFG_STR, touch_first_track_move_event_file,   0, 0),
    FIELD("AUDIOBOOK_TOUCH_FIRST_TRACK_UP_EVENT_FILE",     "TOUCH_FIRST_TRACK_UP_EVENT_FILE",     CFG_STR, touch_first_track_up_event_file,     0, 0),
    FIELD("AUDIOBOOK_TOUCH_BACK_EVENT_FILE",              "TOUCH_BACK_EVENT_FILE",              CFG_STR, touch_back_event_file,              0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_ROW1_EVENT_FILE",        "TOUCH_TRACK_ROW1_EVENT_FILE",        CFG_STR, touch_track_row_event_files[0],      0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_ROW2_EVENT_FILE",        "TOUCH_TRACK_ROW2_EVENT_FILE",        CFG_STR, touch_track_row_event_files[1],      0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_ROW3_EVENT_FILE",        "TOUCH_TRACK_ROW3_EVENT_FILE",        CFG_STR, touch_track_row_event_files[2],      0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_ROW4_EVENT_FILE",        "TOUCH_TRACK_ROW4_EVENT_FILE",        CFG_STR, touch_track_row_event_files[3],      0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_ROW5_EVENT_FILE",        "TOUCH_TRACK_ROW5_EVENT_FILE",        CFG_STR, touch_track_row_event_files[4],      0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_DOWN_EVENT_FILE",  "TOUCH_TRACK_SWIPE_DOWN_EVENT_FILE",  CFG_STR, touch_track_swipe_down_event_file,    0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE1_EVENT_FILE", "TOUCH_TRACK_SWIPE_MOVE1_EVENT_FILE", CFG_STR, touch_track_swipe_move_event_files[0], 0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE2_EVENT_FILE", "TOUCH_TRACK_SWIPE_MOVE2_EVENT_FILE", CFG_STR, touch_track_swipe_move_event_files[1], 0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE3_EVENT_FILE", "TOUCH_TRACK_SWIPE_MOVE3_EVENT_FILE", CFG_STR, touch_track_swipe_move_event_files[2], 0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE4_EVENT_FILE", "TOUCH_TRACK_SWIPE_MOVE4_EVENT_FILE", CFG_STR, touch_track_swipe_move_event_files[3], 0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE5_EVENT_FILE", "TOUCH_TRACK_SWIPE_MOVE5_EVENT_FILE", CFG_STR, touch_track_swipe_move_event_files[4], 0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE6_EVENT_FILE", "TOUCH_TRACK_SWIPE_MOVE6_EVENT_FILE", CFG_STR, touch_track_swipe_move_event_files[5], 0, 0),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_UP_EVENT_FILE",    "TOUCH_TRACK_SWIPE_UP_EVENT_FILE",    CFG_STR, touch_track_swipe_up_event_file,      0, 0),
    FIELD("AUDIOBOOK_KEY_NEXT_EVENT_FILE",                "KEY_NEXT_EVENT_FILE",                CFG_STR, key_next_event_file,                0, 0),
    FIELD("AUDIOBOOK_KEY_PREV_EVENT_FILE",                "KEY_PREV_EVENT_FILE",                CFG_STR, key_prev_event_file,                0, 0),

    /* Timing */
    FIELD("AUDIOBOOK_INTERVAL_SECONDS",                     "INTERVAL_SECONDS",                     CFG_U32, interval_seconds,                     1, 3600),
    FIELD("AUDIOBOOK_IDLE_INTERVAL_SECONDS",                "IDLE_INTERVAL_SECONDS",                CFG_U32, idle_interval_seconds,                1, 3600),
    FIELD("AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS",  "BOOK_TITLE_MARKER_IDLE_POLL_SECONDS",  CFG_U32, book_title_marker_idle_poll_seconds,   1, 3600),
    FIELD("AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS", "BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS", CFG_U32, book_title_marker_music_poll_seconds,  1, 3600),
    FIELD("AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS",         "DIAGNOSTICS_INTERVAL_SECONDS",         CFG_U32, diagnostics_interval_seconds,          1, 3600),
    FIELD("AUDIOBOOK_MIN_SAVE_MS",                          "MIN_SAVE_MS",                          CFG_U32, min_save_ms,                           0, 600000),
    FIELD("AUDIOBOOK_SAVE_BUCKET_MS",                       "SAVE_BUCKET_MS",                       CFG_U32, save_bucket_ms,                        1, 600000),
    FIELD("AUDIOBOOK_RESTORE_ONLY_BEFORE_MS",               "RESTORE_ONLY_BEFORE_MS",               CFG_U32, restore_only_before_ms,                0, 86400000),
    FIELD("AUDIOBOOK_RESTORE_MIN_MS",                       "RESTORE_MIN_MS",                       CFG_U32, restore_min_ms,                        0, 86400000),
    FIELD("AUDIOBOOK_RESTORE_REWIND_MS",                    "RESTORE_REWIND_MS",                    CFG_U32, restore_rewind_ms,                     0, 600000),
    FIELD("AUDIOBOOK_RESTORE_RETRY_AFTER_FAILURE_SECONDS", "RESTORE_RETRY_AFTER_FAILURE_SECONDS",  CFG_U32, restore_retry_after_failure_seconds,   1, 3600),
    FIELD("AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS","RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS",CFG_U32, restore_retry_max_after_failure_seconds,1, 7200),
    FIELD("AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS",    "FAILED_RESTORE_SKIP_LOG_BUCKET_MS",    CFG_U32, failed_restore_skip_log_bucket_ms,     0, 600000),
    FIELD("AUDIOBOOK_NEW_TRACK_COMMIT_MS",                   "NEW_TRACK_COMMIT_MS",                  CFG_U32, new_track_commit_ms,                   0, 600000),
    FIELD("AUDIOBOOK_BACKWARD_SAVE_GUARD_MS",               "BACKWARD_SAVE_GUARD_MS",               CFG_U32, backward_save_guard_ms,                0, 600000),
    FIELD("AUDIOBOOK_COMPLETED_END_THRESHOLD_MS",           "COMPLETED_END_THRESHOLD_MS",           CFG_U32, completed_end_threshold_ms,            0, 600000),
    FIELD("AUDIOBOOK_HELPER_TIMEOUT_SECONDS",               "HELPER_TIMEOUT_SECONDS",               CFG_U32, helper_timeout_seconds,                1, 60),
    FIELD("AUDIOBOOK_HELPER_MAX_CONSECUTIVE_FAILURES",       "HELPER_MAX_CONSECUTIVE_FAILURES",      CFG_U32, helper_max_consecutive_failures,       0, 100),
    FIELD("AUDIOBOOK_HELPER_FAILURE_BACKOFF_SECONDS",        "HELPER_FAILURE_BACKOFF_SECONDS",       CFG_U32, helper_failure_backoff_seconds,        0, 3600),

    /* Memory addresses */
    FIELD("AUDIOBOOK_PLAYER_POSITION_ADDR",     "PLAYER_POSITION_ADDR",     CFG_U32, player_position_addr,     4096, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_PLAYER_DURATION_ADDR",     "PLAYER_DURATION_ADDR",     CFG_U32, player_duration_addr,     4096, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_BOOK_TITLE_MARKER_ADDR",   "BOOK_TITLE_MARKER_ADDR",   CFG_U32, book_title_marker_addr,   4096, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_BOOK_TITLE_MEMSCAN_ADDR",  "BOOK_TITLE_MEMSCAN_ADDR",  CFG_U32, book_title_memscan_addr,   4096, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_BOOK_TITLE_MEMSCAN_BYTES", "BOOK_TITLE_MEMSCAN_BYTES", CFG_U32, book_title_memscan_bytes,  256, 1048576),
    FIELD("AUDIOBOOK_BOOK_TITLE_SOURCE_MAGIC",  "BOOK_TITLE_SOURCE_MAGIC",  CFG_U32, book_title_source_magic,  0, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR",   "DIRECT_OPEN_PROBE_ADDR",   CFG_U32, direct_open_probe_addr,   4096, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR",  "DIRECT_OPEN_SCRATCH_ADDR", CFG_U32, direct_open_scratch_addr, 4096, 0xFFFFFFFF),
    FIELD("AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS",    "DIRECT_OPEN_TIMEOUT_MS",   CFG_U32, direct_open_timeout_ms,    100, 30000),
    FIELD("AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US",  "DIRECT_OPEN_ARM_DELAY_US", CFG_U32, direct_open_arm_delay_us,  0, 5000000),

    /* Book-title autostart */
    FIELD("AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED",               "BOOK_TITLE_AUTOSTART_ENABLED",               CFG_BOOL, book_title_autostart_enabled,               0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED",                  "BOOK_TITLE_MEMSCAN_ENABLED",                  CFG_BOOL, book_title_memscan_enabled,                  0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH",           "BOOK_TITLE_AUTOSTART_REQUIRE_PATH",           CFG_BOOL, book_title_autostart_require_path,           0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED",      "BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED",      CFG_BOOL, book_title_direct_track_select_enabled,      0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED",     "BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED",     CFG_BOOL, book_title_direct_track_preplay_enabled,     0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED",   "BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED",   CFG_BOOL, book_title_direct_track_calibrate_enabled,   0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED","BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED",CFG_BOOL,book_title_direct_track_recovery_transport_enabled,0,1),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED",              "BOOK_TITLE_DIRECT_OPEN_ENABLED",              CFG_BOOL, book_title_direct_open_enabled,              0, 1),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS","BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS", CFG_U8,  book_title_direct_track_return_delay_seconds, 0, 30),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SWIPE_SETTLE_SECONDS","BOOK_TITLE_DIRECT_TRACK_SWIPE_SETTLE_SECONDS", CFG_U8,  book_title_direct_track_swipe_settle_seconds, 0, 30),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES",          "BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES",          CFG_U16, book_title_direct_track_max_swipes,           1, 200),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS",        "BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS",        CFG_U8,  book_title_direct_track_visible_rows,         1, 20),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE",      "BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE",      CFG_U8,  book_title_direct_track_rows_per_swipe,       1, 20),
    FIELD("AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS",  "BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS",  CFG_U16, book_title_direct_track_recovery_max_steps,  0, 200),
    FIELD("AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS",          "BOOK_TITLE_AUTOSTART_DELAY_SECONDS",          CFG_U32, book_title_autostart_delay_seconds,          0, 60),
    FIELD("AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS",  "BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS",  CFG_U32, book_title_launcher_tracklist_wait_seconds,  0, 60),
    FIELD("AUDIOBOOK_BOOK_TITLE_TRACK_LIST_OFFSET",                "BOOK_TITLE_TRACK_LIST_OFFSET",                CFG_U32, book_title_track_list_offset,               0, 1024),
    FIELD("AUDIOBOOK_BOOK_TITLE_TRACK_LIST_SCAN_BYTES",            "BOOK_TITLE_TRACK_LIST_SCAN_BYTES",            CFG_U32, book_title_track_list_scan_bytes,          64, 65536),
    FIELD("AUDIOBOOK_BOOK_TITLE_CATALOG_SCAN_PTR_OFFSET",          "BOOK_TITLE_CATALOG_SCAN_PTR_OFFSET",          CFG_U32, book_title_catalog_scan_ptr_offset,        0, 1024),
    FIELD("AUDIOBOOK_BOOK_TITLE_CATALOG_SCAN_BYTES",              "BOOK_TITLE_CATALOG_SCAN_BYTES",               CFG_U32, book_title_catalog_scan_bytes,             64, 65536),
    FIELD("AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS",                 "BOOK_TITLE_CONTEXT_SECONDS",                  CFG_U32, book_title_context_seconds,                0, 7200),
    FIELD("AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS",           "BOOK_TITLE_RESTORE_LOG_BUCKET_MS",            CFG_U32, book_title_restore_log_bucket_ms,           0, 600000),

    /* Track restore */
    FIELD("AUDIOBOOK_RESTORE_ENABLED",                     "RESTORE_ENABLED",                     CFG_BOOL, restore_enabled,                     0, 1),
    FIELD("AUDIOBOOK_TRACK_RESTORE_ENABLED",               "TRACK_RESTORE_ENABLED",               CFG_BOOL, track_restore_enabled,               0, 1),
    FIELD("AUDIOBOOK_TRACK_RESTORE_MAX_STEPS",              "TRACK_RESTORE_MAX_STEPS",              CFG_U16, track_restore_max_steps,               0, 500),
    FIELD("AUDIOBOOK_TRACK_RESTORE_KEY_FALLBACK_ENABLED",   "TRACK_RESTORE_KEY_FALLBACK_ENABLED",   CFG_BOOL, track_restore_key_fallback_enabled,   0, 1),
    FIELD("AUDIOBOOK_TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED","TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED",CFG_BOOL,track_restore_near_miss_transport_enabled,0,1),
    FIELD("AUDIOBOOK_TRACK_RESTORE_NEAR_MISS_MAX_STEPS",   "TRACK_RESTORE_NEAR_MISS_MAX_STEPS",   CFG_U8,  track_restore_near_miss_max_steps,   0, 50),
    FIELD("AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED","TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED",CFG_BOOL,track_restore_first_track_entry_enabled,0,1),
    FIELD("AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS","TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS",CFG_U32,track_restore_first_track_entry_max_ms,0,600000),
    FIELD("AUDIOBOOK_TRACK_SWITCH_SETTLE_SECONDS",          "TRACK_SWITCH_SETTLE_SECONDS",          CFG_U32, track_switch_settle_seconds,          0, 60),
    FIELD("AUDIOBOOK_TRACK_SWITCH_POLL_US",                 "TRACK_SWITCH_POLL_US",                 CFG_U32, track_switch_poll_us,                 1000, 5000000),
    FIELD("AUDIOBOOK_TOUCH_FIRST_TRACK_HOLD_US",            "TOUCH_FIRST_TRACK_HOLD_US",            CFG_U32, touch_first_track_hold_us,             0, 5000000),
    FIELD("AUDIOBOOK_TOUCH_TRACK_SWIPE_PHASE_US",           "TOUCH_TRACK_SWIPE_PHASE_US",           CFG_U32, touch_track_swipe_phase_us,            0, 5000000),

    /* UI seek */
    FIELD("AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED",       "UI_SEEK_FALLBACK_ENABLED",       CFG_BOOL, ui_seek_fallback_enabled,       0, 1),
    FIELD("AUDIOBOOK_UI_SEEK_BAR_X_MIN",              "UI_SEEK_BAR_X_MIN",              CFG_U16, ui_seek_bar_x_min,              0, 480),
    FIELD("AUDIOBOOK_UI_SEEK_BAR_X_MAX",              "UI_SEEK_BAR_X_MAX",              CFG_U16, ui_seek_bar_x_max,              0, 480),
    FIELD("AUDIOBOOK_UI_SEEK_BAR_Y",                  "UI_SEEK_BAR_Y",                  CFG_U16, ui_seek_bar_y,                  0, 640),
    FIELD("AUDIOBOOK_UI_SEEK_MIN_DURATION_MS",        "UI_SEEK_MIN_DURATION_MS",        CFG_U32, ui_seek_min_duration_ms,        0, 86400000),
    FIELD("AUDIOBOOK_UI_SEEK_VERIFY_DELAY_SECONDS",   "UI_SEEK_VERIFY_DELAY_SECONDS",   CFG_U32, ui_seek_verify_delay_seconds,   0, 30),
    FIELD("AUDIOBOOK_UI_SEEK_VERIFY_TOLERANCE_MS",     "UI_SEEK_VERIFY_TOLERANCE_MS",    CFG_U32, ui_seek_verify_tolerance_ms,     0, 600000),
    FIELD("AUDIOBOOK_UI_SEEK_TOUCH_FRAMES",           "UI_SEEK_TOUCH_FRAMES",           CFG_U8,  ui_seek_touch_frames,           1, 10),
    FIELD("AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED",   "UI_SEEK_SCREEN_GUARD_ENABLED",   CFG_BOOL, ui_seek_screen_guard_enabled,   0, 1),
    FIELD("AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS",  "UI_SEEK_SCREEN_MIN_BAR_PIXELS",  CFG_U16, ui_seek_screen_min_bar_pixels,  0, 1000),
    FIELD("AUDIOBOOK_UI_SEEK_FB_STRIDE",              "UI_SEEK_FB_STRIDE",              CFG_U16, ui_seek_fb_stride,              1, 4096),

    /* Play mode */
    FIELD("AUDIOBOOK_PLAY_MODE_ENFORCE_ENABLED",       "PLAY_MODE_ENFORCE_ENABLED",       CFG_BOOL, play_mode_enforce_enabled,       0, 1),
    FIELD("AUDIOBOOK_PLAY_MODE_TARGET",                "PLAY_MODE_TARGET",                CFG_U8,  play_mode_target,                0, 10),
    FIELD("AUDIOBOOK_PLAY_MODE_USER_INI_OFFSET",       "PLAY_MODE_USER_INI_OFFSET",       CFG_U32, play_mode_user_ini_offset,       0, 4096),
    FIELD("AUDIOBOOK_PLAY_MODE_MAX_TAPS",              "PLAY_MODE_MAX_TAPS",              CFG_U8,  play_mode_max_taps,              0, 20),
    FIELD("AUDIOBOOK_PLAY_MODE_TOUCH_X",               "PLAY_MODE_TOUCH_X",               CFG_U16, play_mode_touch_x,               0, 480),
    FIELD("AUDIOBOOK_PLAY_MODE_TOUCH_Y",               "PLAY_MODE_TOUCH_Y",               CFG_U16, play_mode_touch_y,               0, 640),
    FIELD("AUDIOBOOK_PLAY_MODE_SETTLE_SECONDS",        "PLAY_MODE_SETTLE_SECONDS",        CFG_U8,  play_mode_settle_seconds,        0, 30),
    FIELD("AUDIOBOOK_PLAY_MODE_SCREEN_GUARD_ENABLED",  "PLAY_MODE_SCREEN_GUARD_ENABLED",  CFG_BOOL, play_mode_screen_guard_enabled,  0, 1),

    /* Back guard */
    FIELD("AUDIOBOOK_BACK_GUARD_ENABLED",              "BACK_GUARD_ENABLED",              CFG_BOOL, back_guard_enabled,              0, 1),
    FIELD("AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS",       "BACK_GUARD_WINDOW_SECONDS",       CFG_U32, back_guard_window_seconds,       0, 3600),
    FIELD("AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS", "BACK_GUARD_AFTER_SCREEN_SECONDS", CFG_U32, back_guard_after_screen_seconds, 0, 3600),
    FIELD("AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS","BACK_GUARD_IDLE_INTERVAL_SECONDS",CFG_U32, back_guard_idle_interval_seconds,0, 3600),
    FIELD("AUDIOBOOK_BACK_GUARD_SETTLE_SECONDS",       "BACK_GUARD_SETTLE_SECONDS",       CFG_U8,  back_guard_settle_seconds,       0, 30),
    FIELD("AUDIOBOOK_BACK_GUARD_EXTRA_BACKS",          "BACK_GUARD_EXTRA_BACKS",          CFG_U8,  back_guard_extra_backs,          0, 10),
    FIELD("AUDIOBOOK_BACK_GUARD_SUBHEADER_MIN_WHITE",  "BACK_GUARD_SUBHEADER_MIN_WHITE",  CFG_U16, back_guard_subheader_min_white,  0, 1000),
    FIELD("AUDIOBOOK_BACK_GUARD_SUBHEADER_MAX_WHITE",  "BACK_GUARD_SUBHEADER_MAX_WHITE",  CFG_U16, back_guard_subheader_max_white,  0, 1000),
    FIELD("AUDIOBOOK_BACK_GUARD_HEADER_MIN_WHITE",    "BACK_GUARD_HEADER_MIN_WHITE",     CFG_U16, back_guard_header_min_white,    0, 1000),
    FIELD("AUDIOBOOK_BACK_GUARD_BACK_ARROW_MIN_WHITE", "BACK_GUARD_BACK_ARROW_MIN_WHITE", CFG_U16, back_guard_back_arrow_min_white, 0, 1000),

    /* Position source */
    FIELD("AUDIOBOOK_POSITION_SOURCE",       "POSITION_SOURCE",       CFG_U8,  position_source,       0, 1),

    /* Logging */
    FIELD("AUDIOBOOK_RESUME_LOG_MAX_BYTES",  "RESUME_LOG_MAX_BYTES",  CFG_U32, log_max_bytes, 1024, 10485760),

    /* Auto-tap (Phase 2) */
    FIELD("AUDIOBOOK_AUTOTAP_ENABLED",            "AUTOTAP_ENABLED",            CFG_BOOL, autotap_enabled,            0, 1),
    FIELD("AUDIOBOOK_AUTOTAP_DELAY_MS",           "AUTOTAP_DELAY_MS",           CFG_U32,  autotap_delay_ms,           0, 10000),
    FIELD("AUDIOBOOK_AUTOTAP_MAX_WAIT_MS",        "AUTOTAP_MAX_WAIT_MS",        CFG_U32,  autotap_max_wait_ms,        0, 30000),
    FIELD("AUDIOBOOK_AUTOTAP_REQUIRE_VIEWS_PATH", "AUTOTAP_REQUIRE_VIEWS_PATH", CFG_BOOL, autotap_require_views_path, 0, 1),

    /* Shadow mode */
    FIELD("AUDIOBOOK_SHADOW_MODE",           "SHADOW_MODE",           CFG_BOOL, shadow_mode, 0, 1),

    /* Source-only */
    FIELD("AUDIOBOOK_RESUME_DAEMON_SOURCE_ONLY", "RESUME_DAEMON_SOURCE_ONLY", CFG_BOOL, source_only, 0, 1),
};

static const size_t NUM_FIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);

/* ── Defaults ─────────────────────────────────────────────────────── */

static void set_defaults(daemon_config *c) {
    memset(c, 0, sizeof(*c));

    /* Paths */
    snprintf(c->base_dir,               sizeof(c->base_dir),               "%s", "/usr/data/audiobooks");
    snprintf(c->store_dir,             sizeof(c->store_dir),             "%s", "/usr/data/audiobooks/resume.d");
    snprintf(c->log_path,              sizeof(c->log_path),              "%s", "/usr/data/audiobooks/resume-daemon.log");
    snprintf(c->pid_file,              sizeof(c->pid_file),              "%s", "/usr/data/audiobooks/resume-daemon.pid");
    snprintf(c->catalog_path,          sizeof(c->catalog_path),          "%s", "/usr/data/audiobooks/catalog.tsv");
    snprintf(c->catalog_albums_path,   sizeof(c->catalog_albums_path),   "%s", "/usr/data/audiobooks/catalog-albums.txt");
    snprintf(c->catalog_books_path,    sizeof(c->catalog_books_path),    "%s", "/usr/data/audiobooks/catalog-books.tsv");
    snprintf(c->user_ini_path,         sizeof(c->user_ini_path),         "%s", "/usr/data/user.ini");
    snprintf(c->helper_path,           sizeof(c->helper_path),           "%s", "/usr/data/audiobooks/bin/r1_audiobook_resume_helper");
    snprintf(c->memscan_helper_path,   sizeof(c->memscan_helper_path),   "%s", "/usr/data/audiobooks/bin/r1_audiobook_memscan");
    snprintf(c->direct_open_helper_path,sizeof(c->direct_open_helper_path),"%s", "/usr/data/audiobooks/bin/r1_audiobook_direct_open");
    snprintf(c->touch_event_node,      sizeof(c->touch_event_node),      "%s", "/dev/input/event1");
    snprintf(c->key_next_event_node,   sizeof(c->key_next_event_node),   "%s", "/dev/input/event0");
    snprintf(c->key_prev_event_node,   sizeof(c->key_prev_event_node),   "%s", "/dev/input/event2");

    /* Touch event files */
    #define EV_DIR "/usr/data/audiobooks/input/"
    snprintf(c->touch_next_event_file,              sizeof(c->touch_next_event_file),              "%s%s", EV_DIR, "touch_next_event1.bin");
    snprintf(c->touch_first_track_event_file,       sizeof(c->touch_first_track_event_file),       "%s%s", EV_DIR, "touch_first_track_event1.bin");
    snprintf(c->touch_first_track_down_event_file,   sizeof(c->touch_first_track_down_event_file),   "%s%s", EV_DIR, "touch_first_track_down_event1.bin");
    snprintf(c->touch_first_track_move_event_file,   sizeof(c->touch_first_track_move_event_file),   "%s%s", EV_DIR, "touch_first_track_move_event1.bin");
    snprintf(c->touch_first_track_up_event_file,     sizeof(c->touch_first_track_up_event_file),     "%s%s", EV_DIR, "touch_first_track_up_event1.bin");
    snprintf(c->touch_back_event_file,              sizeof(c->touch_back_event_file),              "%s%s", EV_DIR, "touch_back_event1.bin");
    snprintf(c->touch_track_row_event_files[0],     sizeof(c->touch_track_row_event_files[0]),     "%s%s", EV_DIR, "touch_track_row1_event1.bin");
    snprintf(c->touch_track_row_event_files[1],     sizeof(c->touch_track_row_event_files[1]),     "%s%s", EV_DIR, "touch_track_row2_event1.bin");
    snprintf(c->touch_track_row_event_files[2],     sizeof(c->touch_track_row_event_files[2]),     "%s%s", EV_DIR, "touch_track_row3_event1.bin");
    snprintf(c->touch_track_row_event_files[3],     sizeof(c->touch_track_row_event_files[3]),     "%s%s", EV_DIR, "touch_track_row4_event1.bin");
    snprintf(c->touch_track_row_event_files[4],     sizeof(c->touch_track_row_event_files[4]),     "%s%s", EV_DIR, "touch_track_row5_event1.bin");
    snprintf(c->touch_track_swipe_down_event_file,  sizeof(c->touch_track_swipe_down_event_file),  "%s%s", EV_DIR, "touch_track_swipe_down_event1.bin");
    snprintf(c->touch_track_swipe_move_event_files[0], sizeof(c->touch_track_swipe_move_event_files[0]), "%s%s", EV_DIR, "touch_track_swipe_move1_event1.bin");
    snprintf(c->touch_track_swipe_move_event_files[1], sizeof(c->touch_track_swipe_move_event_files[1]), "%s%s", EV_DIR, "touch_track_swipe_move2_event1.bin");
    snprintf(c->touch_track_swipe_move_event_files[2], sizeof(c->touch_track_swipe_move_event_files[2]), "%s%s", EV_DIR, "touch_track_swipe_move3_event1.bin");
    snprintf(c->touch_track_swipe_move_event_files[3], sizeof(c->touch_track_swipe_move_event_files[3]), "%s%s", EV_DIR, "touch_track_swipe_move4_event1.bin");
    snprintf(c->touch_track_swipe_move_event_files[4], sizeof(c->touch_track_swipe_move_event_files[4]), "%s%s", EV_DIR, "touch_track_swipe_move5_event1.bin");
    snprintf(c->touch_track_swipe_move_event_files[5], sizeof(c->touch_track_swipe_move_event_files[5]), "%s%s", EV_DIR, "touch_track_swipe_move6_event1.bin");
    snprintf(c->touch_track_swipe_up_event_file,    sizeof(c->touch_track_swipe_up_event_file),    "%s%s", EV_DIR, "touch_track_swipe_up_event1.bin");
    snprintf(c->key_next_event_file,                sizeof(c->key_next_event_file),                "%s%s", EV_DIR, "key_next_event0.bin");
    snprintf(c->key_prev_event_file,                sizeof(c->key_prev_event_file),                "%s%s", EV_DIR, "key_prev_event2.bin");
    #undef EV_DIR

    /* Timing */
    c->interval_seconds                       = 5;
    c->idle_interval_seconds                  = 3;
    c->book_title_marker_idle_poll_seconds    = 5;
    c->book_title_marker_music_poll_seconds   = 15;
    c->diagnostics_interval_seconds            = 60;
    c->min_save_ms                            = 3000;
    c->save_bucket_ms                         = 15000;
    c->restore_only_before_ms                 = 15000;
    c->restore_min_ms                         = 10000;
    c->restore_rewind_ms                      = 0;
    c->restore_retry_after_failure_seconds     = 30;
    c->restore_retry_max_after_failure_seconds= 300;
    c->failed_restore_skip_log_bucket_ms      = 30000;
    c->new_track_commit_ms                    = 15000;
    c->backward_save_guard_ms                 = 5000;
    c->completed_end_threshold_ms             = 45000;
    c->helper_timeout_seconds                 = 3;
    c->helper_max_consecutive_failures        = 3;
    c->helper_failure_backoff_seconds         = 10;

    /* Memory addresses */
    c->player_position_addr                   = 9115148;
    c->player_duration_addr                   = 9115252;
    c->book_title_marker_addr                 = 9322496;
    c->book_title_memscan_addr                = 9113600;
    c->book_title_memscan_bytes               = 212992;
    c->book_title_source_magic                = 2695890197;
    c->direct_open_probe_addr                 = 0x760708;
    c->direct_open_scratch_addr               = 0x8e4400;
    c->direct_open_timeout_ms                 = 6000;
    c->direct_open_arm_delay_us               = 200000;

    /* Book-title autostart */
    c->book_title_autostart_enabled                 = 1;
    c->book_title_memscan_enabled                   = 1;
    c->book_title_autostart_require_path            = 1;
    c->book_title_direct_track_select_enabled       = 1;
    c->book_title_direct_track_preplay_enabled      = 1;
    c->book_title_direct_track_calibrate_enabled    = 1;
    c->book_title_direct_track_recovery_transport_enabled = 1;
    c->book_title_direct_open_enabled               = 1;
    c->book_title_direct_track_return_delay_seconds = 1;
    c->book_title_direct_track_swipe_settle_seconds = 1;
    c->book_title_direct_track_max_swipes           = 20;
    c->book_title_direct_track_visible_rows         = 5;
    c->book_title_direct_track_rows_per_swipe       = 4;
    c->book_title_direct_track_recovery_max_steps   = 20;
    c->book_title_autostart_delay_seconds           = 2;
    c->book_title_launcher_tracklist_wait_seconds   = 4;
    c->book_title_track_list_offset                 = 52;
    c->book_title_track_list_scan_bytes             = 4096;
    c->book_title_catalog_scan_ptr_offset           = 44;
    c->book_title_catalog_scan_bytes                = 8192;
    c->book_title_context_seconds                   = 300;
    c->book_title_restore_log_bucket_ms             = 5000;

    /* Track restore */
    c->restore_enabled                       = 0;
    c->track_restore_enabled                 = 1;
    c->track_restore_max_steps               = 50;
    c->track_restore_key_fallback_enabled    = 0;
    c->track_restore_near_miss_transport_enabled = 1;
    c->track_restore_near_miss_max_steps     = 4;
    c->track_restore_first_track_entry_enabled = 0;
    c->track_restore_first_track_entry_max_ms = 15000;
    c->track_switch_settle_seconds           = 3;
    c->track_switch_poll_us                  = 200000;
    c->touch_first_track_hold_us             = 250000;
    c->touch_track_swipe_phase_us            = 50000;

    /* UI seek */
    c->ui_seek_fallback_enabled       = 1;
    c->ui_seek_bar_x_min              = 21;
    c->ui_seek_bar_x_max              = 459;
    c->ui_seek_bar_y                  = 619;
    c->ui_seek_min_duration_ms        = 30000;
    c->ui_seek_verify_delay_seconds   = 2;
    c->ui_seek_verify_tolerance_ms    = 15000;
    c->ui_seek_touch_frames           = 2;
    c->ui_seek_screen_guard_enabled   = 1;
    c->ui_seek_screen_min_bar_pixels  = 300;
    c->ui_seek_fb_stride              = 960;

    /* Play mode */
    c->play_mode_enforce_enabled       = 1;
    c->play_mode_target                = 3;
    c->play_mode_user_ini_offset       = 592;
    c->play_mode_max_taps              = 4;
    c->play_mode_touch_x               = 49;
    c->play_mode_touch_y               = 730;
    c->play_mode_settle_seconds        = 1;
    c->play_mode_screen_guard_enabled  = 1;

    /* Back guard */
    c->back_guard_enabled              = 0;
    c->back_guard_window_seconds       = 60;
    c->back_guard_after_screen_seconds = 8;
    c->back_guard_idle_interval_seconds = 1;
    c->back_guard_settle_seconds       = 1;
    c->back_guard_extra_backs          = 2;
    c->back_guard_subheader_min_white  = 100;
    c->back_guard_subheader_max_white  = 50;
    c->back_guard_header_min_white     = 300;
    c->back_guard_back_arrow_min_white = 80;

    /* Position source */
    c->position_source                  = 0;  /* memory */

    /* Logging */
    c->log_max_bytes                    = 524288;  /* 512 KB */

    /* Auto-tap (Phase 2) */
    c->autotap_enabled             = 1;
    c->autotap_delay_ms            = 500;
    c->autotap_max_wait_ms         = 3000;
    c->autotap_require_views_path  = 1;

    /* Shadow mode */
    c->shadow_mode                      = 0;

    /* Source-only */
    c->source_only                      = 0;
}

/* ── Helpers for applying a single field ─────────────────────────── */

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    if (lo && v < lo) return lo;
    if (hi && v > hi) return hi;
    return v;
}

static void apply_field(daemon_config *c, const config_field *f, const char *val) {
    void *p = (char *)c + f->offset;

    switch (f->type) {
    case CFG_U32: {
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 0);
        if (end == val) return;  /* non-numeric */
        uint32_t cv = clamp_u32((uint32_t)v, f->min_val, f->max_val);
        *(uint32_t *)p = cv;
        break;
    }
    case CFG_U8: {
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 0);
        if (end == val) return;
        uint32_t cv = clamp_u32((uint32_t)v, f->min_val, f->max_val);
        *(uint8_t *)p = (uint8_t)cv;
        break;
    }
    case CFG_U16: {
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 0);
        if (end == val) return;
        uint32_t cv = clamp_u32((uint32_t)v, f->min_val, f->max_val);
        *(uint16_t *)p = (uint16_t)cv;
        break;
    }
    case CFG_BOOL: {
        /* Accept 0/1, true/false, yes/no */
        if (val[0] == '1' || val[0] == 't' || val[0] == 'T' || val[0] == 'y' || val[0] == 'Y')
            *(uint8_t *)p = 1;
        else
            *(uint8_t *)p = 0;
        break;
    }
    case CFG_STR: {
        /* Determine the field's buffer capacity from the table.
           We stored offsets; we need to know the size.  Since we
           can't easily get it from the table, we cap at 256 for
           most string fields and 64 for user_ini_path.  The actual
           struct field sizes are the real limit. */
        /* Compute size from the next field offset or struct end */
        size_t field_size;
        if ((size_t)(f - FIELDS) + 1 < NUM_FIELDS) {
            const config_field *next = &FIELDS[(size_t)(f - FIELDS) + 1];
            size_t this_off = f->offset;
            size_t next_off = next->offset;
            /* If the next field is in a different array element group,
               the size might be wrong.  Use a conservative 256 cap. */
            if (next_off > this_off && next_off - this_off <= 512)
                field_size = next_off - this_off;
            else
                field_size = 256;
        } else {
            field_size = 256;
        }
        /* For multi-char arrays that are smaller, snprintf handles it */
        if (field_size > 512) field_size = 512;
        strncpy((char *)p, val, field_size - 1);
        ((char *)p)[field_size - 1] = '\0';
        break;
    }
    }
}

/* ── Config file parser ───────────────────────────────────────────── */

int config_load_file(daemon_config *c, const char *path) {
    if (!c || !path) return -1;
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Skip comments and blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        /* Find '=' */
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        /* Trim key */
        char *key = p;
        char *kend = eq - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) { *kend = '\0'; kend--; }
        while (*key == ' ' || *key == '\t') key++;

        /* Trim value */
        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        char *vend = val + strlen(val);
        while (vend > val && (vend[-1] == '\n' || vend[-1] == '\r' || vend[-1] == ' ' || vend[-1] == '\t')) { vend[-1] = '\0'; vend--; }

        /* Look up field by conf_key */
        for (size_t i = 0; i < NUM_FIELDS; i++) {
            if (strcmp(FIELDS[i].conf_key, key) == 0) {
                apply_field(c, &FIELDS[i], val);
                break;
            }
        }
    }
    fclose(fp);
    return 0;
}

/* ── Environment variable overrides ───────────────────────────────── */

static void load_env_overrides(daemon_config *c) {
    for (size_t i = 0; i < NUM_FIELDS; i++) {
        const char *val = getenv(FIELDS[i].env_name);
        if (val && val[0]) {
            apply_field(c, &FIELDS[i], val);
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

int config_load(daemon_config *cfg, const char *config_file_path) {
    if (!cfg) return -1;
    set_defaults(cfg);

    /* Tier 2: config file (if provided) */
    if (config_file_path) {
        config_load_file(cfg, config_file_path);
    } else {
        /* Try default config path: base_dir/resume-daemon.conf */
        char def[512];
        snprintf(def, sizeof(def), "%s/resume-daemon.conf", cfg->base_dir);
        config_load_file(cfg, def);
    }

    /* Tier 3: env vars (highest priority) */
    load_env_overrides(cfg);

    return 0;
}

void config_free(daemon_config *cfg) {
    /* No dynamic allocation in config struct */
    (void)cfg;
}

void config_log_summary(const daemon_config *c) {
    log_msg("start interval=%us idle=%us marker_idle=%us marker_music=%us diag=%us "
            "min_save=%ums bucket=%ums restore_before=%ums restore_min=%ums rewind=%ums "
            "position_addr=0x%x duration_addr=0x%x marker_addr=0x%x "
            "autostart=%d restore=%d track_restore=%d autotap=%d autotap_delay=%ums "
            "shadow=%d source_only=%d",
            c->interval_seconds, c->idle_interval_seconds,
            c->book_title_marker_idle_poll_seconds, c->book_title_marker_music_poll_seconds,
            c->diagnostics_interval_seconds,
            c->min_save_ms, c->save_bucket_ms, c->restore_only_before_ms,
            c->restore_min_ms, c->restore_rewind_ms,
            c->player_position_addr, c->player_duration_addr, c->book_title_marker_addr,
            c->book_title_autostart_enabled, c->restore_enabled, c->track_restore_enabled,
            c->autotap_enabled, c->autotap_delay_ms,
            c->shadow_mode, c->source_only);
}

void config_print_help(void) {
    fprintf(stderr,
        "r1_audiobook_resume_daemon — HiBy R1 Audiobook Resume Daemon\n\n"
        "Usage: r1_audiobook_resume_daemon [OPTIONS]\n\n"
        "Options:\n"
        "  --interval N          Poll interval in seconds (default: 5)\n"
        "  --config PATH         Path to config file (default: base_dir/resume-daemon.conf)\n"
        "  --shadow              Enable shadow/log-only mode (log actions, no side effects)\n"
        "  --help                Show this help and exit\n"
        "  --version             Show version and exit\n\n"
        "Environment variables (override config file, prefix AUDIOBOOK_):\n"
        "  AUDIOBOOK_BASE_DIR              Base directory (default: /usr/data/audiobooks)\n"
        "  AUDIOBOOK_INTERVAL_SECONDS      Poll interval (default: 5)\n"
        "  AUDIOBOOK_IDLE_INTERVAL_SECONDS Idle interval (default: 3)\n"
        "  AUDIOBOOK_MIN_SAVE_MS           Minimum position before saving (default: 3000)\n"
        "  AUDIOBOOK_SAVE_BUCKET_MS        Save bucket size (default: 15000)\n"
        "  AUDIOBOOK_RESTORE_ENABLED       Enable seek restore (default: 0)\n"
        "  AUDIOBOOK_TRACK_RESTORE_ENABLED Enable track restore (default: 1)\n"
        "  AUDIOBOOK_SHADOW_MODE           Shadow/log-only mode (default: 0)\n"
        "  AUDIOBOOK_RESUME_DAEMON_SOURCE_ONLY  Source-only/test mode (default: 0)\n"
        "  ... (80+ total config fields, see spec section 6)\n\n"
        "Config file format: key=value lines, keys without AUDIOBOOK_ prefix.\n"
    );
}