/*
 * catalog.h — catalog.tsv parsing, album pattern loading,
 *             field lookups by path/root/index
 *
 * Spec section 2.5 (catalog_entry, catalog_db), section 14.
 */

#ifndef CATALOG_H
#define CATALOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Catalog entry (spec 2.5) ─────────────────────────────────────── */

typedef struct {
    char     root[512];         /* field 1: book root path */
    int      index;             /* field 2: track index within book */
    int      count;             /* field 3: total tracks in book */
    int      media_id;          /* field 4: SQLite media DB row ID */
    char     path[512];         /* field 5: full file path */
    char     title[256];        /* field 6: track/chapter title */
    char     album[256];        /* field 7: album name (book title) */
    char     book_key[128];     /* field 9: stable book key */
} catalog_entry;

typedef struct {
    catalog_entry *entries;
    size_t count;
    char **album_patterns;
    size_t album_pattern_count;
} catalog_db;

/* ── API ──────────────────────────────────────────────────────────── */

/* Load catalog.tsv into a catalog_db.  Returns 0 on success, -1 on error.
 * If the file doesn't exist, returns 0 with an empty catalog (not an error). */
int catalog_load(catalog_db *db, const char *catalog_path,
                 const char *albums_path, const char *books_path);

/* Free all memory held by a catalog_db. */
void catalog_free(catalog_db *db);

/* Refresh album patterns from the catalog entries.
 * Extracts unique album names (field 7) into the album_patterns array. */
int refresh_catalog_album_patterns(catalog_db *db);

/* Look up a catalog entry by full path.
 * Returns a pointer to the matching entry, or NULL if not found. */
const catalog_entry *catalog_field_for_path(const catalog_db *db, const char *path);

/* Look up a catalog entry by root and track index.
 * Returns a pointer to the matching entry, or NULL if not found. */
const catalog_entry *catalog_field_for_root_index(const catalog_db *db,
                                                   const char *root, int index);

/* Find the first path in the catalog that belongs to the given root.
 * Writes the path to out_path.  Returns 0 on success, -1 if not found. */
int catalog_first_path_for_root(const catalog_db *db, const char *root,
                                 char *out_path, size_t out_len);

/* Resolve the book key for a given path.
 * Returns the book_key string, or an empty string if not found. */
const char *book_key_for_path(const catalog_db *db, const char *path);

#endif /* CATALOG_H */