/* posstore.h — SD-primary playback-position store.
 *
 * The listener's place is saved to the SD card, NOT to library.db on
 * /usr/data, so a full /usr/data (chronic on this device — UBIFS overhead +
 * the stock music DB that the scanner keeps recreating on /usr/data) can
 * NEVER lose the position. One tiny file per book at POS_DIR/<book_id>.pos.
 *
 * library.db progress is still written (best-effort, by save_progress) as a
 * mirror for the list view's "%" display; its failure is harmless because
 * the SD file is authoritative on resume. On open/resume we read the SD
 * file first and fall back to library.db only for positions saved by older
 * builds (pre-2.0.9) that wrote library.db alone. */

#ifndef AUDIOBOOK_POSSTORE_H
#define AUDIOBOOK_POSSTORE_H

#include <stdint.h>

/* Directory on the SD card holding one <book_id>.pos per book. The SD is at
 * /usr/data/mnt/sd_0 (exFAT, rw) on this device. */
#define POS_DIR "/usr/data/mnt/sd_0/.audiobook_pos"

/* Save position (authoritative). completed: 0 = mid-listen, 1 = finished.
 * Writes to a temp file then renames (atomic-ish on exFAT within one dir) so
 * a partial write can never corrupt an existing .pos. No-op if the SD is
 * absent/read-only — position then survives only via the library.db mirror. */
void pos_save_sd(int book_id, int track_ordinal, int64_t track_pos_ms,
                 int64_t book_elapsed_ms, int completed);

/* Load saved position. Returns 1 if a usable SD .pos exists (out fields
 * filled; any out ptr may be NULL), 0 if none — caller falls back to
 * library.db. */
int  pos_load_sd(int book_id, int *track_ordinal, int64_t *track_pos_ms,
                 int64_t *book_elapsed_ms, int *completed);

/* Remove a book's SD .pos (and its .tmp). Called when a book is deleted or
 * pruned as an orphan so stale position files don't accumulate. */
void pos_remove_sd(int book_id);

#endif /* AUDIOBOOK_POSSTORE_H */