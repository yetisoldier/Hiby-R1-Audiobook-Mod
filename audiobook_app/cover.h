/* cover.h — cached cover-art loader for the Now Playing screen.
 *
 * Loads a book's external cover.jpg/png (path stored in books.cover_path),
 * decodes it with libjpeg (dlopen'd, JPEG) or pngdec (libz dlopen'd, PNG),
 * downscales to a small square, and converts to RGB565 for a fast scaled blit
 * by the renderer. One cover is cached at a time (the current book); the cache
 * is freed on book change or cover_shutdown. Decode failure / no cover → NULL
 * (the UI then skips the cover and draws the existing layout, no gap).
 *
 * RAM-frugal: the decode buffer (full-res RGB8) is freed immediately after
 * downscale; only the small RGB565 square (e.g. 180x180 = 64.5 KB) is retained.
 */
#ifndef AUDIOBOOK_COVER_H
#define AUDIOBOOK_COVER_H

#include <stdint.h>

/* Forward-declare so cover.h doesn't drag in the full SQLite header. */
typedef struct sqlite3 sqlite3;

/* Cover is decoded + cached at this pixel size (square). Now Playing displays
 * it 1:1 (220px); the detail/title page displays it downscaled (170px) via the
 * renderer's scaled blit. 220x220 RGB565 = ~96.8 KB cache. */
#define COVER_PX 220

/* Get the cached RGB565 cover for book_id, or NULL if none/not decodable.
 * Loads + caches on first request for a new book_id; frees the previous
 * book's cover. Thread-safe enough for the UI pan-loop call site (single
 * UI thread; not called from the player thread). db is the library DB
 * handle (used to read the book's cover_path). */
const uint16_t *cover_get(sqlite3 *db, int book_id);

/* Small list-thumbnail size (square). Shown next to each book row in the
 * list view. Kept small so an LRU cache of several thumbnails fits in RAM
 * (COVER_THUMB_PX*COVER_THUMB_PX*2 bytes each). */
#define COVER_THUMB_PX 56

/* Get a small (COVER_THUMB_PX) RGB565 thumbnail for book_id, or NULL. Uses a
 * separate LRU cache (COVER_THUMB_CACHE entries) so scrolling a long list
 * doesn't thrash the Now-Playing cover or OOM.
 *
 * IMPORTANT: cover_thumb_cached() is cache-ONLY (never decodes) — safe to call
 * from the 30 fps render/pan hook. cover_thumb_prewarm() does the (slow) libjpeg
 * decode + insert; call it from the event loop, at most ONE per tick, so the
 * render path never blocks on a decode (that froze the device when draw_list
 * decoded ~7 thumbs in a single frame). */
const uint16_t *cover_thumb_cached(int book_id);  /* cache-only; render-safe */

/* Decode (or load the on-SD .r565) + store a thumbnail in the RAM LRU. Called
 * from the event loop at most ONE per tick. Returns 1 if the thumbnail is now in
 * the RAM cache (was already cached, or just decoded/loaded), 0 if it could not
 * be cached (no cover / not JPEG / decode failed — e.g. a progressive JPEG,
 * which the guard bails on). The caller uses the 0 to skip retrying this book so
 * one undecodable cover doesn't starve every book below it in the pre-warm walk. */
int cover_thumb_prewarm(sqlite3 *db, int book_id); /* returns 1=now cached, 0=failed */

/* Free the current cover cache (e.g. on screen exit / shutdown). */
void cover_clear(void);

/* Stop the cover subsystem: free the cover + thumbnail caches. */
void cover_shutdown(void);

#endif /* AUDIOBOOK_COVER_H */