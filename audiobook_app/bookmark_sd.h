/* bookmark_sd.h — SD-primary bookmark store.
 *
 * Bookmarks live on the SD card, NOT in library.db on /usr/data, so a full
 * /usr/data (chronic on this device — UBIFS overhead + the stock music DB the
 * scanner keeps recreating) can never lose or refuse to add a bookmark. One
 * tiny file per book at POS_DIR/<book_id>.bm, alongside the <book_id>.pos
 * position files.
 *
 * library.db is untouched for books/tracks/chapters/progress; the old
 * `bookmarks` table stays in the schema but is no longer read or written by
 * the app. Existing in-DB bookmarks are migrated to SD the first time a
 * book's bookmark screen is opened (library.c::audiobook_list_bookmarks).
 *
 * The created_at timestamp doubles as the bookmark_id reported to the UI
 * (it fills audiobook_bookmark_t.bookmark_id), so the existing jump/delete
 * paths in ui.c work unchanged. Writes are temp-then-rename within one
 * directory, so a power cut can never corrupt an existing .bm — at worst a
 * new write is lost (and a partially-written line is skipped on read). */

#ifndef AUDIOBOOK_BOOKMARK_SD_H
#define AUDIOBOOK_BOOKMARK_SD_H

#include <stdint.h>
#include "library.h"   /* audiobook_bookmark_t */
#include "posstore.h"  /* POS_DIR */

/* Add a bookmark to the SD store. Returns 0 on success and fills
 * *out_created_at with the assigned id (== created_at), or -1 if the SD is
 * absent/read-only or the write fails. The existing .bm is never corrupted:
 * the write goes to a temp file then renames. out_created_at may be NULL. */
int bookmark_save_sd(int book_id, int track_id, int64_t position_ms,
                     int64_t total_book_position_ms, const char *label,
                     int *out_created_at);

/* List bookmarks for a book, newest first. Returns the count reported, or -1
 * on read error (0 == no bookmarks / no file yet). Malformed lines skipped. */
int bookmark_list_sd(int book_id,
                     int (*cb)(const audiobook_bookmark_t *bm, void *ctx),
                     void *ctx);

/* Delete the bookmark whose id (== created_at) matches. Returns 0 on
 * success, -1 on error. Removes the file entirely if it becomes empty. */
int bookmark_delete_sd(int book_id, int created_at);

/* True if a .bm file (even an empty marker) exists for this book. Used by
 * library.c to run the one-time DB->SD migration exactly once per book. */
int bookmark_file_exists_sd(int book_id);

/* One-time migration: write the given DB rows (NEWEST-first, as the SELECT
 * ORDER BY created_at DESC returns them) to the .bm file, preserving each
 * row's created_at as its id. An empty marker file is created when count==0
 * so the DB is never re-queried. Returns 0/-1. */
int bookmark_migrate_sd(int book_id, const audiobook_bookmark_t *rows, int count);

/* Remove a book's entire SD bookmark file (and .tmp). Called on orphan prune
 * alongside pos_remove_sd so stale .bm files don't accumulate. */
void bookmark_remove_book_sd(int book_id);

#endif /* AUDIOBOOK_BOOKMARK_SD_H */