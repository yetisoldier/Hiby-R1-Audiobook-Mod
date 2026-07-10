# Resume Daemon C Rewrite — Implementation Specification

> **Audience:** Forge (implementer), Karen (test engineer)
> **Source:** `tools/r1_audiobook_resume_daemon.sh` (2,500 lines POSIX shell)
> **Target:** C daemon, cross-compiled with Zig (`mipsel-linux-musleabi`)
> **Status:** Design-only. No C code in this document.

---

## 1. Module Decomposition

The C daemon is split into 10 modules. Each module maps to one `.c`/`.h` pair.
Modules have clean boundaries; no module reaches into another's private state.

```
src/
├── main.c              — entry point, signal handling, main loop dispatch
├── config.c / config.h — configuration loading, env-var overrides, defaults
├── log.c / log.h       — structured logging with rotation
├── proc_mem.c / proc_mem.h  — process memory reading (proc/pid/mem abstraction)
├── player.c / player.h — hiby_player PID discovery, position/duration reads,
│                         path slot decoding, book-title marker polling
├── catalog.c / catalog.h   — catalog.tsv parsing, album pattern loading,
│                             field lookups by path/root/index
├── resume.c / resume.h — resume record CRUD, JSON serialization, completion
│                         state, save/restore logic, bucket tracking
├── ui.c / ui.h         — framebuffer pixel checks, touch event injection,
│                         key event injection, play-mode enforcement
├── state.c / state.h   — state machine, daemon runtime state, diagnostics
├── helpers.c / helpers.h   — external helper subprocess management
│                             (seek helper, memscan, direct-open)
└── shadow.c / shadow.h — shadow-mode logging (migration scaffold)
```

### Module Responsibilities

| Module | Responsibility | Shell functions absorbed |
|--------|---------------|------------------------|
| `main.c` | Entry point, signal setup, PID file, inherited FD cleanup, main loop skeleton | `main()`, `close_inherited_socket_fds()` |
| `config.c` | Load config file, apply env-var overrides, validate, expose `daemon_config` struct | All 80+ `AUDIOBOOK_*` env reads at top of script |
| `log.c` | Timestamped log lines, rotation by size cap, log file open/close | `rotate_log_if_needed()`, `log()` |
| `proc_mem.c` | `pread` on `/proc/PID/mem`, u32le decoding, byte pattern search, catalog path search in memory | `u32le_from_hex()`, `u32le_at_pid_mem()`, `pid_mem_contains()`, `pid_mem_contains_catalog_album()`, `pid_mem_first_catalog_path()` |
| `player.c` | PID discovery + cache, position/duration reads, path slot hex read + decode + cache, book-title marker seq read, memscan root lookup | `player_pid()`, `player_pid_cached()`, `position_ms_memory()`, `duration_ms_memory()`, `current_path_slot_hex()`, `current_path_slot_preview()`, `current_path_from_hex()`, `decode_path_slot_hex()`, `path_preview_is_audiobook()`, `path_preview_is_music()`, `path_slot_hex_is_audiobook()`, `book_title_marker_seq()`, `book_title_memscan_root()` |
| `catalog.c` | Parse `catalog.tsv`, `catalog-books.tsv`, album patterns; field lookups; refresh album patterns | `refresh_catalog_album_patterns()`, `catalog_field_for_path()`, `catalog_field_for_root_index()`, `catalog_first_path_for_root()`, `book_key_for_path()` |
| `resume.c` | Record path resolution (book-key vs legacy), record read/write (JSON), completion detection, save bucketing, restore target computation, failure tracking, restore retry backoff | `save_position()`, `maybe_restore()`, `maybe_restore_track()`, `record_for_path()`, `existing_record_for_path()`, `json_value()`, `json_number()`, `json_bool()`, `completion_state_for_path_position()`, `restore_target_ms()`, `note_seek_restore_failure()`, `note_track_restore_failure()`, `restore_retry_delay_seconds()`, `should_defer_new_track_save()`, `should_skip_after_completed_restore()`, `should_skip_failed_restore_save()`, `should_attempt_restore_for_position()`, `safe_id()`, `json_escape()` |
| `ui.c` | Framebuffer reads (white-pixel counting, screen classification), input event file injection, generated touch tap synthesis, seek-bar tap restore, play-mode enforcement, back-guard screen checks and firing, touch row/swipe helpers, track next/prev via input events | `ui_seek_screen_ready()`, `fb_white_pixels_region()`, `audiobook_subheader_visible()`, `audiobook_title_list_visible()`, `audiobook_track_list_visible()`, `audiobook_global_back_target_visible()`, `send_input_event()`, `emit_input_*()`, `write_touch_tap_stream()`, `touch_generated_tap()`, `ui_seek_restore()`, `play_mode_value()`, `play_mode_screen_ready()`, `ensure_audiobook_play_mode()`, `track_next()`, `track_prev()`, `touch_first_track()`, `touch_back_to_track_list()`, `touch_track_row()`, `touch_track_swipe_up()`, `book_title_wait_for_launcher_track_list()`, `enable_audiobook_back_guard_window()`, `maybe_audiobook_back_guard()` |
| `state.c` | State machine enum, state transitions, autostart trigger logic, direct-track-select orchestration, visible-track-select, near-miss transport, direct-open trigger, book-title direct-start, diagnostics counters | `book_title_autostart_active_now()`, `book_title_direct_track_select()`, `book_title_visible_track_select()`, `book_title_direct_open_trigger()`, `book_title_direct_open_row_override()`, `book_title_direct_start_saved_track()`, `book_title_verify_selected_track()`, `tap_track_list_index()`, `direct_track_geometry()`, `track_restore_near_miss_transport()`, `maybe_autostart_book_title()`, `should_poll_book_title_marker()`, `book_title_context_active()`, `book_title_log_bucket()`, `log_book_title_restore_wait()`, `log_book_title_pre_restore_skip()`, `clear_book_title_autostart()`, `diag_inc()`, `diag_maybe_log()` |
| `helpers.c` | Run seek helper with timeout, kill on timeout, capture output; manage memscan and direct-open helper invocations | `run_helper()` |
| `shadow.c` | Shadow-mode wrapper: logs "WOULD DO: ..." instead of executing UI actions or writes; controlled by config flag | (new — no shell equivalent) |

---

## 2. Data Structures

### 2.1 Daemon Configuration (`config.h`)

```c
typedef struct {
    /* Paths */
    char base_dir[256];         // /usr/data/audiobooks
    char store_dir[264];        // base_dir + "/resume.d"
    char log_path[256];         // base_dir + "/resume-daemon.log"
    char pid_file[256];         // base_dir + "/resume-daemon.pid"
    char catalog_path[256];     // base_dir + "/catalog.tsv"
    char catalog_albums_path[256];
    char catalog_books_path[256];
    char user_ini_path[64];     // /usr/data/user.ini
    char helper_path[256];      // base_dir + "/bin/r1_audiobook_resume_helper"
    char memscan_helper_path[256];
    char direct_open_helper_path[256];
    char touch_event_node[32];  // /dev/input/event1
    char key_next_event_node[32];
    char key_prev_event_node[32];

    /* Touch event file paths (14+ files) */
    char touch_next_event_file[256];
    char touch_first_track_event_file[256];
    char touch_first_track_down_event_file[256];
    char touch_first_track_move_event_file[256];
    char touch_first_track_up_event_file[256];
    char touch_back_event_file[256];
    char touch_track_row_event_files[5][256];  // rows 1-5
    char touch_track_swipe_down_event_file[256];
    char touch_track_swipe_move_event_files[6][256];  // moves 1-6
    char touch_track_swipe_up_event_file[256];
    char key_next_event_file[256];
    char key_prev_event_file[256];

    /* Timing (milliseconds) */
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
    uint8_t  position_source;   // 0=memory, 1=helper

    /* Logging */
    uint32_t log_max_bytes;

    /* Shadow mode (migration) */
    uint8_t  shadow_mode;       // 0=act, 1=log-only

    /* Source-only (for testing) */
    uint8_t  source_only;       // skip main()
} daemon_config;
```

### 2.2 Resume Record (`resume.h`)

```c
typedef struct {
    int      schema_version;    // always 3
    char     book_id[256];      // safe_id(root)
    char     book_key[128];
    char     root_hiby_path[512];
    char     current_path[512];
    int      media_id;          // -1 = null
    int      track_index;       // -1 = null
    int      track_count;       // -1 = null
    char     chapter_title[256];
    uint32_t position_ms;
    char     updated_at[32];    // ISO 8601 UTC
    bool     completed;
} resume_record;
```

### 2.3 Book Context (`player.h`)

```c
typedef struct {
    char     path[512];         // full hiby path e.g. a:\Audiobooks\Book\03.mp3
    char     root[512];         // book root e.g. a:\Audiobooks\Book
    char     path_preview[128]; // first 128 chars of user.ini slot
    uint32_t position_ms;
    uint32_t duration_ms;
    int      track_index;       // from catalog, -1 if unknown
    int      track_count;       // from catalog, -1 if unknown
    int      media_id;          // from catalog, -1 if unknown
    char     chapter_title[256];
    char     book_key[128];
} book_context;
```

### 2.4 Runtime State (`state.h`)

```c
typedef enum {
    STATE_IDLE,                 // non-audiobook path, idle polling
    STATE_AUDIOBOOK_TRACKING,   // audiobook active, reading position
    STATE_AUTOSTART_TRIGGERED,  // book-title marker changed, autostart window active
    STATE_RESTORE_IN_PROGRESS,  // track or position restore underway
    STATE_RESTORE_FAILED,       // restore failed, in backoff
    STATE_COMPLETED,            // book marked completed, waiting for start-over
} daemon_state;

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
    char     restore_failed_kind[16];   // "seek" or "track"
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
} daemon_runtime;
```

### 2.5 Catalog Entry (`catalog.h`)

```c
typedef struct {
    char     root[512];         // field 1
    int      index;             // field 2 (track index within book)
    int      count;             // field 3 (total tracks in book)
    int      media_id;          // field 4
    char     path[512];         // field 5
    char     title[256];        // field 6
    char     album[256];        // field 7
    char     book_key[128];     // field 9
} catalog_entry;

typedef struct {
    catalog_entry *entries;
    size_t count;
    char **album_patterns;
    size_t album_pattern_count;
} catalog_db;
```

### 2.6 Input Event (`ui.h`)

```c
// Linux input_event is 24 bytes on MIPS (struct timeval + type + code + value)
// On MIPS32 big-endian timeval is 8 bytes, but mipsel is little-endian.
// The shell daemon writes raw binary frames; C will use the kernel struct.
typedef struct {
    struct timeval time;   // 8 bytes LE on mipsel
    uint16_t type;
    uint16_t code;
    int32_t  value;
} input_event_frame;
```

---

## 3. State Machine

The main loop is a state machine with one top-level dispatcher and sub-states for restore operations.

### 3.1 Top-Level State Diagram

```
                    ┌──────────────┐
                    │   STARTUP    │
                    │ (init, pid)  │
                    └──────┬───────┘
                           │
                           ▼
              ┌────────────────────────┐
              │     MAIN LOOP          │
              │  (each iteration)      │
              └────────────────────────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
           ▼               ▼               ▼
    ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
    │  PATH CHECK │ │ MARKER POLL │ │ BACK GUARD  │
    │  (preview)  │ │ (if due)    │ │ (if active) │
    └──────┬──────┘ └──────┬──────┘ └─────────────┘
           │               │
           │      seq changed? ──yes──▶ AUTOSTART_TRIGGERED
           │               │
           │      no change │
           ▼               ▼
    ┌──────────────────────────────┐
    │   PATH CLASSIFICATION         │
    │   audiobook / music / other   │
    └──────────┬───────────────────┘
               │
       ┌───────┼───────┐
       │       │       │
       ▼       ▼       ▼
  AUDIOBOOK  MUSIC   OTHER
       │       │       │
       ▼       ▼       ▼
 ┌──────────┐ idle   idle
 │ READ     │ sleep  sleep
 │ POSITION │ loop_  loop_
 │          │ sleep  sleep
 └────┬─────┘
      │
      ▼
 ┌──────────────────────────────┐
 │  RESTORE CHECK                │
 │  (if restored_path != path    │
 │   and position qualifies)     │
 └──────┬───────────────────────┘
        │
   ┌────┼────────┐
   │    │        │
   ▼    ▼        ▼
 TRACK  SEEK   NO_RESTORE
 RESTORE RESTORE
   │    │
   ▼    ▼
 ┌──────────────────────────────┐
 │  SAVE CHECK                   │
 │  (bucket, defer, skip rules)  │
 └──────┬───────────────────────┘
        │
        ▼
   sleep(loop_sleep)
   loop back
```

### 3.2 Detailed State Transitions

**State: MAIN_LOOP_ITERATION**

Each iteration performs these steps in order:

1. **Read path preview** — `current_path_slot_preview()` from `user.ini` offset 40, 128 bytes
2. **Back guard check** — `maybe_audiobook_back_guard(now, path_preview)` if `back_guard_enabled`
3. **Marker poll decision** — `should_poll_book_title_marker(path_preview, now)`:
   - If audiobook path: always poll
   - If music path: poll if `now - last_poll >= music_poll_seconds`
   - If other path and context active: poll
   - If other path: poll if `now - last_poll >= idle_poll_seconds`
4. **Marker poll** — `book_title_marker_seq()`: reads u32le at `marker_addr`, checks magic=3235793431, reads seq at `marker_addr+40`
   - If seq changed → **AUTOSTART_TRIGGERED** sub-state
5. **Path classification**:
   - Audiobook pattern (`a:\Audiobooks\*`) → **AUDIOBOOK_TRACKING**
   - Music pattern (`a:\Music\*`) → **IDLE** (idle_interval sleep)
   - Other → **IDLE** with back_guard_idle_interval if back_guard active
6. **AUDIOBOOK_TRACKING sub-flow**:
   a. Read full path from user.ini
   b. Set `book_title_context_until = now + context_seconds`
   c. Read position via `position_ms()` (memory or helper)
   d. Determine `autostart_restore_active` (autostart window active?)
   e. If autostart active and reset_key changed: reset restore state
   f. If path changed since last iteration: log, enforce play mode, clear restore state
   g. If record exists and completed flag is true: enter **COMPLETED** handling
   h. If `restored_path != path`:
      - Check `should_attempt_restore_for_position`
      - If autostart active and pos > restore_only_before_ms: log wait, continue
      - If restore_failed and retry not due: skip
      - Try **TRACK_RESTORE** (if saved_path != current_path, same book root)
      - On track restore success: re-read path and position, settle
      - On track restore failure: note failure, continue
      - Try **SEEK_RESTORE** via helper, then UI seek fallback
      - On success: `restored_path = path`, clear failures
      - On failure: `restored_path = ""` (stays un-restored)
   i. **SAVE phase**:
      - If pos >= min_save_ms:
        - Compute bucket = pos / save_bucket_ms
        - Check skip conditions: autostart pre-restore skip, failed restore skip, completed start-over, deferred new-track, bucket unchanged
        - If save warranted: `save_position(path, pos)`, update bucket
7. **Sleep** `loop_sleep` seconds, loop

**State: AUTOSTART_TRIGGERED**

Entered when `book_title_marker_seq` returns a new sequence number.

1. Read `album_ptr` at `marker_addr + 24`
2. Read `track_list_ptr` at `album_ptr + track_list_offset`
3. Determine match_reason:
   - `launcher`: source_magic matches and source_seq matches
   - `path`: memory at track_list_ptr contains `a:\Audiobooks`
   - `catalog`: memory at catalog_scan_ptr contains album patterns
   - `context`: context window active (no path match)
   - `relaxed`: fallback when require_path=0
4. Set context_until, enable back guard window
5. Set `autostart_until = now + restore_retry_after_failure_seconds`
6. Reset restore state, clear failures
7. Sleep `autostart_delay_seconds`
8. If preplay direct-start allowed for reason:
   - Try `book_title_direct_start_saved_track` (memscan root, catalog path lookup, record lookup, direct-open or swipe+tap)
9. If reason=launcher: detect screen state, back to title-list if needed, return
10. Otherwise: `touch_first_track()` to enter track list

**State: COMPLETED**

1. If `restored_path != path`: log "completed book start-over"
2. Set `restored_path = path`, `completed_saved_path = ""`
3. Clear restore failure state, clear autostart
4. Set `completed_start_over_path = path`
5. On next save: write new position, clear completed flag in record

**State: RESTORE_FAILED**

1. Record failure: path, kind (seek/track), timestamp
2. For seek failures: increment count, compute exponential backoff (base * 2^(count-1), capped at max)
3. For track failures: set kind=track, reset seek failure state
4. On subsequent iterations: skip restore attempt until retry delay elapsed
5. Skip saves that would overwrite the saved position (backward save guard)

### 3.3 Track Restore Sub-Flow

```
maybe_restore_track(path, pos)
│
├─ restore_enabled && track_restore_enabled? else return 0
├─ load record, check completed? return 0
├─ saved_path != path? (same book root required) else return 0
├─ pos <= restore_only_before_ms? else return 0
├─ autostart_restore_active?
│   └─ yes: proceed
│   └─ no: check first-track-entry enabled and conditions
│          ├─ current_index==1, saved_index>1, pos<=max_ms → proceed
│          └─ else: skip (manual track selection)
│
├─ direction = next/prev, steps = |saved_index - current_index|
├─ steps <= max_steps? else return 1
│
├─ TRY: book_title_direct_track_select
│   ├─ direct_open_row_override (ptrace helper + tap row 1)
│   ├─ direct_track_geometry (compute swipes + row)
│   ├─ touch_back_to_track_list
│   ├─ tap_track_list_index (swipe + tap)
│   └─ book_title_verify_selected_track (poll path change)
│
│   result:
│   ├─ 0 (success) → return 0
│   ├─ 2 (near-miss, same book but wrong track)
│   │   ├─ TRY: book_title_visible_track_select
│   │   │   ├─ 0 → return 0
│   │   │   └─ 2 → TRY: track_restore_near_miss_transport → return
│   │   └─ return 1
│   └─ 1 (failure) → fall through
│
├─ TRY: book_title_visible_track_select
│   ├─ 0 → return 0
│   └─ 2 → TRY: track_restore_near_miss_transport → return
│
├─ IF key_fallback_enabled:
│   ├─ Re-read current path (may have changed during direct/visible attempts)
│   ├─ Recompute direction/steps
│   └─ Key-based transport: track_next/track_prev loop
│       └─ Poll path change each step until saved_path reached
│
└─ return 1 (failed)
```

---

## 4. Memory Read Interface (`proc_mem.h`)

### 4.1 API

```c
// Open /proc/PID/mem for reading
int proc_mem_open(pid_t pid);

// Read N bytes at address into buf (uses pread, handles EINTR)
int proc_mem_read(int fd, void *buf, size_t len, uint32_t addr);

// Read a little-endian u32 at address
int proc_mem_read_u32le(int fd, uint32_t addr, uint32_t *out);

// Check if memory at addr contains the given byte pattern
bool proc_mem_contains(int fd, uint32_t addr, size_t count,
                       const void *pattern, size_t pattern_len);

// Check if memory at addr contains any line from the album patterns file
bool proc_mem_contains_catalog_album(int fd, uint32_t addr, size_t count,
                                     const catalog_db *cat);

// Find first catalog path that appears in memory at addr
int proc_mem_first_catalog_path(int fd, uint32_t addr, size_t count,
                                const catalog_db *cat,
                                char *out_path, size_t out_len);

// Close the proc mem fd
void proc_mem_close(int fd);
```

### 4.2 Implementation Notes

- Use `pread()` directly (not `dd`+`xxd`+`awk` pipeline). This eliminates 3 subprocess spawns per read.
- `proc_mem_read` loops on partial reads and EINTR, matching the `read_all_pread` pattern from `r1_audiobook_memscan.c`.
- Address validation: reject addresses < 4096 (matches shell `[ "$addr" -gt 4096 ] || return 1`).
- The fd is cached in `player.c` and reused across reads. It is invalidated if the PID changes.
- For `proc_mem_contains_catalog_album`: load all album patterns into memory once (catalog refresh), then scan the memory buffer for each. Shell pipes through `grep -F -f`; C does `memmem()` for each pattern.
- For `proc_mem_first_catalog_path`: read the memory range into a heap buffer, then iterate catalog entries checking `memmem()`.

### 4.3 memscan Helper Integration

The `book_title_memscan_root()` function calls the external `r1_audiobook_memscan` binary. In C, this remains a subprocess call (the helper is a separate compiled binary that does its own memory scanning). The `helpers.c` module manages this subprocess.

```c
// helpers.h
int helpers_memscan_root(pid_t pid, const daemon_config *cfg,
                         const char *catalog_books_path,
                         char *out_root, size_t out_len);
int helpers_seek(uint32_t seconds, uint16_t verify_delay_ms,
                 uint8_t verify_tolerance, const daemon_config *cfg);
int helpers_direct_open(pid_t pid, uint32_t row_index,
                        uint32_t probe_addr, uint32_t scratch_addr,
                        uint32_t timeout_ms, const daemon_config *cfg);
```

Each helper call:
1. Fork + exec the helper binary
2. Wait with timeout (using `waitpid` + `alarm` or `SIGCHLD` handler)
3. Kill (`SIGKILL`) on timeout
4. Capture stdout/stderr to log
5. Return exit code

---

## 5. UI Automation Interface (`ui.h`)

### 5.1 Framebuffer

```c
// Read a horizontal row of pixels from /dev/fb0
int fb_read_row(int fb_fd, uint16_t y, uint16_t stride,
                void *row_buf, size_t row_len);

// Count "white-like" and "blue-like" pixels in a row segment
// (matches the shell's RGB555 pixel classification)
int fb_count_white_pixels_row(int fb_fd, uint16_t x0, uint16_t x1,
                              uint16_t y, uint16_t stride);
int fb_count_white_pixels_region(int fb_fd, uint16_t x0, uint16_t y0,
                                 uint16_t x1, uint16_t y1,
                                 uint16_t stride);

// Screen classification (uses pixel counting in specific regions)
bool fb_audiobook_subheader_visible(int fb_fd, const daemon_config *cfg);
bool fb_audiobook_title_list_visible(int fb_fd, const daemon_config *cfg);
bool fb_audiobook_track_list_visible(int fb_fd, const daemon_config *cfg);
bool fb_audiobook_global_back_target_visible(int fb_fd, const daemon_config *cfg);
bool fb_ui_seek_screen_ready(int fb_fd, const daemon_config *cfg);
```

### 5.2 Pixel Classification

The R1 framebuffer is RGB555 (16 bits per pixel, 4 bytes per pixel with padding in 32-bit stride). The shell decodes via `xxd` + `awk` hex-to-decoded-RGB. The C code reads raw `uint16_t` pixels and decodes:

```c
// RGB555 decode (little-endian: low byte first)
// v = pixel[0] | (pixel[1] << 8)
// r = v >> 10 & 0x1f
// g = v >> 5  & 0x3f  (note: shell uses /32 % 64, which is v>>5 & 0x3f)
// b = v & 0x1f
//
// "white": r >= 24 && g >= 48 && b >= 24
// "blue":  r <= 10 && g >= 24 && b >= 18
```

Wait — the shell's formula is:
- `v = h2d(substr($0, i+2, 2) substr($0, i, 2))` — this reads bytes in LE order (low byte first), producing a 16-bit value
- `r = int(v / 2048)` = `v >> 11` — this is actually **R in bits 11-15** (5 bits)
- `g = int(v / 32) % 64` = `(v >> 5) & 0x3f` — **G in bits 5-10** (6 bits)
- `b = v % 32` = `v & 0x1f` — **B in bits 0-4** (5 bits)

This is **RGB565** (5 red, 6 green, 5 blue), not RGB555. The C code must use the same formula.

### 5.3 Touch Event Injection

```c
// Send a pre-recorded event file to an input device node
int ui_send_event_file(const char *label, const char *event_file,
                       const char *event_node);

// Synthesize a touch tap at (x, y) and write to a file
int ui_write_touch_tap(uint16_t x, uint16_t y, uint8_t frames,
                       const char *output_file);

// Synthesize and send a touch tap in one step
int ui_touch_tap(const char *label, uint16_t x, uint16_t y,
                 uint8_t frames, const daemon_config *cfg);

// High-level navigation helpers
int ui_track_next(const daemon_config *cfg);
int ui_track_prev(const daemon_config *cfg);
int ui_touch_first_track(const daemon_config *cfg);
int ui_touch_back_to_track_list(const daemon_config *cfg);
int ui_touch_track_row(int row, const daemon_config *cfg);
int ui_touch_track_swipe_up(const daemon_config *cfg);

// Seek-bar tap restore
int ui_seek_restore(const char *path, uint32_t saved_pos,
                    const daemon_config *cfg, const book_context *book);

// Play mode enforcement
int ui_ensure_play_mode(const daemon_config *cfg);

// Back guard
void ui_enable_back_guard_window(time_t now, daemon_runtime *rt,
                                  const daemon_config *cfg);
void ui_maybe_back_guard(time_t now, const char *path_preview,
                         daemon_runtime *rt, const daemon_config *cfg);
```

### 5.4 Event File vs Synthesized Events

The shell daemon has two modes for touch events:
1. **Pre-recorded binary files** — `cat event_file > /dev/input/eventN` (fast, no synthesis)
2. **Synthesized at runtime** — `write_touch_tap_stream()` generates `input_event` binary frames

In C, both paths write to `/dev/input/eventN`:
- Pre-recorded: `open()` + `read()` from file + `write()` to device node
- Synthesized: construct `input_event` structs in memory, `write()` to device node

The synthesized touch frame format (matching shell `emit_input_event`):
```
struct input_event {        // 24 bytes on MIPS LE
    struct timeval time;    // 8 bytes (two u32 LE: tv_sec, tv_usec)
    uint16_t type;          // 2 bytes LE
    uint16_t code;          // 2 bytes LE
    int32_t  value;         // 4 bytes LE
};
```

For absolute touch coordinates (type=3/EV_ABS):
- code 57 (ABS_MT_TRACKING_ID) = 0
- code 58 (ABS_MT_PRESSURE) = 63
- code 48 (ABS_MT_TOUCH_MAJOR) = 9
- code 53 (ABS_MT_POSITION_X) = x
- code 54 (ABS_MT_POSITION_Y) = y
- type=0 (EV_SYN), code=2 (SYN_REPORT), value=0
- If press: type=1 (EV_KEY), code=330 (BTN_TOUCH), value=1
- type=0 (EV_SYN), code=0 (SYN_REPORT), value=0

Touch release: type=1, code=330, value=0, then two SYN_REPORTs.

---

## 6. Config Management

### 6.1 Three-Tier Configuration

**Tier 1: Compiled defaults** — hardcoded in `config.c` as a `daemon_config` initialized with all default values matching the shell script's `:-` defaults.

**Tier 2: Config file** — `/usr/data/audiobooks/resume-daemon.conf` (new, optional). Format: `key=value` lines, one per line. Keys match the env var names without the `AUDIOBOOK_` prefix (e.g., `INTERVAL_SECONDS=5`).

**Tier 3: Environment variables** — same names as the shell script (`AUDIOBOOK_INTERVAL_SECONDS`, etc.). Highest priority, overrides config file and defaults.

### 6.2 Loading Order

```
1. Initialize config with compiled defaults
2. If config file exists: parse it, override matching fields
3. For each env var: if set, override matching field
4. Validate all numeric fields (clamp to safe ranges, reject non-numeric)
```

### 6.3 Config File Format

```ini
# Resume daemon configuration
# Keys correspond to AUDIOBOOK_* env vars without the prefix.
# Values are parsed as integers, booleans (1/0), or strings.

INTERVAL_SECONDS=5
IDLE_INTERVAL_SECONDS=3
MIN_SAVE_MS=3000
SAVE_BUCKET_MS=15000
RESTORE_ENABLED=0
TRACK_RESTORE_ENABLED=1
# ... etc
```

### 6.4 Env Var → Config Field Mapping

The mapping is 1:1 and generated by a lookup table in `config.c`:

```c
typedef struct {
    const char *env_name;      // "AUDIOBOOK_INTERVAL_SECONDS"
    const char *conf_key;      // "INTERVAL_SECONDS"
    config_type type;          // CONFIG_U32, CONFIG_U8, CONFIG_U16, CONFIG_BOOL, CONFIG_STR
    size_t offset;             // offsetof(daemon_config, interval_seconds)
    uint32_t min_val;          // validation: clamp/ reject
    uint32_t max_val;
} config_field;
```

A single `config_load()` function iterates the table, checks env vars, and populates the struct.

### 6.5 Total Configurable Values

80+ values, matching the shell script exactly. The full list is enumerated in the `config_field` table. Categories:

| Category | Count | Examples |
|----------|-------|---------|
| Paths | 22 | base_dir, store_dir, helper paths, event files, device nodes |
| Timing | 15 | interval, idle_interval, min_save_ms, save_bucket_ms, delays |
| Memory addresses | 8 | position_addr, duration_addr, marker_addr, memscan_addr |
| Autostart | 14 | enabled flags, delays, offsets, scan bytes, context seconds |
| Track restore | 9 | enabled flags, max_steps, settle, poll, fallbacks |
| UI seek | 9 | enabled, bar coords, stride, tolerance, touch frames |
| Play mode | 7 | enabled, target, offset, taps, coords, settle |
| Back guard | 10 | enabled, window, intervals, thresholds, pixel counts |

---

## 7. Logging (`log.h`)

### 7.1 API

```c
void log_init(const char *path, uint32_t max_bytes);
void log_close(void);
void log_msg(const char *fmt, ...);  // timestamped, appended to file
```

### 7.2 Format

Each log line:
```
YYYY-MM-DDTHH:MM:SS+ZZZZ message
```

Matching the shell's `date '+%Y-%m-%dT%H:%M:%S%z'` format exactly.

### 7.3 Rotation

Before each write:
1. Check if log file size >= `log_max_bytes`
2. If so: rename `log` → `log.1` (overwriting previous `log.1`)
3. Write a rotation marker line: `TIMESTAMP rotated log previous_size=N max=M previous=log.1`
4. Then write the actual log message

This matches the shell's `rotate_log_if_needed()` behavior exactly.

### 7.4 Implementation

- Log file is opened in append mode (`O_WRONLY | O_APPEND | O_CREAT`)
- File descriptor is kept open for the daemon's lifetime
- `fstat()` before each write to check size (avoids `wc -c` subprocess)
- `vsnprintf()` + `write()` for formatted output (no `printf` to a file)
- Timestamp generated via `localtime_r()` + `strftime()`

---

## 8. Signal Handling

### 8.1 Signals Handled

| Signal | Action |
|--------|--------|
| `SIGTERM` | Clean shutdown: close log, remove PID file, exit 0 |
| `SIGINT` | Same as SIGTERM |
| `SIGCHLD` | Reap zombie children (helper subprocesses); set `child_exit` flag |
| `SIGALRM` | Helper timeout: kill child if still running |

### 8.2 Implementation

```c
static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t child_exited = 0;

void sig_handler(int signo) {
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
```

### 8.3 Shutdown Sequence

```
1. shutdown_requested flag set by signal handler
2. Main loop checks flag at top of each iteration
3. If set:
   a. Log "shutdown signal received"
   b. Remove PID file
   c. Close log file
   d. Close proc_mem fd if open
   e. exit(0)
```

### 8.4 SIGCHLD Handling

- Install handler with `SA_RESTART` to avoid interrupting `pread`/`write`
- After each main loop iteration, if `child_exited` flag is set: call `waitpid(-1, NULL, WNOHANG)` in a loop to reap all zombies
- Helper timeout uses `alarm()` + `waitpid()` with a fallback `kill(SIGKILL)` if `waitpid` returns 0 after alarm

### 8.5 Self-Pipe Alternative

For maximum portability, a self-pipe can be used instead of `sig_atomic_t` flags:

```c
int signal_pipe[2];  // write end in handler, read end in main loop
```

The main loop uses `select()` or `poll()` with the read end as one of the watched fds. This is the recommended approach for the C version since it integrates cleanly with helper timeout handling.

---

## 9. Build Target

### 9.1 Zig Cross-Compile

```bash
zig cc \
    -target mipsel-linux-musleabi \
    -Os \                          # optimize for size
    -static \
    -fno-stack-protector \         # not available/needed on bare MIPS
    -ffunction-sections \
    -fdata-sections \
    -Wl,--gc-sections \            # strip unused functions/data
    -Wl,--strip-all \              # strip debug + symbols
    -o r1_audiobook_resume_daemon \
    src/*.c
```

### 9.2 Zig Build Script (`build.zig`)

```zig
// build.zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetOptions(.{
        .cpu_arch = .mipsel,
        .os_tag = .linux,
        .abi = .musleabi,
    });

    const exe = b.addExecutable(.{
        .name = "r1_audiobook_resume_daemon",
        .root_source_file = null,  // C-based, not Zig
        .target = target,
        .optimize = .ReleaseSmall,
        .link_libc = true,
    });

    // Add C source files
    exe.addCSourceFiles(.{
        .files = &.{
            "src/main.c", "src/config.c", "src/log.c", "src/proc_mem.c",
            "src/player.c", "src/catalog.c", "src/resume.c", "src/ui.c",
            "src/state.c", "src/helpers.c", "src/shadow.c",
        },
        .flags = &.{ "-Os", "-static", "-ffunction-sections", "-fdata-sections" },
    });

    exe.linkOptions = .{
        .strip = .all,
    };
    exe.linker_gc_sections = true;

    b.installArtifact(exe);
}
```

### 9.3 Static Linking

All dependencies are musl libc functions only:
- `pread`, `read`, `write`, `open`, `close`, `lseek`
- `fork`, `execv`, `waitpid`, `kill`, `alarm`
- `signal` / `sigaction`
- `time`, `gettimeofday`, `localtime_r`, `strftime`
- `malloc`, `free`, `realloc` (or use static buffers)
- `memcmp`, `memcpy`, `strlen`, `strncpy`, `strncmp`
- `memmem` (GNU extension — provide a fallback implementation)
- `snprintf`, `vsnprintf`

No external libraries. No sqlite (the daemon reads TSV files, not the DB). No zlib. No pthreads.

### 9.4 Binary Size Budget

| Target | Limit |
|--------|-------|
| Stripped binary | < 100 KB |
| RAM footprint (data + bss) | < 64 KB |
| Stack usage | < 8 KB |

Size optimization strategies:
- `-Os` optimization level
- `-ffunction-sections -fdata-sections` + `--gc-sections` linker pass
- `--strip-all` to remove symbols
- Avoid floating point (use integer math throughout — the shell already uses only integer arithmetic)
- Use static buffers instead of dynamic allocation where possible
- Avoid `printf` family for log output where `write` + manual formatting suffices
- Link against musl (smaller than glibc)

### 9.5 RAM Budget

The X1600 has ~128 MB total RAM. `hiby_player` uses most of it. The daemon must be lightweight:

| Component | Estimated RAM |
|-----------|--------------|
| Code segment (shared, not counted) | — |
| Data segment (config struct) | ~4 KB |
| BSS (runtime state, catalog) | ~8 KB |
| Stack | 8 KB |
| Heap (catalog entries, temp buffers) | ~16 KB max |
| File descriptors | ~6 (log, fb0, proc_mem, 2× input nodes) |
| **Total** | **~36 KB** |

Catalog loading: the `catalog.tsv` file can be large. Load entries into a fixed-size array (e.g., 4096 entries max, ~72 bytes each = 288 KB). If memory is tight, switch to on-demand field lookups via `lseek`+`read` on the TSV file (slower but no heap). Recommended: load eagerly at startup, free the album patterns after initial use, keep catalog entries in memory.

---

## 10. Testing Strategy

### 10.1 Host-Side Unit Tests (x86_64)

Each module has unit tests that compile and run on the development host using a mock framework.

**`proc_mem.c` tests:**
- Mock `/proc/PID/mem` using a temporary file with known contents
- Test `proc_mem_read_u32le` with known addresses
- Test `proc_mem_contains` with pattern matching
- Test edge cases: address < 4096, partial reads, EINTR retry

**`catalog.c` tests:**
- Parse a sample TSV file, verify field lookups
- Test `catalog_field_for_path` with exact and missing paths
- Test `catalog_field_for_root_index` with valid and out-of-range indices
- Test album pattern refresh

**`resume.c` tests:**
- Write and read a resume record JSON, verify round-trip
- Test completion detection (last track, remaining <= threshold)
- Test restore target computation (rewind, no rewind, edge cases)
- Test save bucketing logic (bucket changes, same bucket)
- Test failure backoff calculation (exponential, capped)
- Test `should_defer_new_track_save`, `should_skip_after_completed_restore`, `should_skip_failed_restore_save`

**`ui.c` tests:**
- Mock framebuffer using a temporary file with known pixel data
- Test pixel classification (white, blue, neither) at known coordinates
- Test `fb_count_white_pixels_region` with various regions
- Test screen classification functions with mock framebuffer states
- Test touch event synthesis (verify binary output matches expected format)
- Test play mode value reading from mock user.ini

**`config.c` tests:**
- Load defaults, verify all 80+ fields have correct default values
- Load config file, verify overrides
- Load env vars, verify they override config file
- Test validation: non-numeric values, out-of-range values

**`state.c` tests:**
- State machine transition tests with mock book_context and daemon_runtime
- Test autostart trigger detection (seq change, magic check)
- Test match_reason classification (launcher, path, catalog, context, relaxed)
- Test direct_track_geometry calculations
- Test near-miss transport logic

### 10.2 Integration Tests (QEMU User-Mode)

Use QEMU user-mode emulation for MIPS LE to run the compiled binary:

```bash
qemu-mipsel -L /path/to/musl-sysroot ./r1_audiobook_resume_daemon --test-mode
```

**Test harness:**
- Provide mock `/proc/` filesystem via `PROC_PREFIX` env var
- Mock `user.ini` with known path slots
- Mock framebuffer via `FB_PATH` env var pointing to a test file
- Mock input device nodes as regular files
- Run daemon for N iterations with scripted state changes
- Verify log output and resume record files match expected

### 10.3 Shadow Mode Testing

The daemon has a `--shadow` flag (or `SHADOW_MODE=1` env var) that:
- Runs all read-side operations (memory reads, path detection, position reads)
- Logs all decisions it *would* make ("WOULD SAVE: path=X pos=Y", "WOULD RESTORE: path=X target_ms=Y")
- Does NOT write resume records, does NOT inject touch events, does NOT call helpers

This allows running the C daemon alongside the shell daemon on a real device and comparing logs.

### 10.4 Device Integration Tests

On the actual R1 device:
1. **Shadow mode comparison** — run both daemons, diff their logs
2. **Controlled cutover** — stop shell daemon, start C daemon, manually test each feature
3. **Automated smoke test** — scripted audiobook scenarios (play, pause, switch books, resume, complete)

### 10.5 Test Case Derivation for Karen

Karen can derive test cases from these spec sections:

| Spec Section | Test Cases |
|-------------|-----------|
| §3.2 State transitions | One test per arrow in the state diagram |
| §3.3 Track restore sub-flow | One test per branch (direct success, near-miss, visible fallback, key fallback, all-fail) |
| §2.2 Resume record | Round-trip write/read for each field; completion flag transitions |
| §6 Config management | Default values, env overrides, config file overrides, validation errors |
| §8 Signal handling | SIGTERM during save, SIGCHLD from helper, timeout kill |
| §11 Migration | Shadow mode log comparison for each daemon behavior |

---

## 11. Migration Plan

### 11.1 Phase 1: Shadow Mode (Weeks 1-3)

**Goal:** Validate C daemon reads and decisions match shell daemon, without acting.

1. Build C daemon with `SHADOW_MODE=1`
2. Deploy to R1 alongside shell daemon (shell daemon remains active, acts as before)
3. C daemon runs in parallel, reads the same state, logs what it *would* do
4. Collect both log files
5. **Comparison tool:** `tools/compare_daemon_logs.py` — parses both logs, matches entries by timestamp, flags discrepancies in:
   - Path detection
   - Position reads
   - Autostart trigger detection
   - Restore decisions
   - Save decisions
   - Completion detection
6. Fix discrepancies until logs match for 7 consecutive days of normal use

**Safety:** Shell daemon is untouched. C daemon cannot affect playback. If C daemon crashes, no user impact.

### 11.2 Phase 2: Controlled Cutover (Week 4)

**Goal:** C daemon takes over, shell daemon stands by as fallback.

1. Build C daemon with `SHADOW_MODE=0`
2. Stop shell daemon (`kill $(cat resume-daemon.pid)`)
3. Start C daemon
4. Manual test matrix:
   - [ ] Play audiobook, pause, resume — position restores correctly
   - [ ] Switch between two audiobooks — each resumes at its own position
   - [ ] Finish a book — completion flag set, next start begins from beginning
   - [ ] Play music — daemon goes idle, no saves
   - [ ] Quick book switch — bookmark corruption guard works
   - [ ] Track restore — switching to a multi-track book restores correct track
   - [ ] Play mode enforcement — sequential mode set on audiobook entry
   - [ ] Back guard — extra-back after audiobook list exit
   - [ ] Log rotation — log file rotates at size cap
5. If any test fails: stop C daemon, restart shell daemon, file bug

### 11.3 Phase 3: Shell Daemon Retirement (Week 5+)

**Goal:** Remove shell daemon from boot sequence.

1. Update `/etc/init.d/S91audiobook_resume.sh` to start C daemon instead of shell daemon
2. Keep shell daemon binary in place for one release cycle as fallback
3. After one release cycle with no issues: remove shell daemon binary
4. Update documentation

### 11.4 Rollback Plan

At any point during Phase 2 or 3:
1. Stop C daemon: `kill $(cat resume-daemon.pid)`
2. Start shell daemon: `/usr/bin/r1_audiobook_resume_daemon.sh &`
3. Resume records are compatible (both use same JSON format and same paths)

### 11.5 Shadow Mode Implementation

In `shadow.c`:

```c
// Returns true if in shadow mode (log-only, no side effects)
bool shadow_is_active(const daemon_config *cfg);

// Wraps save_position: logs "WOULD SAVE" instead of writing
int shadow_save_position(const char *path, uint32_t pos, ...);

// Wraps touch event injection: logs "WOULD TAP x=Y y=Z" instead of writing to device
int shadow_touch_tap(const char *label, uint16_t x, uint16_t y, ...);

// Wraps helper calls: logs "WOULD CALL helper X with args Y" instead of forking
int shadow_helper_call(const char *helper_name, const char *args, ...);

// Wraps seek restore: logs "WOULD SEEK to seconds=N" instead of calling helper
int shadow_seek_restore(uint32_t target_seconds, ...);
```

Every side-effect-producing function checks `shadow_is_active()` first. In shadow mode, it logs the intent and returns success without acting. Read-only functions (memory reads, path detection, catalog lookups, position reads) execute normally.

---

## 12. JSON Resume Record Format

The C daemon must produce byte-identical JSON to the shell daemon.

### 12.1 Schema (Version 3)

```json
{
  "schema_version": 3,
  "book_id": "a__Audiobooks_Book_Title",
  "book_key": "optional-key",
  "root_hiby_path": "a:\\Audiobooks\\Book Title",
  "current_path": "a:\\Audiobooks\\Book Title\\03 - Chapter.mp3",
  "media_id": 12345,
  "track_index": 3,
  "track_count": 15,
  "chapter_title": "Chapter 3 - Title",
  "position_ms": 1234567,
  "updated_at": "2026-07-10T12:34:56Z",
  "completed": false
}
```

### 12.2 Parsing

The C parser must handle:
- Quoted string values with escaped backslashes (`\\`) and quotes (`\"`)
- Unquoted numeric values (integers)
- Unquoted boolean values (`true` / `false`)
- Null values for missing catalog fields (`media_id: null`)
- Whitespace around colons (shell uses `[ \t]*:[ \t]*` regex)

### 12.3 Writing

The C writer must:
- Use 2-space indentation (matching shell `echo` with 2 spaces)
- Escape backslashes and double quotes in string values
- Format `updated_at` as UTC ISO 8601: `YYYY-MM-DDTHH:MM:SSZ`
- Write `null` for missing integer fields (media_id, track_index, track_count when catalog lookup fails)
- Write `true` or `false` for completed field
- Write atomically: write to temp file, then `rename()` over the record (matching shell `tmp + mv`)

---

## 13. Path Slot Decoding

### 13.1 User.ini Path Slot

The current audiobook path is stored at offset 40 in `/usr/data/user.ini` as UTF-16LE, up to 512 bytes (256 characters).

### 13.2 Hex Encoding

The shell reads 512 bytes, hex-encodes them, then decodes the hex back to ASCII by processing 4 hex chars at a time (2 bytes = 1 UTF-16LE character):
- `lo,hi` hex pair
- If both are `00`: skip if output empty, stop if output non-empty
- If `hi == 00`: output `lo` as ASCII character
- If `hi != 00`: output `?` (non-ASCII character)

### 13.3 C Implementation

In C, this is a direct read + decode loop:

```c
// Read 512 bytes at offset 40 from user.ini
// Decode as UTF-16LE: for each uint16_t, if 0x0000 stop (if output non-empty),
// if high byte is 0 output low byte, else output '?'
// Fix-up: if path starts with ":\\" prepend "a", if starts with "\\Audiobooks\\" prepend "a:"
```

### 13.4 Path Classification

- **Audiobook**: matches `a:\Audiobooks\*` (case-insensitive on drive letter), or `:\Audiobooks\*`, or `\Audiobooks\*`
- **Music**: matches `a:\Music\*` (case-insensitive), or `:\Music\*`, or `\Music\*`
- **Other**: anything else

The hex-level check (`path_slot_hex_is_audiobook`) checks for the UTF-16LE hex encoding of `:\Audiobooks\` with optional drive letter prefix. In C, this check is done on the decoded string, not the hex — simpler and equivalent.

---

## 14. Catalog TSV Format

### 14.1 `catalog.tsv` (Tab-Separated)

| Field # | Name | Description |
|---------|------|-------------|
| 1 | root | Book root path (e.g., `a:\Audiobooks\Book Title`) |
| 2 | track_index | 1-based index within book |
| 3 | track_count | Total tracks in book |
| 4 | media_id | SQLite media DB row ID |
| 5 | path | Full file path |
| 6 | title | Track/chapter title |
| 7 | album | Album name (book title) |
| 8 | (unused) | |
| 9 | book_key | Stable book key for record lookup |

First line is a header (skipped by `tail -n +2` in shell).

### 14.2 `catalog-books.tsv`

Used by the memscan helper. Contains book root paths, one per line, for memory scanning.

### 14.3 `catalog-albums.txt`

Extracted album names (field 7) from catalog.tsv, one per line, used for catalog-based memory matching.

### 14.4 C Loading

At startup:
1. Open `catalog.tsv`, skip header line
2. Read each line, split on `\t`, populate `catalog_entry` array
3. Extract unique album names into `album_patterns` array
4. Store sorted by path for fast lookup (binary search) or hash table

---

## 15. Edge Cases (Must Handle)

All edge cases from the shell daemon must be handled identically:

1. **hiby_player not running** — `player_pid()` returns empty; daemon continues idle polling, does not crash
2. **Position read fails** — `pread` returns error or short read; daemon logs, backs off, continues
3. **Helper timeout** — seek helper killed after `helper_timeout_seconds`; counted toward `helper_max_consecutive_failures`; daemon exits after threshold
4. **User.ini not readable** — path preview returns empty; daemon treats as non-audiobook
5. **Framebuffer not readable** — screen guard functions return false; UI seek and play mode checks fail gracefully
6. **Input event files missing** — logged as unavailable, function returns error
7. **Input device nodes missing** — logged as unavailable, function returns error
8. **Catalog file missing** — catalog lookups return empty; record resolution falls back to legacy root-based lookup
9. **Resume record directory missing** — created at startup (`mkdir -p`)
10. **Log directory missing** — created at startup
11. **PID file stale** — overwritten at startup (no lock file check in shell; C matches)
12. **Quick book switch** — `should_defer_new_track_save` prevents overwriting a deeper bookmark when user briefly lands on a different track
13. **Completed book restart** — completed flag cleared on first save after completion, allowing fresh progress tracking
14. **Failed restore save guard** — `should_skip_failed_restore_save` prevents saving a position that would overwrite a deeper saved position after a failed restore
15. **Back guard after audiobook exit** — detects audiobook subheader still visible, fires extra back taps to escape; detects global back target, fires back taps
16. **Exponential retry backoff** — seek restore failures use exponential backoff: base * 2^(count-1), capped at max
17. **Track switch settle** — after track next/prev, polls path change at `track_switch_poll_us` interval for up to `track_switch_settle_seconds`
18. **Direct-open helper failure** — falls back to swipe+tap track selection
19. **Direct-track-select near-miss** — if wrong track selected, computes delta and retries with adjusted row; if still wrong, falls back to near-miss transport (key-based next/prev stepping)
20. **Autostart relaxed mode** — when `require_path=0`, autostart fires even without memory path evidence, using context window as trigger

---

## 16. Diagnostics

### 16.1 Periodic Stats Logging

Every `diagnostics_interval_seconds` (default 60s), log:

```
stats loops=N audiobook=N non_audiobook=N path_preview=N marker_polls=N marker_skips=N position_reads=N saves=N
```

Counters reset after each log.

### 16.2 Startup Configuration Log

At startup, log all non-default configuration values in a single line, matching the shell's startup log format:

```
start interval=5s idle_interval=3s marker_idle_poll=5s marker_music_poll=15s diagnostics_interval=60s min_save_ms=3000 save_bucket_ms=15000 ...
```

---

## 17. File Layout Summary

### 17.1 Source Files

```
src/
├── main.c
├── config.c      config.h
├── log.c         log.h
├── proc_mem.c    proc_mem.h
├── player.c      player.h
├── catalog.c     catalog.h
├── resume.c      resume.h
├── ui.c          ui.h
├── state.c       state.h
├── helpers.c     helpers.h
└── shadow.c      shadow.h
```

### 17.2 Test Files

```
tests/
├── test_config.c
├── test_proc_mem.c
├── test_catalog.c
├── test_resume.c
├── test_ui.c
├── test_state.c
├── test_log.c
├── test_helpers.c
├── test_shadow.c
├── test_integration.c     // QEMU-assisted integration tests
├── fixtures/
│   ├── catalog.tsv
│   ├── catalog-books.tsv
│   ├── user.ini
│   ├── resume.d/
│   └── fb_test_*.bin      // mock framebuffer snapshots
└── compare_daemon_logs.py  // shadow mode log comparison tool
```

### 17.3 Build Files

```
build.zig
build.zig.zon         (if needed for Zig package management)
Makefile              // convenience wrapper for zig build
```

---

## 18. Appendix: Shell Function → C Module Mapping

| Shell Function | C Module | C Function (proposed) |
|---------------|----------|----------------------|
| `rotate_log_if_needed()` | log.c | `log_rotate_if_needed()` |
| `log()` | log.c | `log_msg()` |
| `close_inherited_socket_fds()` | main.c | `close_inherited_socket_fds()` |
| `run_helper()` | helpers.c | `helpers_run()` |
| `player_pid()` | player.c | `player_get_pid()` |
| `player_pid_cached()` | player.c | `player_get_pid_cached()` |
| `u32le_from_hex()` | proc_mem.c | `proc_mem_u32le()` |
| `position_ms_memory()` | player.c | `player_read_position()` |
| `duration_ms_memory()` | player.c | `player_read_duration()` |
| `u32le_at_pid_mem()` | proc_mem.c | `proc_mem_read_u32le()` |
| `pid_mem_contains()` | proc_mem.c | `proc_mem_contains()` |
| `refresh_catalog_album_patterns()` | catalog.c | `catalog_refresh_albums()` |
| `pid_mem_contains_catalog_album()` | proc_mem.c | `proc_mem_contains_album()` |
| `pid_mem_first_catalog_path()` | proc_mem.c | `proc_mem_first_catalog_path()` |
| `current_path_slot_hex()` | player.c | `player_read_path_slot_hex()` |
| `current_path_slot_preview()` | player.c | `player_read_path_preview()` |
| `decode_path_slot_hex()` | player.c | `player_decode_path_slot()` |
| `current_path()` | player.c | `player_get_current_path()` |
| `position_ms()` | player.c | `player_get_position()` |
| `book_root_for_path()` | resume.c | `resume_book_root()` |
| `safe_id()` | resume.c | `resume_safe_id()` |
| `json_escape()` | resume.c | `resume_json_escape()` |
| `json_value()` / `json_number()` / `json_bool()` | resume.c | `resume_json_get_str()` / `_get_int()` / `_get_bool()` |
| `catalog_field_for_path()` | catalog.c | `catalog_lookup_by_path()` |
| `catalog_field_for_root_index()` | catalog.c | `catalog_lookup_by_root_index()` |
| `book_title_memscan_root()` | player.c | `player_memscan_root()` |
| `book_key_for_path()` | catalog.c | `catalog_book_key_for_path()` |
| `record_for_path()` / `existing_record_for_path()` | resume.c | `resume_record_path()` / `resume_existing_record()` |
| `same_book_root()` | resume.c | `resume_same_book_root()` |
| `completion_state_for_path_position()` | resume.c | `resume_check_completion()` |
| `save_position()` | resume.c | `resume_save_position()` |
| `maybe_restore()` | resume.c | `resume_maybe_restore()` |
| `maybe_restore_track()` | resume.c | `resume_maybe_restore_track()` |
| `track_restore_near_miss_transport()` | state.c | `state_near_miss_transport()` |
| `send_input_event()` | ui.c | `ui_send_event_file()` |
| `emit_input_*()` | ui.c | `ui_emit_event()` |
| `emit_touch_abs_frame()` | ui.c | `ui_emit_touch_frame()` |
| `write_touch_tap_stream()` | ui.c | `ui_write_touch_tap()` |
| `touch_generated_tap()` | ui.c | `ui_touch_tap()` |
| `play_mode_value()` | ui.c | `ui_play_mode_read()` |
| `ensure_audiobook_play_mode()` | ui.c | `ui_ensure_play_mode()` |
| `ui_seek_screen_ready()` | ui.c | `ui_seek_screen_ready()` |
| `fb_white_pixels_region()` | ui.c | `fb_count_white_region()` |
| `audiobook_subheader_visible()` | ui.c | `ui_subheader_visible()` |
| `audiobook_title_list_visible()` | ui.c | `ui_title_list_visible()` |
| `audiobook_track_list_visible()` | ui.c | `ui_track_list_visible()` |
| `audiobook_global_back_target_visible()` | ui.c | `ui_back_target_visible()` |
| `book_title_wait_for_launcher_track_list()` | ui.c | `ui_wait_launcher_tracklist()` |
| `enable_audiobook_back_guard_window()` | ui.c | `ui_enable_back_guard()` |
| `maybe_audiobook_back_guard()` | ui.c | `ui_maybe_back_guard()` |
| `track_next()` / `track_prev()` | ui.c | `ui_track_next()` / `ui_track_prev()` |
| `touch_first_track()` | ui.c | `ui_touch_first_track()` |
| `touch_back_to_track_list()` | ui.c | `ui_touch_back()` |
| `touch_track_row()` | ui.c | `ui_touch_row()` |
| `touch_track_swipe_up()` | ui.c | `ui_swipe_up()` |
| `book_title_autostart_active_now()` | state.c | `state_autostart_active()` |
| `direct_track_geometry()` | state.c | `state_direct_geometry()` |
| `tap_track_list_index()` | state.c | `state_tap_track_index()` |
| `book_title_verify_selected_track()` | state.c | `state_verify_selected_track()` |
| `book_title_direct_open_trigger()` | state.c | `state_direct_open_trigger()` |
| `book_title_direct_open_row_override()` | state.c | `state_direct_open_override()` |
| `book_title_direct_track_select()` | state.c | `state_direct_track_select()` |
| `book_title_visible_track_select()` | state.c | `state_visible_track_select()` |
| `book_title_direct_start_saved_track()` | state.c | `state_direct_start_saved()` |
| `book_title_marker_seq()` | player.c | `player_marker_seq()` |
| `book_title_context_active()` | state.c | `state_context_active()` |
| `should_poll_book_title_marker()` | state.c | `state_should_poll_marker()` |
| `maybe_autostart_book_title()` | state.c | `state_maybe_autostart()` |
| `should_attempt_restore_for_position()` | state.c | `state_should_attempt_restore()` |
| `diag_inc()` / `diag_maybe_log()` | state.c | `state_diag_inc()` / `state_diag_log()` |
| `clear_book_title_autostart()` | state.c | `state_clear_autostart()` |
| `clear_restore_failure_state()` | resume.c | `resume_clear_failures()` |
| `note_seek_restore_failure()` | resume.c | `resume_note_seek_failure()` |
| `note_track_restore_failure()` | resume.c | `resume_note_track_failure()` |
| `restore_retry_delay_seconds()` | resume.c | `resume_retry_delay()` |
| `restore_target_ms()` | resume.c | `resume_target_ms()` |
| `should_defer_new_track_save()` | resume.c | `resume_should_defer_save()` |
| `should_skip_after_completed_restore()` | resume.c | `resume_should_skip_after_completed()` |
| `should_skip_failed_restore_save()` | resume.c | `resume_should_skip_failed_save()` |
| `book_title_log_bucket()` | state.c | `state_log_bucket()` |
| `log_book_title_restore_wait()` | state.c | `state_log_restore_wait()` |
| `log_book_title_pre_restore_skip()` | state.c | `state_log_pre_restore_skip()` |
| `ui_seek_restore()` | ui.c | `ui_seek_restore()` |
| `track_switch_settle_ticks()` | state.c | `state_settle_ticks()` |
| `book_title_should_preplay_direct_start()` | state.c | `state_should_preplay()` |
| `book_title_preplay_allow_memscan_root()` | state.c | `state_preplay_allow_memscan()` |

---

## 19. Implementation Order for Forge

Recommended implementation sequence (each step is independently testable):

1. **config.c** — defaults, env parsing, config file parsing. Unit testable immediately.
2. **log.c** — logging with rotation. Unit testable with temp files.
3. **proc_mem.c** — memory reading with mock file. Unit testable.
4. **catalog.c** — TSV parsing with test fixtures. Unit testable.
5. **player.c** — PID discovery, position/duration reads, path slot decoding. Testable with mock proc and user.ini.
6. **resume.c** — record read/write, completion, save/restore logic. Unit testable with temp dir.
7. **ui.c** — framebuffer reads (mock), event file sending (mock), touch synthesis. Unit testable.
8. **state.c** — state machine, autostart, track restore orchestration. Testable with mocked ui/player/resume.
9. **helpers.c** — subprocess management. Testable with mock helper scripts.
10. **shadow.c** — shadow mode wrappers. Trivial once other modules exist.
11. **main.c** — signal handling, main loop, wiring. Integration test.
12. **build.zig** — cross-compile setup. Verify binary size.
13. **tests/** — integration tests, QEMU tests, comparison tool.

---

*End of specification.*