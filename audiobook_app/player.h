/* player.h — audio playback engine for the audiobook app.
 *
 * Runs a decode->ALSA loop on a dedicated pthread so the UI/pan loop stays
 * responsive. MP3 is decoded by minimp3_ex (compiled directly into this .so);
 * M4B/AAC is demuxed by mp4_audio.c and decoded by libfdk-aac (dlopen'd at
 * runtime, optional). ALSA output is likewise dlopen'd. All external libs are
 * called via dlsym'd function pointers — matching how hiby_player itself uses
 * them, and keeping our .so free of link-time deps (preserves the
 * -fvisibility=hidden, no-symbol-clash design).
 *
 * UI reads live state each frame (state, position, total). Commands (play,
 * pause, seek, next/prev) are submitted under a mutex.
 */

#ifndef AUDIOBOOK_PLAYER_H
#define AUDIOBOOK_PLAYER_H

#include <stdint.h>
#include "library.h"   /* sqlite3 + progress/track/book APIs */

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_state_t;

/* Start the engine thread. Idempotent. db is the library DB handle (used for
 * track listing, book duration, and progress save/load). Returns 0 on
 * success, -1 if the decode/output libs can't be loaded. */
int player_init(sqlite3 *db);

/* Stop the engine thread, save progress, close libs. */
void player_shutdown(void);

/* Begin playing book_id. If resume!=0, load saved progress and seek to it.
 * If a different book is already playing, switches to this one. */
int player_play_book(int book_id, int resume);

/* Begin playing book_id starting at an absolute book-elapsed position (ms),
 * regardless of saved progress. Used by chapter-tap ("seek + play"). */
int player_play_book_from(int book_id, int64_t book_ms);

/* Play<->pause toggle. If stopped and a book was loaded before, resumes it. */
void player_toggle(void);

/* Pause (or stop if already paused). Saves progress. */
void player_pause(void);

/* Stop playback entirely; save progress. */
void player_stop(void);

/* Seek the current book to an absolute book-elapsed position (ms), then
 * resume playing. */
void player_seek_book_ms(int64_t ms);

/* Skip to the next/previous track in the current book. */
int player_next(void);
int player_prev(void);

/* Skip to the next/previous track in the current book. */
int player_next(void);
int player_prev(void);

/* ---- Bookmarks ---- */
/* Add a bookmark at the current playback position (current track + book-elapsed
 * position). `label` is shown in the bookmark list (may be NULL/empty →
 * "Bookmark"). Returns the new bookmark_id, or -1 if no book is loaded. */
int player_add_bookmark(const char *label);

/* ---- Playback speed ---- */
/* Set playback speed in milli-units (1000 = 1.0x, 1100 = 1.1x, 1250 = 1.25x,
 * 1500 = 1.5x). Clamped to [800, 2000]. At 1.0x the decode loop passes PCM
 * straight through (clean passthrough); at other speeds it time-stretches with
 * WSOLA (wsola.c) — tempo changes, PITCH PRESERVED. ALSA output rate stays at
 * the content rate (no DAC re-lock). WSOLA state is ~48 KB fixed (no malloc) and
 * re-inited on track open + speed change. Persisted via the settings table. */
void player_set_speed(int permille);
int  player_get_speed(void);   /* current speed, milli-units */

/* ---- Sleep timer ---- */
/* Set the sleep timer to fire after `minutes` minutes (0 = off/cancel).
 * When it fires, playback pauses and the timer clears itself. Setting a new
 * value (or 0) replaces any prior timer. */
void player_set_sleep_minutes(int minutes);

/* Remaining milliseconds until the sleep timer fires, or -1 if no timer is
 * armed. Clamps to >= 0 (returns 0 in the moment it fires). For UI countdown. */
int64_t player_sleep_remaining_ms(void);

/* ---- Volume (ALSA mixer on the DAC) ---- */
/* step: +1 = up, -1 = down. Clamps to [0,100], persists, applies to hardware. */
void player_volume_step(int dir);
/* Set absolute volume 0..100. */
void player_volume_set(int vol);
/* Current volume 0..100, or -1 if the mixer isn't available. */
int player_volume(void);

/* ---- Live state (read by the UI each frame; volatile) ---- */
player_state_t player_state(void);
int player_current_book(void);
int64_t player_position_ms(void);   /* book-elapsed position */
int64_t player_total_ms(void);      /* book total duration */
const char *player_current_track_title(void);

/* 0 = ok/supported, 1 = current book's format isn't playable (e.g. M4B but the
 * fdk-aac decode lib is missing, or an unsupported container/codec). */
int player_format_unsupported(void);

#endif /* AUDIOBOOK_PLAYER_H */