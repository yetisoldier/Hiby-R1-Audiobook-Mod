#ifndef R1_AB_DB_H
#define R1_AB_DB_H

#include "config.h"

#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct audiobook_db {
    sqlite3 *db;
} audiobook_db;

typedef struct m4b_chapter m4b_chapter;

typedef struct book_row {
    int64_t book_id;
    char book_key[128];
    char title[256];
    char sort_title[256];
    char author[256];
    char narrator[256];
    char series[256];
    double series_number;
    char root_path[256];
    char cover_path[256];
    char cover_cache_path[256];
    int64_t total_duration_ms;
    int track_count;
    char fingerprint[128];
    int64_t date_added;
    int64_t date_modified;
    int64_t last_played_at;
    int completed;
    float playback_speed;
} book_row;

typedef struct track_row {
    int64_t track_id;
    int64_t book_id;
    int ordinal;
    int disc_number;
    int track_number;
    char path[512];
    char title[256];
    char sort_title[256];
    int64_t duration_ms;
    int embedded_chapters;
    int64_t file_size;
    int64_t file_mtime;
    char fingerprint[128];
} track_row;

typedef struct progress_row {
    int64_t book_id;
    int64_t track_id;
    int track_ordinal;
    int64_t position_ms;
    int64_t total_book_elapsed_ms;
    float playback_speed;
    int64_t last_played_at;
    int completed;
    int64_t completed_at;
    int64_t last_saved_at;
    int64_t protected_until_ms;
} progress_row;

typedef struct book_list {
    book_row *items;
    size_t count;
} book_list;

typedef struct track_list {
    track_row *items;
    size_t count;
} track_list;

typedef struct bookmark_row {
    int64_t bookmark_id;
    int64_t book_id;
    int64_t track_id;
    int64_t position_ms;
    int64_t total_book_position_ms;
    char label[128];
    int64_t created_at;
    int64_t updated_at;
} bookmark_row;

typedef struct bookmark_list {
    bookmark_row *items;
    size_t count;
} bookmark_list;

int db_open(audiobook_db *adb, const char *path);
void db_close(audiobook_db *adb);
int db_migrate(audiobook_db *adb);
int db_clear_library(audiobook_db *adb);
int db_upsert_book(audiobook_db *adb, const book_row *book, int64_t *out_book_id);
int db_upsert_track(audiobook_db *adb, const track_row *track, int64_t *out_track_id);
int db_set_progress(audiobook_db *adb, const progress_row *progress);
int db_set_progress_txn(audiobook_db *adb, const progress_row *progress);
int db_set_book_completion_txn(audiobook_db *adb, const progress_row *progress);
int db_replace_track_chapters(audiobook_db *adb, int64_t track_id, const m4b_chapter *chapters, size_t count);
int db_get_progress(audiobook_db *adb, int64_t book_id, progress_row *progress);
int db_query_titles(audiobook_db *adb, book_list *out);
int db_query_continue(audiobook_db *adb, book_list *out);
int db_query_finished(audiobook_db *adb, book_list *out);
int db_query_chapters(audiobook_db *adb, int64_t book_id, track_list *out);
int db_search(audiobook_db *adb, const char *needle, book_list *out);
int db_list_bookmarks(audiobook_db *adb, int64_t book_id, bookmark_list *out);
int db_add_bookmark(audiobook_db *adb, int64_t book_id, int64_t track_id, int64_t position_ms, const char *label);
int db_delete_bookmark(audiobook_db *adb, int64_t bookmark_id);
int db_mark_book_completed(audiobook_db *adb, int64_t book_id, int64_t completed_at);
void db_free_book_list(book_list *list);
void db_free_track_list(track_list *list);
void db_free_bookmark_list(bookmark_list *list);

#endif
