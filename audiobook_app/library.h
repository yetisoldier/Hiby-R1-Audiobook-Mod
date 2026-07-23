/* library.h — audiobook library database layer.
 *
 * Provides SQLite-based library management: schema bootstrap, book/track/
 * chapter/progress/bookmark CRUD, FTS5 search, and book.json sidecar I/O.
 *
 * The schema is recovered from the orphaned r1_audiobook_app binary and
 * matches its layout exactly so the DB is interchangeable.
 */

#ifndef AUDIOBOOK_LIBRARY_H
#define AUDIOBOOK_LIBRARY_H

#include <stdint.h>
#include "sqlite3.h"

/* ---- Constants --------------------------------------------------------- */

/* library.db now lives on the SD card (exFAT). /usr/data is chronically ~95%
 * full (the stock music DB rebuilds on every boot and dominates the ~36 MB
 * UBIFS partition), so catalog growth or a progress save that hits
 * SQLITE_FULL can leave the DB in a broken state. SD has gigabytes free and
 * the DB is <1 MB — moving it here durably fixes the "storage full" scan block
 * and the stale-WAL freeze. The old path is kept for one-time migration. */
#define AUDIOBOOK_DB_DIR        "/usr/data/mnt/sd_0/Audiobooks/.audiobook_library"
#define AUDIOBOOK_DB_PATH       "/usr/data/mnt/sd_0/Audiobooks/.audiobook_library/library.db"
#define AUDIOBOOK_DB_PATH_OLD   "/usr/data/audiobooks/library.db"
#define AUDIOBOOK_DATA_DIR      "/usr/data/audiobooks"
#define AUDIOBOOK_COVER_CACHE   "/usr/data/audiobooks/cache/covers"
#define AUDIOBOOK_RUN_DIR       "/usr/data/audiobooks/run"
#define AUDIOBOOK_LIBRARY_ROOT  "/usr/data/mnt/sd_0/Audiobooks"
#define SCHEMA_VERSION          "2"

/* ---- Types -------------------------------------------------------------- */

typedef struct {
    int book_id;
    char book_key[512];
    char title[512];
    char sort_title[512];
    char author[256];
    char narrator[256];
    char series[256];
    double series_number;
    char root_path[512];
    char cover_path[512];
    char cover_cache_path[512];
    int64_t total_duration_ms;
    int track_count;
    char fingerprint[64];
    int date_added;
    int date_modified;
    int last_played_at;
    int completed;
    int completed_at;
    double playback_speed;
} audiobook_book_t;

typedef struct {
    int track_id;
    int book_id;
    int ordinal;
    int disc_number;
    int track_number;
    char path[512];
    char title[512];
    char sort_title[512];
    int64_t duration_ms;
    int embedded_chapters;
    int64_t file_size;
    int file_mtime;
    char fingerprint[64];
} audiobook_track_t;

typedef struct {
    int chapter_id;
    int track_id;
    int ordinal;
    char title[256];
    int64_t start_ms;
    int64_t end_ms;
    int bookmarkable;
} audiobook_chapter_t;

typedef struct {
    int book_id;
    int track_id;
    int track_ordinal;
    int64_t position_ms;
    int64_t total_book_elapsed_ms;
    double playback_speed;
    int last_played_at;
    int completed;
    int completed_at;
    int last_saved_at;
    int64_t protected_until_ms;
} audiobook_progress_t;

typedef struct {
    int bookmark_id;
    int book_id;
    int track_id;
    int64_t position_ms;
    int64_t total_book_position_ms;
    char label[256];
    int created_at;
    int updated_at;
} audiobook_bookmark_t;

/* ---- API ---------------------------------------------------------------- */

/* Open or create the library database. Returns 0 on success, -1 on error.
 * Handles: create data dirs, validate/open DB, bootstrap schema, set journal + FK. */
int audiobook_db_open(const char *db_path, sqlite3 **db_out);

/* Close the database. */
void audiobook_db_close(sqlite3 *db);

/* Write mutex for the shared library.db (now on SD/exFAT). Since the build is
 * -DSQLITE_THREADSAFE=2, each connection is single-thread-owned, while exFAT
 * fcntl locks may be no-ops. This app-level mutex serializes the two writers
 * (scan on the event thread + save_progress on the player thread) so they
 * never collide in the WAL. Readers do NOT take this lock — WAL readers always
 * see a consistent committed snapshot regardless of a concurrent writer. */
void audiobook_db_write_lock(void);
int audiobook_db_write_trylock(void); /* 0 = acquired, nonzero = busy */
void audiobook_db_write_unlock(void);

/* ---- Book queries ------------------------------------------------------- */

/* Get a book by book_key. Returns 1 if found, 0 if not found, -1 on error. */
int audiobook_get_book_by_key(sqlite3 *db, const char *book_key,
                              audiobook_book_t *out);

/* Get a book by book_id. Returns 1 if found, 0 if not found, -1 on error. */
int audiobook_get_book(sqlite3 *db, int book_id, audiobook_book_t *out);

/* Get the publisher summary stored for a book. Returns 1 if a non-empty
 * description was found, 0 if absent, and -1 on error. */
int audiobook_get_book_description(sqlite3 *db, int book_id,
                                   char *out, int out_len);

/* Get all books ordered by sort_title. Returns count or -1 on error.
 * Calls cb for each book; stops if cb returns non-zero. */
int audiobook_list_books(sqlite3 *db,
                         int (*cb)(const audiobook_book_t *book, void *ctx),
                         void *ctx);

/* Get continue-listening books (in-progress, not completed, ordered by
 * last_played_at desc). Returns count or -1. */
int audiobook_list_continue(sqlite3 *db,
                            int (*cb)(const audiobook_book_t *book, void *ctx),
                            void *ctx);

/* Get completed books. Returns count or -1. */
int audiobook_list_finished(sqlite3 *db,
                           int (*cb)(const audiobook_book_t *book, void *ctx),
                           void *ctx);

/* List distinct authors. Returns count or -1. */
int audiobook_list_authors(sqlite3 *db,
                          int (*cb)(const char *author, void *ctx),
                          void *ctx);

/* List distinct series. Returns count or -1. */
int audiobook_list_series(sqlite3 *db,
                         int (*cb)(const char *series, void *ctx),
                         void *ctx);

/* List distinct root_paths (folders). Returns count or -1. */
int audiobook_list_folders(sqlite3 *db,
                          int (*cb)(const char *path, void *ctx),
                          void *ctx);

/* List books by author display_name (ordered by series_number, sort_title).
 * Returns count or -1. */
int audiobook_list_books_by_author(sqlite3 *db, const char *author,
                                    int (*cb)(const audiobook_book_t *book,
                                              void *ctx),
                                    void *ctx);

/* List books by series display_name (ordered by series_number, sort_title).
 * Returns count or -1. */
int audiobook_list_books_by_series(sqlite3 *db, const char *series,
                                    int (*cb)(const audiobook_book_t *book,
                                              void *ctx),
                                    void *ctx);

/* ---- Track queries ------------------------------------------------------ */

/* Get all tracks for a book, ordered by disc/track/ordinal. Returns count. */
int audiobook_get_tracks(sqlite3 *db, int book_id,
                        int (*cb)(const audiobook_track_t *track, void *ctx),
                        void *ctx);

/* ---- Chapter queries ---------------------------------------------------- */

/* Get all chapters for a book (across all tracks), ordered by disc/track/
 * chapter ordinal. Returns count. */
int audiobook_get_chapters(sqlite3 *db, int book_id,
                          int (*cb)(const audiobook_chapter_t *ch, void *ctx),
                          void *ctx);

/* ---- Progress ----------------------------------------------------------- */

/* Get progress for a book. Returns 1 if found, 0 if not, -1 on error. */
int audiobook_get_progress(sqlite3 *db, int book_id,
                           audiobook_progress_t *out);

/* Save progress (upsert). Returns 0 on success, -1 on error. */
int audiobook_save_progress(sqlite3 *db, const audiobook_progress_t *p);

/* ---- Bookmarks ---------------------------------------------------------- */

/* Add a bookmark. Returns bookmark_id or -1 on error. */
int audiobook_add_bookmark(sqlite3 *db, int book_id, int track_id,
                           int64_t position_ms, int64_t total_book_position_ms,
                           const char *label);

/* List bookmarks for a book, newest first. Returns count. */
int audiobook_list_bookmarks(sqlite3 *db, int book_id,
                            int (*cb)(const audiobook_bookmark_t *bm, void *ctx),
                            void *ctx);

/* Delete a bookmark. book_id is required (the SD store is one file per book).
 * bookmark_id is the created_at assigned at add time. Returns 0 on success. */
int audiobook_delete_bookmark(sqlite3 *db, int book_id, int bookmark_id);

/* ---- Search ------------------------------------------------------------- */

/* FTS5 search across title/author/narrator/series/chapter_titles.
 * Calls cb for each matching book. Returns count or -1. */
int audiobook_search(sqlite3 *db, const char *query,
                    int (*cb)(const audiobook_book_t *book, void *ctx),
                    void *ctx);

/* ---- book.json sidecar -------------------------------------------------- */

/* Save progress to a book.json sidecar in the book's root_path.
 * Path: <root_path>/<book_key>.json
 * Returns 0 on success, -1 on error. */
int audiobook_save_book_json(const char *root_path, const char *book_key,
                             const audiobook_progress_t *p);

/* Load progress from a book.json sidecar.
 * Returns 1 if found and parsed, 0 if not found, -1 on error. */
int audiobook_load_book_json(const char *root_path, const char *book_key,
                            audiobook_progress_t *out);

/* ---- Settings ----------------------------------------------------------- */

/* Get/set a setting value. Returns 0 on success, -1 if not found/error. */
int audiobook_get_setting(sqlite3 *db, const char *key, char *out, int out_len);
int audiobook_set_setting(sqlite3 *db, const char *key, const char *value);

/* ---- Utility ------------------------------------------------------------ */

/* Derive a book_key from a root_path: sanitize path to a flat unique key.
 * E.g. "/usr/data/mnt/sd_0/Audiobooks/David Sedaris/2008 - Calypso"
 *  -> "_usr_data_mnt_sd_0_Audiobooks_David_Sedaris_2008___Calypso" */
void audiobook_derive_book_key(const char *root_path, char *out, int out_len);

/* Natural-numeric sort comparison. Compares strings with embedded numbers
 * sorted numerically (track2 < track10). Returns -1, 0, or 1. */
int audiobook_natural_cmp(const char *a, const char *b);

/* Compute sort_title from title: lowercase, strip leading articles
 * ("the ", "a ", "an "). */
void audiobook_derive_sort_title(const char *title, char *out, int out_len);

#endif /* AUDIOBOOK_LIBRARY_H */
