/*
 * ui.h — framebuffer pixel checks, touch/key event injection,
 *         seek-bar restore, play-mode enforcement, back guard
 *
 * Spec section 5 (UI automation interface).
 * All framebuffer and input device operations fail gracefully
 * (log warning, return error) if devices can't be opened.
 */

#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"

/* ── Framebuffer pixel operations ─────────────────────────────────── */

/*
 * Count "white-like" pixels in a rectangular region of /dev/fb0.
 * Uses RGB565 decode: r = v>>11, g = (v>>5)&0x3f, b = v&0x1f.
 * "white" = r >= 24 && g >= 48 && b >= 24.
 *
 * Returns the white pixel count, or -1 on error.
 */
int fb_white_pixels_region(int fb_fd, uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1,
                           uint16_t stride);

/*
 * Check if the seek bar is visible on the Now Playing screen.
 * Counts white+blue pixels in the seek bar row and compares to
 * ui_seek_screen_min_bar_pixels threshold.
 *
 * Returns true if screen is ready (or screen guard disabled), false otherwise.
 */
bool ui_seek_screen_ready(int fb_fd, const daemon_config *cfg);

/*
 * Check if the audiobook subheader is visible.
 * Counts white pixels in region (60,118)-(220,155).
 *
 * Returns true if white pixel count >= back_guard_subheader_min_white.
 */
bool audiobook_subheader_visible(int fb_fd, const daemon_config *cfg);

/*
 * Check if the audiobook title list screen is visible.
 * Combines subheader, header-mid, and header-icon pixel checks.
 *
 * Returns true if title list is visible.
 */
bool audiobook_title_list_visible(int fb_fd, const daemon_config *cfg);

/*
 * Check if the audiobook track list screen is visible.
 * Subheader visible + header pixels + NOT title list.
 *
 * Returns true if track list is visible.
 */
bool audiobook_track_list_visible(int fb_fd, const daemon_config *cfg);

/*
 * Check if the global back target is visible (post-audiobook exit).
 * Subheader low + header high + back-arrow high.
 *
 * Returns true if global back target is visible.
 */
bool audiobook_global_back_target_visible(int fb_fd, const daemon_config *cfg);

/* ── Input event injection ───────────────────────────────────────── */

/*
 * Write one input_event struct (24 bytes on MIPS LE) to an event node fd.
 * type, code, value are the input event fields; time is zeroed.
 *
 * Returns 0 on success, -1 on error.
 */
int send_input_event(int fd, uint16_t type, uint16_t code, int32_t value);

/*
 * Emit a touch absolute frame (EV_ABS events for a touch at x, y).
 * If include_press is true, also emits BTN_TOUCH=1.
 * Writes to fd.
 *
 * Returns 0 on success, -1 on error.
 */
int emit_touch_abs_frame(int fd, uint16_t x, uint16_t y, int include_press);

/*
 * Synthesize a touch tap at (x, y) and write to an output file.
 * Creates a binary file with input_event structs: press + frames + release.
 *
 * Returns 0 on success, -1 on error.
 */
int write_touch_tap_stream(const char *output_file, uint16_t x, uint16_t y,
                           uint8_t frames);

/*
 * Send a pre-recorded event file to an input device node.
 * Reads the binary file and writes its contents to the device node.
 *
 * Returns 0 on success, -1 if file or device is unavailable.
 */
int ui_send_event_file(const char *label, const char *event_file,
                       const char *event_node);

/*
 * Synthesize and send a touch tap in one step.
 * Writes a temp file with touch events, sends it to the touch device,
 * then removes the temp file.
 *
 * Returns 0 on success, -1 on error.
 */
int touch_generated_tap(const char *label, uint16_t x, uint16_t y,
                        uint8_t frames, const daemon_config *cfg);

/* ── High-level touch helpers ─────────────────────────────────────── */

/*
 * Tap the first track in the list.
 * Tries the pre-recorded event file, or the down/move/up sequence.
 *
 * Returns 0 on success, -1 on error.
 */
int touch_first_track(const daemon_config *cfg);

/*
 * Tap back to return to the track list.
 * Uses the pre-recorded back event file.
 *
 * Returns 0 on success, -1 on error.
 */
int touch_back_to_track_list(const daemon_config *cfg);

/*
 * Tap a specific track row (1-5).
 * Uses pre-recorded event files for each row.
 *
 * Returns 0 on success, -1 on error or invalid row.
 */
int touch_track_row(int row, const daemon_config *cfg);

/*
 * Swipe up to scroll the track list.
 * Sends a sequence of swipe down + move events + swipe up.
 *
 * Returns 0 on success, -1 on error.
 */
int touch_track_swipe_up(const daemon_config *cfg);

/*
 * Send the "next track" key event.
 * Tries key-next event file first, then touch-next as fallback.
 *
 * Returns 0 on success, -1 on error.
 */
int track_next(const daemon_config *cfg);

/*
 * Send the "previous track" key event.
 *
 * Returns 0 on success, -1 on error.
 */
int track_prev(const daemon_config *cfg);

/* ── Seek-bar restore ─────────────────────────────────────────────── */

/*
 * Compute the seek bar X position for a given position/duration.
 * Maps position linearly to [x_min+1, x_max-1] range.
 *
 * Returns the X coordinate, or 0 if inputs are invalid.
 */
uint16_t ui_seek_compute_x(uint32_t saved_pos, uint32_t duration,
                           uint16_t x_min, uint16_t x_max);

/*
 * Tap the seek bar at a computed X position to restore playback position.
 * Verifies the position after tapping.
 *
 * Returns 0 on success, -1 on error.
 */
int ui_seek_restore(const char *path, uint32_t saved_pos,
                    const daemon_config *cfg);

/* ── Play-mode enforcement ────────────────────────────────────────── */

/*
 * Read the current play mode value from user.ini.
 * Reads a single byte at play_mode_user_ini_offset.
 *
 * Returns the mode value (0-255), or -1 on error.
 */
int play_mode_value(const daemon_config *cfg);

/*
 * Check if the Now Playing screen is active for play-mode enforcement.
 * If screen guard is disabled, returns true.
 * Otherwise, checks if the seek bar is visible.
 *
 * Returns true if screen is ready, false otherwise.
 */
bool play_mode_screen_ready(int fb_fd, const daemon_config *cfg);

/*
 * Ensure the player is in the correct play mode.
 * Taps the play-mode button until the mode matches the target.
 *
 * Returns 0 on success (mode matches or enforcement disabled), -1 on error.
 */
int ensure_audiobook_play_mode(const daemon_config *cfg);

/* ── Back guard ───────────────────────────────────────────────────── */

/*
 * Structure for back-guard runtime state (defined in state.h, but
 * we keep a minimal copy here to avoid circular dependency).
 * The caller passes the relevant fields from daemon_runtime.
 */
typedef struct {
    time_t audiobook_back_guard_until;
    time_t audiobook_back_guard_seen_at;
    time_t audiobook_back_guard_last_fire_at;
} back_guard_state;

/*
 * Activate the back guard window.
 * Sets audiobook_back_guard_until = now + back_guard_window_seconds.
 */
void enable_audiobook_back_guard_window(time_t now,
                                        back_guard_state *bg,
                                        const daemon_config *cfg);

/*
 * Check and fire the back guard if needed.
 * Examines the framebuffer and sends extra back taps to escape
 * the audiobook context if needed.
 */
void maybe_audiobook_back_guard(time_t now, const char *path_preview,
                                back_guard_state *bg,
                                const daemon_config *cfg);

/* ── Book title wait ──────────────────────────────────────────────── */

/*
 * Wait for the launcher track list to appear on screen.
 * Polls the framebuffer for up to book_title_launcher_tracklist_wait_seconds.
 *
 * Returns 0 if track list becomes visible, -1 on timeout.
 */
int book_title_wait_for_launcher_track_list(const daemon_config *cfg);

/* ── Pixel decode helper (exposed for testing) ──────────────────── */

/*
 * Decode a 16-bit RGB565 pixel value and check if it's "white".
 * r = v >> 11, g = (v >> 5) & 0x3f, b = v & 0x1f
 * white = r >= 24 && g >= 48 && b >= 24
 *
 * Returns true if the pixel is classified as white.
 */
bool pixel_is_white(uint16_t v);

/*
 * Decode a 16-bit RGB565 pixel value and check if it's "blue".
 * blue = r <= 10 && g >= 24 && b >= 18
 *
 * Returns true if the pixel is classified as blue.
 */
bool pixel_is_blue(uint16_t v);

#endif /* UI_H */