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
    int n_key_fds;

    /* Screen blank (power button). Lightweight: backlight off only — we keep
     * panning so the touch IC stays alive (enables double-tap wake) and the
     * decode thread keeps running (audiobook plays with the screen dark). */
    int blanked;             /* 1 = screen blanked (backlight off) */
    int saved_brightness;    /* brightness to restore on wake */
    uint64_t last_touch_up_ms; /* for double-tap-to-wake while blanked */

    ui_screen_t screen;
    list_mode_t list_mode;
    int scroll_offset;       /* vertical scroll in list views */
    int selected_idx;        /* currently highlighted item */

    /* Home screen selection */
    int home_selected;

    /* Current book for detail/now-playing */
    int current_book_id;

    /* Filter string for LIST_AUTHOR_BOOKS / LIST_SERIES_BOOKS (the author or
     * series display name tapped on the Authors/Series list). */
    char list_filter[256];

    /* Navigation stack for back button */
    ui_screen_t nav_stack[8];
    list_mode_t nav_list_mode[8];
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

    /* "Library refreshed" confirmation flash on the Home screen after a
     * Home → Refresh tap runs the scan. 0 = no flash showing. */
    uint64_t refresh_msg_until_ms;

    /* "Scan failed" error flash (red) shown when a Home → Refresh scan aborts
     * — e.g. /usr/data is too full to write library.db. 0 = no flash showing. */
    uint64_t refresh_err_until_ms;

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
} ui_state_t;

/* Main entry — called from hook_b. Runs until user taps "back to menu".
 * The event loop reads touch but does NOT draw — the ioctl hook in hook.c
 * calls ui_draw_frame() on every hiby_player frame to render the UI. */
int ui_run(uint16_t *fb, int fb_fd);

/* Draw the current screen to a specific framebuffer buffer.
 * Called from the ioctl hook (in hiby_player's render thread) on every
 * FBIOPAN_DISPLAY. buf points to the start of the target buffer (already
 * offset by yoffset). Clears the buffer then draws the current screen. */
void ui_draw_frame(uint16_t *buf);

/* Touch event handlers (called from event loop). */
int ui_handle_tap(ui_state_t *ui, int x, int y);
int ui_handle_swipe(ui_state_t *ui, int dx, int dy);

#endif /* AUDIOBOOK_UI_H */