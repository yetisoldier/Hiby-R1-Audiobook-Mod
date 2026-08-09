#ifndef AUDIOBOOK_MUSIC_CATALOG_H
#define AUDIOBOOK_MUSIC_CATALOG_H

#include <stddef.h>

typedef struct {
    int databases_checked;
    int databases_changed;
    int databases_failed;
    int audiobook_rows_removed;
} music_catalog_cleanup_result_t;

/* Remove /Audiobooks entries from HiBy's stock Music databases. Missing DB
 * paths are expected and are not failures. Returns 0 when every existing DB
 * was cleaned successfully, or -1 when at least one existing DB failed. */
int music_catalog_remove_audiobooks(
    const char *const *db_paths,
    size_t path_count,
    music_catalog_cleanup_result_t *result);

int music_catalog_remove_audiobooks_default(
    music_catalog_cleanup_result_t *result);

#endif
