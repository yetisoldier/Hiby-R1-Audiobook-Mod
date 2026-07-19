/* scan.h — audiobook library scanner.
 *
 * Walks the Audiobooks directory, groups files into books by folder,
 * reads tags, computes durations, sorts tracks, and upserts everything
 * into the library database. Idempotent: re-scanning only updates
 * changed books.
 */

#ifndef AUDIOBOOK_SCAN_H
#define AUDIOBOOK_SCAN_H

#include "library.h"

/* Scan progress callback. stage: 0=starting, 1=scanning folder,
 * 2=processing book, 3=indexing search, 4=cleaning orphans, 5=done.
 * current/current_total for progress display. */
typedef void (*scan_progress_cb)(int stage, int current, int total,
                                 const char *info, void *ctx);

/* Scan the library root and upsert all books/tracks/chapters into the DB.
 * root_path: typically AUDIOBOOK_LIBRARY_ROOT.
 * Returns 0 on success, -1 on error.
 * If progress is non-NULL, it's called with status updates. */
int audiobook_scan_library(sqlite3 *db, const char *root_path,
                           scan_progress_cb progress, void *ctx);

/* Clean up orphaned DB entries: books whose root_path no longer exists,
 * tracks whose file no longer exists. Returns count of removed items. */
int audiobook_cleanup_orphans(sqlite3 *db, scan_progress_cb progress,
                              void *ctx);

#endif /* AUDIOBOOK_SCAN_H */