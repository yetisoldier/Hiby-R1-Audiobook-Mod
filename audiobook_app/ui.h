/* ui.h — audiobook UI state machine, event loop, and screen definitions.
 *
 * Runs inside hook_b when the user taps the Audiobooks tile. Renders to
 * hiby_player's mmap'd framebuffer, reads touch from /dev/input/event1.
 *
 * Architecture:
 *   - ui_run() is the main entry, called from hook_b.
 *   - It initializes the renderer + input, opens the library DB.
 *   - The event loop: render current screen, poll input, handle events.
 *   - Each screen has its own draw + handle functions.
 *   - Navigation between screens via ui_screen_t enum.
 */

#ifndef AUDIOBOOK_UI_H
#define AUDIOBOOK_UI_H

#include <stdint.h>
#include "render.h"
#include "library.h"
#include "cover.h"   /* COVER_PX for the cover cache buffer */

/* ---- Screen identifiers ------------------------------------------------ */
typedef enum {
    SCREEN_HOME = 0,
    SCREEN_LIST,
    SCREEN_DETAIL,
    SCREEN_NOW_PLAYING,
    SCREEN_BOOKMARKS,
    SCREEN_CHAPTERS,
    SCREEN_SETTINGS,
} ui_screen_t;

/* ---- User settings ------------------------------------------------------
 * Persisted in the same SQLite settings table as playback_speed, and cached
 * here so the render thread never touches the database. Every one defaults to
 * 0, which is the behaviour the app had before they existed. */
typedef enum {
    SETTING_LOCK_DISABLE_TOUCH = 0,  /* blanked screen ignores touch entirely */
    SETTING_PROGRESS_BARS,           /* chapter + book bars in Now Playing */
    SETTING_SPEED_ADJUSTS_TIME,      /* elapsed/remaining follow playback speed */
    SETTING_COUNT
} ui_setting_t;

/* List view modes (what SCREEN_LIST shows) */
typedef enum {
    LIST_TITLES = 0,
    LIST_AUTHORS,
    LIST_SERIES,
    LIST_FOLDERS,
    LIST_FINISHED,
    LIST_CONTINUE,
    LIST_AUTHOR_BOOKS,
    LIST_SERIES_BOOKS,
} list_mode_t;

/* ---- Render-cache row structs (built by the event thread, read by the
 * render thread under g_cache_lock) --------------------------------------- */

/* A row in the List screen. is_folder distinguishes folder rows (drill-down)
 * from book rows. For book rows, has_progress/elapsed_ms drive the "%" line and
 * progress bar. */
typedef struct {
    int book_id;
    int is_folder;       /* 1 = folder row, 0 = book row */
    char title[256];
    char author[256];
    int64_t duration_ms;
    int completed;
    int has_progress;
    int64_t elapsed_ms;
} list_item_t;

/* A row in the Bookmarks screen. */
typedef struct {
    int bookmark_id;
    char label[256];
    int64_t position_ms;       /* total-book position for display */
} bookmark_row_t;

/* A row in the Chapters screen. */
typedef struct {
    char title[256];
    int64_t start_ms;
    int track_id;           /* disambiguates repeated titles across files */
} chapter_row_t;

/* Collector contexts used while (re)building the Bookmarks/Chapters caches.
 * Defined here so the event-thread rebuild functions (early in ui.c) can use
 * them before the collector callbacks are defined. */
typedef struct {
    bookmark_row_t *rows;
    int count;
    int capacity;
} bm_ctx_t;
typedef struct {
    chapter_row_t *rows;
    int count;
    int capacity;
} ch_ctx_t;

/* ---- UI state ----------------------------------------------------------- */
typedef struct {
    renderer_t rend;
    sqlite3 *db;
    int input_fd;           /* /dev/input/event1 (touch), our own fd, grabbed */
    /* Key device fds. hiby_player EVIOCGRABs the key devices exclusively, so a
     * fresh open() of /dev/input/eventN gets nothing. Instead we dup() one of
     * hiby_player's own already-open, already-grabbed fds (found via
     * /proc/self/fd — we're the same process, so we have access, and since
     * we've blocked hiby_player's main thread those fds sit unread with key
     * events queued). key_fds[0..n_key_fds-1] are the dup'd non-blocking fds. */
    int key_fd;              /* legacy: /dev/input/event2 fresh open or -1 */
    int key_fds[4];
    int key_stock_fds[4];    /* stock fd dup retained to restore EVIOCGRAB */
    int key_stock_flags[4];  /* original stock open-file status flags */
    int key_exclusive[4];    /* key_fds[i] currently owns EVIOCGRAB */
    int n_key_fds;

    /* Bluetooth AVRCP remote (e.g. a BT speaker's play/pause button). The
     * speaker registers a virtual input device "/dev/input/event<n>" named
     * "<device> (AVRCP)" when connected; it appears/disappears with the BT
     * link, so we (re)open it lazily and retry periodically. Not EVIOCGRABbed
     * by hiby_player (it didn't exist at its startup), so a fresh open works.
     * Codes seen on a Sony SRS-XB43: KEY_PLAYCD=200 / KEY_PAUSECD=201 (the one
     * play/pause button alternates between them). We treat both as a toggle. */
    int avrcp_fd;
    uint64_t avrcp_next_open_ms;

    /* Screen blank (power button). Lightweight: backlight off only — we keep
     * panning so the touch IC stays alive (enables double-tap wake) and the
     * decode thread keeps running (audiobook plays with the screen dark). */
    int blanked;             /* 1 = screen blanked (backlight off) */
    int saved_brightness;    /* brightness to restore on wake */
    uint64_t last_touch_up_ms; /* for double-tap-to-wake while blanked */

    ui_screen_t screen;
    list_mode_t list_mode;
    char folder_path[512];   /* LIST_FOLDERS drill-down: current folder path
                              * ("" = library root AUDIOBOOK_LIBRARY_ROOT).
                              * Saved/restored across the nav stack. */
    int scroll_offset;       /* vertical scroll in list views */
    int selected_idx;        /* currently highlighted item */

    /* Home screen selection */
    int home_selected;
    int settings_selected;
    int settings_val[SETTING_COUNT];

    /* Current book for detail/now-playing */
    int current_book_id;

    /* Filter string for LIST_AUTHOR_BOOKS / LIST_SERIES_BOOKS (the author or
     * series display name tapped on the Authors/Series list). */
    char list_filter[256];

    /* Navigation stack for back button */
    ui_screen_t nav_stack[8];
    list_mode_t nav_list_mode[8];
    char nav_folder_path[8][512];  /* per-nav-depth saved folder_path */
    int nav_book_id[8];
    int nav_depth;

    /* Running flag */
    int running;

    /* Last touch state */
    int touch_active;
    int touch_x;
    int touch_y;
    int touch_start_x;
    int touch_start_y;
    uint64_t touch_down_ms;     /* finger-down time, for long-press detection */

    /* Sleep timer: selected duration in minutes (0 = off, 15/30/60). The
     * deadline itself lives in the player engine (persists across navigation);
     * this just records the UI's current cycle slot. */
    int sleep_minutes;

    /* Now Playing progress-bar y (screen coords), set by draw_now_playing each
     * frame so handle_now_playing_touch can hit-test it for scrub-seek. -1 when
     * no bar is drawn (e.g. unknown duration). */
    int seek_bar_y;

    /* Scrub-drag of the Now Playing progress handle. The handle is a small
     * circle at the current position; pressing it starts a scrub — the handle
     * follows the finger (scrub_preview_ms) and the time display updates live,
     * and on finger-up the seek is committed. Pressing elsewhere on the bar
     * does NOT seek (prevents accidental progress changes). */
    int scrub_active;        /* 1 while the user is dragging the handle */
    int64_t scrub_total_ms;  /* total ms captured at drag start */
    int64_t scrub_preview_ms;/* live finger-position target, shown while dragging */

    /* "Mark" button feedback: the Now Playing Mark button flashes green briefly
     * after adding a bookmark, so the user sees the tap registered. */
    uint64_t mark_flash_until_ms;

    /* Transient hardware-volume feedback. Physical volume presses are handled
     * by the event thread; the render thread draws this allocation-free HUD on
     * top of whichever audiobook screen is active. */
    uint64_t volume_overlay_until_ms;
    int volume_overlay_pct;

    /* Physical volume-key hold ramp. The R1 key driver does not reliably emit
     * EV_KEY value=2 repeats, so the event loop generates repeats from the
     * down/up state after a short initial delay. */
    int volume_hold_dir;             /* -1 down, +1 up, 0 released */
    uint64_t volume_hold_started_ms;
    uint64_t volume_hold_next_ms;

    /* "Library refreshed" confirmation flash on the Home screen after a
     * Home → Refresh tap runs the scan. 0 = no flash showing. */
    uint64_t refresh_msg_until_ms;

    /* "Scan failed" error flash (red) shown when a Home → Refresh scan aborts
     * — e.g. /usr/data is too full to write library.db. 0 = no flash showing. */
    uint64_t refresh_err_until_ms;

    /* Refresh runs on a private DB connection in a worker so touch, keys,
     * playback, and framebuffer panning stay responsive during long scans. */
    int refresh_scanning;

    /* Thumbnail pre-warm: draw_list (render hook) records the first visible
     * book_id whose thumbnail isn't cached yet; the event loop decodes ONE per
     * tick via cover_thumb_prewarm. Keeps libjpeg decode OUT of the render path
     * (decoding ~7 thumbs in one frame froze the device). 0 = nothing pending. */
    int thumb_warm_target;

    /* Books whose thumbnail pre-warm has FAILED (no cover / not JPEG / decode
     * failed — e.g. a progressive JPEG, which the guard bails on). draw_list
     * skips these when choosing thumb_warm_target, so one undecodable cover
     * doesn't stay "first uncached visible" forever and starve every book below
     * it. Cleared on screen change so a fresh visit re-tries (a failed
     * progressive decode is nearly free — it bails at read_header). */
    int thumb_failed[64];
    int thumb_failed_n;

    /* Live drag-scroll. scroll_max is the largest valid scroll_offset for the
     * current screen (set by each draw_* from item count × row height −
     * viewport); the touch handler clamps drag to [0, scroll_max]. did_scroll
     * is set when a drag actually moved the list, so finger-up swallows the
     * event (no tap-opens-book, no back-swipe) — without it, a scroll drag
     * would either open a book or navigate back. */
    int scroll_max;
    int did_scroll;

    /* ---- Render cache ----------------------------------------------------
     * The render/pan thread never touches the DB. The event thread rebuilds
     * these per-screen snapshots (all DB I/O done BEFORE taking g_cache_lock),
     * then swaps the pointer under g_cache_lock and frees the old buffer. The
     * render thread holds g_cache_lock across its whole memory-walk draw of the
     * cached rows — so it can never observe a half-swapped/freed buffer and
     * never races the event thread. Single non-nested lock → no deadlock. */
    /* Home screen counts. */
    int home_continue_n;
    int home_finished_n;
    int home_total_n;

    /* List screen cache. list_is_strlist selects the active array: Authors/
     * Series use strlist (+strlist_count); all other modes use list_items (+
     * list_count). draw_list branches on this flag (not ui->list_mode) so it
     * always renders exactly what the cache holds, even across a one-frame
     * mode change. */
    list_item_t *list_items;
    int list_count;
    int list_cap;
    char **strlist;
    int strlist_count;
    int strlist_cap;
    int list_is_strlist;

    /* Detail / Now-Playing cache for current_book_id. cur_cover_buf holds the
     * decoded RGB565 cover (copied from cover_get's single-book buffer under
     * the lock, so the render blit can never race a re-decode of that buffer). */
    audiobook_book_t cur_book;
    int cur_book_ok;
    audiobook_progress_t cur_prog;
    int cur_prog_ok;
    char cur_description[2048];
    uint16_t cur_cover_buf[COVER_PX * COVER_PX];
    int cur_cover_ok;

    /* Bookmarks / Chapters screen caches. */
    bookmark_row_t *bm_rows;
    int bm_count;
    int bm_cap;
    chapter_row_t *ch_rows;
    int ch_count;
    int ch_cap;
} ui_state_t;

/* Main entry — called from hook_b. Runs until user taps "back to menu".
 * The event loop reads touch but does NOT draw — the ioctl hook in hook.c
 * calls ui_draw_frame() on every hiby_player frame to render the UI. */
int ui_run(uint16_t *fb, int fb_fd);

/* Load the persisted user settings into ui->settings_val. Called once at
 * startup; the screen updates the cache in place thereafter. */
void ui_settings_load(ui_state_t *ui, sqlite3 *db);

/* Draw the current screen to a specific framebuffer buffer.
 * Called from the ioctl hook (in hiby_player's render thread) on every
 * FBIOPAN_DISPLAY. buf points to the start of the target buffer (already
 * offset by yoffset). Clears the buffer then draws the current screen. */
void ui_draw_frame(uint16_t *buf);

/* Called by the LD_PRELOAD ioctl hook when stock hiby_player requests
 * FBIOBLANK while the audiobook UI is active. The event loop converts hard
 * display power-down into the app's lightweight backlight-only blank. */
void ui_notify_fb_blank(int blanked);

/* Touch event handlers (called from event loop). */
int ui_handle_tap(ui_state_t *ui, int x, int y);
int ui_handle_swipe(ui_state_t *ui, int dx, int dy);

#endif /* AUDIOBOOK_UI_H */
