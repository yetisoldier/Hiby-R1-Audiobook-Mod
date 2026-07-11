#include "db.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "sqlite: %s\n", err);
            sqlite3_free(err);
        }
        return -1;
    }
    return 0;
}

int db_open(audiobook_db *adb, const char *path) {
    if (!adb || !path) return -1;
    memset(adb, 0, sizeof(*adb));
    if (sqlite3_open(path, &adb->db) != SQLITE_OK) return -1;
    sqlite3_busy_timeout(adb->db, 2000);
    return 0;
}

void db_close(audiobook_db *adb) {
    if (!adb || !adb->db) return;
    sqlite3_close(adb->db);
    adb->db = NULL;
}

int db_migrate(audiobook_db *adb) {
    if (!adb || !adb->db) return -1;
    const char *sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS schema_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS authors(author_id INTEGER PRIMARY KEY, sort_name TEXT NOT NULL, display_name TEXT NOT NULL UNIQUE);"
        "CREATE TABLE IF NOT EXISTS series(series_id INTEGER PRIMARY KEY, sort_name TEXT NOT NULL, display_name TEXT NOT NULL UNIQUE);"
        "CREATE TABLE IF NOT EXISTS books(book_id INTEGER PRIMARY KEY, book_key TEXT NOT NULL UNIQUE, title TEXT NOT NULL, sort_title TEXT NOT NULL, author_id INTEGER REFERENCES authors(author_id), narrator TEXT, series_id INTEGER REFERENCES series(series_id), series_number REAL, root_path TEXT NOT NULL, cover_path TEXT, cover_cache_path TEXT, total_duration_ms INTEGER NOT NULL DEFAULT 0, track_count INTEGER NOT NULL DEFAULT 0, fingerprint TEXT, date_added INTEGER, date_modified INTEGER, last_played_at INTEGER, completed INTEGER NOT NULL DEFAULT 0, completed_at INTEGER, playback_speed REAL NOT NULL DEFAULT 1.0);"
        "CREATE TABLE IF NOT EXISTS tracks(track_id INTEGER PRIMARY KEY, book_id INTEGER NOT NULL REFERENCES books(book_id) ON DELETE CASCADE, ordinal INTEGER NOT NULL, disc_number INTEGER NOT NULL DEFAULT 1, track_number INTEGER NOT NULL DEFAULT 0, path TEXT NOT NULL UNIQUE, title TEXT NOT NULL, sort_title TEXT NOT NULL, duration_ms INTEGER NOT NULL DEFAULT 0, embedded_chapters INTEGER NOT NULL DEFAULT 0, file_size INTEGER NOT NULL DEFAULT 0, file_mtime INTEGER NOT NULL DEFAULT 0, fingerprint TEXT, UNIQUE(book_id, ordinal));"
        "CREATE TABLE IF NOT EXISTS chapters(chapter_id INTEGER PRIMARY KEY, track_id INTEGER NOT NULL REFERENCES tracks(track_id) ON DELETE CASCADE, ordinal INTEGER NOT NULL, title TEXT, start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, bookmarkable INTEGER NOT NULL DEFAULT 1, UNIQUE(track_id, ordinal));"
        "CREATE TABLE IF NOT EXISTS progress(book_id INTEGER PRIMARY KEY REFERENCES books(book_id) ON DELETE CASCADE, track_id INTEGER REFERENCES tracks(track_id), track_ordinal INTEGER NOT NULL DEFAULT 1, position_ms INTEGER NOT NULL DEFAULT 0, total_book_elapsed_ms INTEGER NOT NULL DEFAULT 0, playback_speed REAL NOT NULL DEFAULT 1.0, last_played_at INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0, completed_at INTEGER NOT NULL DEFAULT 0, last_saved_at INTEGER NOT NULL DEFAULT 0, protected_until_ms INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS bookmarks(bookmark_id INTEGER PRIMARY KEY, book_id INTEGER NOT NULL REFERENCES books(book_id) ON DELETE CASCADE, track_id INTEGER REFERENCES tracks(track_id), position_ms INTEGER NOT NULL, total_book_position_ms INTEGER NOT NULL, label TEXT NOT NULL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS library_roots(root_id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, label TEXT, enabled INTEGER NOT NULL DEFAULT 1, last_scan_started_at INTEGER, last_scan_completed_at INTEGER, last_scan_status TEXT, last_scan_error TEXT, last_seen_mtime INTEGER, last_seen_size INTEGER);"
        "CREATE TABLE IF NOT EXISTS scan_state(scan_id INTEGER PRIMARY KEY, root_id INTEGER REFERENCES library_roots(root_id), started_at INTEGER, finished_at INTEGER, status TEXT, changed_count INTEGER NOT NULL DEFAULT 0, error TEXT);"
        "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL, scope TEXT NOT NULL DEFAULT 'global');"
        "CREATE VIRTUAL TABLE IF NOT EXISTS book_search USING fts5(book_id UNINDEXED, title, author, narrator, series, chapter_titles, tokenize='unicode61 remove_diacritics 2');"
        "CREATE INDEX IF NOT EXISTS idx_books_title_sort ON books(sort_title);"
        "CREATE INDEX IF NOT EXISTS idx_books_author ON books(author_id, sort_title);"
        "CREATE INDEX IF NOT EXISTS idx_books_series ON books(series_id, series_number, sort_title);"
        "CREATE INDEX IF NOT EXISTS idx_books_continue ON progress(completed, last_played_at DESC);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_book_ordinal ON tracks(book_id, ordinal);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_path ON tracks(path);"
        "CREATE INDEX IF NOT EXISTS idx_bookmarks_book_created ON bookmarks(book_id, created_at DESC);";
    return exec_sql(adb->db, sql);
}

int db_clear_library(audiobook_db *adb) {
    if (!adb || !adb->db) return -1;
    return exec_sql(adb->db, "BEGIN IMMEDIATE; DELETE FROM chapters; DELETE FROM tracks; DELETE FROM progress; DELETE FROM bookmarks; DELETE FROM book_search; DELETE FROM books; COMMIT;");
}

static int prepare(sqlite3 *db, sqlite3_stmt **stmt, const char *sql) {
    return sqlite3_prepare_v2(db, sql, -1, stmt, NULL) == SQLITE_OK ? 0 : -1;
}

int db_upsert_book(audiobook_db *adb, const book_row *book, int64_t *out_book_id) {
    if (!adb || !adb->db || !book) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO books(book_key,title,sort_title,root_path,cover_path,cover_cache_path,total_duration_ms,track_count,fingerprint,date_added,date_modified,last_played_at,completed,playback_speed)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(book_key) DO UPDATE SET title=excluded.title, sort_title=excluded.sort_title, root_path=excluded.root_path, cover_path=excluded.cover_path, cover_cache_path=excluded.cover_cache_path, total_duration_ms=excluded.total_duration_ms, track_count=excluded.track_count, fingerprint=excluded.fingerprint, date_modified=excluded.date_modified, playback_speed=excluded.playback_speed;";
    if (prepare(adb->db, &st, sql) != 0) return -1;
    sqlite3_bind_text(st, 1, book->book_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, book->title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, book->sort_title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, book->root_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, book->cover_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, book->cover_cache_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 7, book->total_duration_ms);
    sqlite3_bind_int(st, 8, book->track_count);
    sqlite3_bind_text(st, 9, book->fingerprint, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 10, book->date_added);
    sqlite3_bind_int64(st, 11, book->date_modified);
    sqlite3_bind_int64(st, 12, book->last_played_at);
    sqlite3_bind_int(st, 13, book->completed);
    sqlite3_bind_double(st, 14, book->playback_speed);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    if (out_book_id) {
        sqlite3_stmt *q = NULL;
        if (prepare(adb->db, &q, "SELECT book_id FROM books WHERE book_key=?") != 0) return -1;
        sqlite3_bind_text(q, 1, book->book_key, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(q) == SQLITE_ROW) *out_book_id = sqlite3_column_int64(q, 0);
        sqlite3_finalize(q);
    }
    return 0;
}

int db_upsert_track(audiobook_db *adb, const track_row *track, int64_t *out_track_id) {
    if (!adb || !adb->db || !track) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO tracks(book_id,ordinal,disc_number,track_number,path,title,sort_title,duration_ms,embedded_chapters,file_size,file_mtime,fingerprint)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET book_id=excluded.book_id, ordinal=excluded.ordinal, disc_number=excluded.disc_number, track_number=excluded.track_number, title=excluded.title, sort_title=excluded.sort_title, duration_ms=excluded.duration_ms, embedded_chapters=excluded.embedded_chapters, file_size=excluded.file_size, file_mtime=excluded.file_mtime, fingerprint=excluded.fingerprint"
        " RETURNING track_id;";
    if (prepare(adb->db, &st, sql) != 0) return -1;
    sqlite3_bind_int64(st, 1, track->book_id);
    sqlite3_bind_int(st, 2, track->ordinal);
    sqlite3_bind_int(st, 3, track->disc_number);
    sqlite3_bind_int(st, 4, track->track_number);
    sqlite3_bind_text(st, 5, track->path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, track->title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, track->sort_title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, track->duration_ms);
    sqlite3_bind_int(st, 9, track->embedded_chapters);
    sqlite3_bind_int64(st, 10, track->file_size);
    sqlite3_bind_int64(st, 11, track->file_mtime);
    sqlite3_bind_text(st, 12, track->fingerprint, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        sqlite3_finalize(st);
        return -1;
    }
    if (out_track_id && rc == SQLITE_ROW) {
        *out_track_id = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    return 0;
}

int db_set_progress(audiobook_db *adb, const progress_row *progress) {
    if (!adb || !adb->db || !progress) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO progress(book_id,track_id,track_ordinal,position_ms,total_book_elapsed_ms,playback_speed,last_played_at,completed,completed_at,last_saved_at,protected_until_ms)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(book_id) DO UPDATE SET track_id=excluded.track_id, track_ordinal=excluded.track_ordinal, position_ms=excluded.position_ms, total_book_elapsed_ms=excluded.total_book_elapsed_ms, playback_speed=excluded.playback_speed, last_played_at=excluded.last_played_at, completed=excluded.completed, completed_at=excluded.completed_at, last_saved_at=excluded.last_saved_at, protected_until_ms=excluded.protected_until_ms;";
    if (prepare(adb->db, &st, sql) != 0) return -1;
    sqlite3_bind_int64(st, 1, progress->book_id);
    sqlite3_bind_int64(st, 2, progress->track_id);
    sqlite3_bind_int(st, 3, progress->track_ordinal);
    sqlite3_bind_int64(st, 4, progress->position_ms);
    sqlite3_bind_int64(st, 5, progress->total_book_elapsed_ms);
    sqlite3_bind_double(st, 6, progress->playback_speed);
    sqlite3_bind_int64(st, 7, progress->last_played_at);
    sqlite3_bind_int(st, 8, progress->completed);
    sqlite3_bind_int64(st, 9, progress->completed_at);
    sqlite3_bind_int64(st, 10, progress->last_saved_at);
    sqlite3_bind_int64(st, 11, progress->protected_until_ms);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int db_set_progress_txn(audiobook_db *adb, const progress_row *progress) {
    if (!adb || !adb->db || !progress) return -1;
    if (exec_sql(adb->db, "BEGIN IMMEDIATE;") != 0) return -1;
    if (db_set_progress(adb, progress) != 0) {
        exec_sql(adb->db, "ROLLBACK;");
        return -1;
    }
    if (exec_sql(adb->db, "COMMIT;") != 0) {
        exec_sql(adb->db, "ROLLBACK;");
        return -1;
    }
    return 0;
}

int db_set_book_completion_txn(audiobook_db *adb, int64_t book_id, int completed, int64_t completed_at, int64_t last_played_at) {
    if (!adb || !adb->db) return -1;
    if (exec_sql(adb->db, "BEGIN IMMEDIATE;") != 0) return -1;

    sqlite3_stmt *progress = NULL;
    sqlite3_stmt *book = NULL;
    int ok = 0;

    if (prepare(adb->db, &progress,
                "UPDATE progress SET completed=?, completed_at=?, last_played_at=?, last_saved_at=?, protected_until_ms=0 WHERE book_id=?") != 0) {
        goto done;
    }
    sqlite3_bind_int(progress, 1, completed ? 1 : 0);
    sqlite3_bind_int64(progress, 2, completed_at);
    sqlite3_bind_int64(progress, 3, last_played_at);
    sqlite3_bind_int64(progress, 4, last_played_at);
    sqlite3_bind_int64(progress, 5, book_id);
    if (sqlite3_step(progress) != SQLITE_DONE) goto done;

    if (prepare(adb->db, &book,
                "UPDATE books SET completed=?, completed_at=?, last_played_at=? WHERE book_id=?") != 0) {
        goto done;
    }
    sqlite3_bind_int(book, 1, completed ? 1 : 0);
    sqlite3_bind_int64(book, 2, completed_at);
    sqlite3_bind_int64(book, 3, last_played_at);
    sqlite3_bind_int64(book, 4, book_id);
    if (sqlite3_step(book) != SQLITE_DONE) goto done;

    ok = 1;

done:
    if (progress) sqlite3_finalize(progress);
    if (book) sqlite3_finalize(book);
    if (!ok) {
        exec_sql(adb->db, "ROLLBACK;");
        return -1;
    }
    if (exec_sql(adb->db, "COMMIT;") != 0) {
        exec_sql(adb->db, "ROLLBACK;");
        return -1;
    }
    return 0;
}

int db_get_progress(audiobook_db *adb, int64_t book_id, progress_row *progress) {
    if (!adb || !adb->db || !progress) return -1;
    sqlite3_stmt *st = NULL;
    if (prepare(adb->db, &st, "SELECT book_id,track_id,track_ordinal,position_ms,total_book_elapsed_ms,playback_speed,last_played_at,completed,completed_at,last_saved_at,protected_until_ms FROM progress WHERE book_id=?") != 0) return -1;
    sqlite3_bind_int64(st, 1, book_id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        memset(progress, 0, sizeof(*progress));
        progress->book_id = sqlite3_column_int64(st, 0);
        progress->track_id = sqlite3_column_int64(st, 1);
        progress->track_ordinal = sqlite3_column_int(st, 2);
        progress->position_ms = sqlite3_column_int64(st, 3);
        progress->total_book_elapsed_ms = sqlite3_column_int64(st, 4);
        progress->playback_speed = (float)sqlite3_column_double(st, 5);
        progress->last_played_at = sqlite3_column_int64(st, 6);
        progress->completed = sqlite3_column_int(st, 7);
        progress->completed_at = sqlite3_column_int64(st, 8);
        progress->last_saved_at = sqlite3_column_int64(st, 9);
        progress->protected_until_ms = sqlite3_column_int64(st, 10);
        sqlite3_finalize(st);
        return 0;
    }
    sqlite3_finalize(st);
    return -1;
}

static int load_books(sqlite3 *db, const char *sql, book_list *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st = NULL;
    if (prepare(db, &st, sql) != 0) return -1;
    size_t cap = 16;
    out->items = ab_xcalloc(cap, sizeof(*out->items));
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out->count == cap) {
            cap *= 2;
            out->items = ab_xrealloc(out->items, cap * sizeof(*out->items));
            memset(out->items + out->count, 0, (cap - out->count) * sizeof(*out->items));
        }
        book_row *b = &out->items[out->count++];
        memset(b, 0, sizeof(*b));
        b->book_id = sqlite3_column_int64(st, 0);
        snprintf(b->book_key, sizeof(b->book_key), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(b->title, sizeof(b->title), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(b->sort_title, sizeof(b->sort_title), "%s", (const char *)sqlite3_column_text(st, 3));
        const unsigned char *author = sqlite3_column_text(st, 4);
        if (author) snprintf(b->author, sizeof(b->author), "%s", (const char *)author);
        const unsigned char *narrator = sqlite3_column_text(st, 5);
        if (narrator) snprintf(b->narrator, sizeof(b->narrator), "%s", (const char *)narrator);
        const unsigned char *series = sqlite3_column_text(st, 6);
        if (series) snprintf(b->series, sizeof(b->series), "%s", (const char *)series);
        b->series_number = sqlite3_column_double(st, 7);
        const unsigned char *root = sqlite3_column_text(st, 8);
        if (root) snprintf(b->root_path, sizeof(b->root_path), "%s", (const char *)root);
        const unsigned char *cover = sqlite3_column_text(st, 9);
        if (cover) snprintf(b->cover_path, sizeof(b->cover_path), "%s", (const char *)cover);
        const unsigned char *cache = sqlite3_column_text(st, 10);
        if (cache) snprintf(b->cover_cache_path, sizeof(b->cover_cache_path), "%s", (const char *)cache);
        b->total_duration_ms = sqlite3_column_int64(st, 11);
        b->track_count = sqlite3_column_int(st, 12);
        const unsigned char *finger = sqlite3_column_text(st, 13);
        if (finger) snprintf(b->fingerprint, sizeof(b->fingerprint), "%s", (const char *)finger);
        b->date_added = sqlite3_column_int64(st, 14);
        b->date_modified = sqlite3_column_int64(st, 15);
        b->last_played_at = sqlite3_column_int64(st, 16);
        b->completed = sqlite3_column_int(st, 17);
        b->playback_speed = 1.0f;
    }
    sqlite3_finalize(st);
    return 0;
}

int db_query_titles(audiobook_db *adb, book_list *out) {
    return adb && adb->db ? load_books(adb->db,
        "SELECT b.book_id,b.book_key,b.title,b.sort_title,COALESCE(a.display_name,''),COALESCE(b.narrator,''),COALESCE(s.display_name,''),COALESCE(b.series_number,0),b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,''),b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,''),COALESCE(b.date_added,0),COALESCE(b.date_modified,0),COALESCE(b.last_played_at,0),b.completed FROM books b LEFT JOIN authors a ON a.author_id=b.author_id LEFT JOIN series s ON s.series_id=b.series_id ORDER BY b.sort_title COLLATE NOCASE,b.book_id",
        out) : -1;
}

int db_query_continue(audiobook_db *adb, book_list *out) {
    return adb && adb->db ? load_books(adb->db,
        "SELECT b.book_id,b.book_key,b.title,b.sort_title,COALESCE(a.display_name,''),COALESCE(b.narrator,''),COALESCE(s.display_name,''),COALESCE(b.series_number,0),b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,''),b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,''),COALESCE(b.date_added,0),COALESCE(b.date_modified,0),COALESCE(b.last_played_at,0),b.completed FROM books b JOIN progress p ON p.book_id=b.book_id LEFT JOIN authors a ON a.author_id=b.author_id LEFT JOIN series s ON s.series_id=b.series_id WHERE p.completed=0 ORDER BY p.last_played_at DESC,b.sort_title COLLATE NOCASE",
        out) : -1;
}

int db_query_finished(audiobook_db *adb, book_list *out) {
    return adb && adb->db ? load_books(adb->db,
        "SELECT b.book_id,b.book_key,b.title,b.sort_title,COALESCE(a.display_name,''),COALESCE(b.narrator,''),COALESCE(s.display_name,''),COALESCE(b.series_number,0),b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,''),b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,''),COALESCE(b.date_added,0),COALESCE(b.date_modified,0),COALESCE(b.last_played_at,0),b.completed FROM books b JOIN progress p ON p.book_id=b.book_id LEFT JOIN authors a ON a.author_id=b.author_id LEFT JOIN series s ON s.series_id=b.series_id WHERE b.completed=1 OR p.completed=1 ORDER BY COALESCE(b.completed_at,p.completed_at) DESC,b.sort_title COLLATE NOCASE",
        out) : -1;
}

int db_query_chapters(audiobook_db *adb, int64_t book_id, track_list *out) {
    if (!adb || !adb->db || !out) return -1;
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st = NULL;
    const char *chapter_sql =
        "SELECT c.chapter_id,t.book_id,c.ordinal,0,c.ordinal,COALESCE(c.title,''),COALESCE(c.title,''),COALESCE(c.title,''),"
        "COALESCE(c.end_ms - c.start_ms, 0),1,0,0,'' "
        "FROM chapters c JOIN tracks t ON t.track_id=c.track_id WHERE t.book_id=? "
        "ORDER BY t.disc_number,t.track_number,c.ordinal,c.title COLLATE NOCASE";
    if (prepare(adb->db, &st, chapter_sql) != 0) return -1;
    sqlite3_bind_int64(st, 1, book_id);
    size_t cap = 16;
    out->items = ab_xcalloc(cap, sizeof(*out->items));
    bool found_chapters = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        found_chapters = true;
        if (out->count == cap) {
            cap *= 2;
            out->items = ab_xrealloc(out->items, cap * sizeof(*out->items));
            memset(out->items + out->count, 0, (cap - out->count) * sizeof(*out->items));
        }
        track_row *t = &out->items[out->count++];
        memset(t, 0, sizeof(*t));
        t->track_id = sqlite3_column_int64(st, 0);
        t->book_id = sqlite3_column_int64(st, 1);
        t->ordinal = sqlite3_column_int(st, 2);
        t->disc_number = sqlite3_column_int(st, 3);
        t->track_number = sqlite3_column_int(st, 4);
        snprintf(t->path, sizeof(t->path), "%s", sqlite3_column_text(st, 5) ? (const char *)sqlite3_column_text(st, 5) : "");
        snprintf(t->title, sizeof(t->title), "%s", sqlite3_column_text(st, 6) ? (const char *)sqlite3_column_text(st, 6) : "");
        snprintf(t->sort_title, sizeof(t->sort_title), "%s", sqlite3_column_text(st, 7) ? (const char *)sqlite3_column_text(st, 7) : "");
        t->duration_ms = sqlite3_column_int64(st, 8);
        t->embedded_chapters = sqlite3_column_int(st, 9);
        t->file_size = sqlite3_column_int64(st, 10);
        t->file_mtime = sqlite3_column_int64(st, 11);
        snprintf(t->fingerprint, sizeof(t->fingerprint), "%s", sqlite3_column_text(st, 12) ? (const char *)sqlite3_column_text(st, 12) : "");
    }
    sqlite3_finalize(st);
    if (found_chapters) return 0;
    db_free_track_list(out);

    if (prepare(adb->db,
                &st,
                "SELECT track_id,book_id,ordinal,disc_number,track_number,path,title,sort_title,duration_ms,embedded_chapters,file_size,file_mtime,COALESCE(fingerprint,'') FROM tracks WHERE book_id=? ORDER BY disc_number,track_number,ordinal,sort_title COLLATE NOCASE") != 0) {
        return -1;
    }
    sqlite3_bind_int64(st, 1, book_id);
    cap = 16;
    out->items = ab_xcalloc(cap, sizeof(*out->items));
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out->count == cap) {
            cap *= 2;
            out->items = ab_xrealloc(out->items, cap * sizeof(*out->items));
            memset(out->items + out->count, 0, (cap - out->count) * sizeof(*out->items));
        }
        track_row *t = &out->items[out->count++];
        memset(t, 0, sizeof(*t));
        t->track_id = sqlite3_column_int64(st, 0);
        t->book_id = sqlite3_column_int64(st, 1);
        t->ordinal = sqlite3_column_int(st, 2);
        t->disc_number = sqlite3_column_int(st, 3);
        t->track_number = sqlite3_column_int(st, 4);
        snprintf(t->path, sizeof(t->path), "%s", sqlite3_column_text(st, 5) ? (const char *)sqlite3_column_text(st, 5) : "");
        snprintf(t->title, sizeof(t->title), "%s", sqlite3_column_text(st, 6) ? (const char *)sqlite3_column_text(st, 6) : "");
        snprintf(t->sort_title, sizeof(t->sort_title), "%s", sqlite3_column_text(st, 7) ? (const char *)sqlite3_column_text(st, 7) : "");
        t->duration_ms = sqlite3_column_int64(st, 8);
        t->embedded_chapters = sqlite3_column_int(st, 9);
        t->file_size = sqlite3_column_int64(st, 10);
        t->file_mtime = sqlite3_column_int64(st, 11);
        snprintf(t->fingerprint, sizeof(t->fingerprint), "%s", sqlite3_column_text(st, 12) ? (const char *)sqlite3_column_text(st, 12) : "");
    }
    sqlite3_finalize(st);
    return 0;
}

int db_search(audiobook_db *adb, const char *needle, book_list *out) {
    if (!adb || !adb->db || !needle || !out) return -1;
    sqlite3_stmt *st = NULL;
    if (prepare(adb->db, &st,
                "SELECT b.book_id,b.book_key,b.title,b.sort_title,COALESCE(a.display_name,''),COALESCE(b.narrator,''),COALESCE(s.display_name,''),COALESCE(b.series_number,0),b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,''),b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,''),COALESCE(b.date_added,0),COALESCE(b.date_modified,0),COALESCE(b.last_played_at,0),b.completed FROM book_search x JOIN books b ON b.book_id=x.book_id LEFT JOIN authors a ON a.author_id=b.author_id LEFT JOIN series s ON s.series_id=b.series_id WHERE x MATCH ? ORDER BY b.sort_title COLLATE NOCASE") != 0) {
        return -1;
    }
    sqlite3_bind_text(st, 1, needle, -1, SQLITE_TRANSIENT);
    size_t cap = 16;
    memset(out, 0, sizeof(*out));
    out->items = ab_xcalloc(cap, sizeof(*out->items));
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out->count == cap) {
            cap *= 2;
            out->items = ab_xrealloc(out->items, cap * sizeof(*out->items));
            memset(out->items + out->count, 0, (cap - out->count) * sizeof(*out->items));
        }
        book_row *b = &out->items[out->count++];
        memset(b, 0, sizeof(*b));
        b->book_id = sqlite3_column_int64(st, 0);
        snprintf(b->book_key, sizeof(b->book_key), "%s", sqlite3_column_text(st, 1) ? (const char *)sqlite3_column_text(st, 1) : "");
        snprintf(b->title, sizeof(b->title), "%s", sqlite3_column_text(st, 2) ? (const char *)sqlite3_column_text(st, 2) : "");
        snprintf(b->sort_title, sizeof(b->sort_title), "%s", sqlite3_column_text(st, 3) ? (const char *)sqlite3_column_text(st, 3) : "");
        snprintf(b->author, sizeof(b->author), "%s", sqlite3_column_text(st, 4) ? (const char *)sqlite3_column_text(st, 4) : "");
        snprintf(b->narrator, sizeof(b->narrator), "%s", sqlite3_column_text(st, 5) ? (const char *)sqlite3_column_text(st, 5) : "");
        snprintf(b->series, sizeof(b->series), "%s", sqlite3_column_text(st, 6) ? (const char *)sqlite3_column_text(st, 6) : "");
        b->series_number = sqlite3_column_double(st, 7);
        snprintf(b->root_path, sizeof(b->root_path), "%s", sqlite3_column_text(st, 8) ? (const char *)sqlite3_column_text(st, 8) : "");
    }
    sqlite3_finalize(st);
    return 0;
}

void db_free_book_list(book_list *list) {
    if (!list) return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

void db_free_track_list(track_list *list) {
    if (!list) return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

int db_list_bookmarks(audiobook_db *adb, int64_t book_id, bookmark_list *out) {
    if (!adb || !adb->db || !out) return -1;
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st = NULL;
    if (prepare(adb->db,
                &st,
                "SELECT bookmark_id,book_id,COALESCE(track_id,0),position_ms,total_book_position_ms,label,created_at,updated_at FROM bookmarks WHERE book_id=? ORDER BY created_at DESC") != 0) {
        return -1;
    }
    sqlite3_bind_int64(st, 1, book_id);
    size_t cap = 8;
    out->items = ab_xcalloc(cap, sizeof(*out->items));
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out->count == cap) {
            cap *= 2;
            out->items = ab_xrealloc(out->items, cap * sizeof(*out->items));
            memset(out->items + out->count, 0, (cap - out->count) * sizeof(*out->items));
        }
        bookmark_row *b = &out->items[out->count++];
        memset(b, 0, sizeof(*b));
        b->bookmark_id = sqlite3_column_int64(st, 0);
        b->book_id = sqlite3_column_int64(st, 1);
        b->track_id = sqlite3_column_int64(st, 2);
        b->position_ms = sqlite3_column_int64(st, 3);
        b->total_book_position_ms = sqlite3_column_int64(st, 4);
        snprintf(b->label, sizeof(b->label), "%s", sqlite3_column_text(st, 5) ? (const char *)sqlite3_column_text(st, 5) : "");
        b->created_at = sqlite3_column_int64(st, 6);
        b->updated_at = sqlite3_column_int64(st, 7);
    }
    sqlite3_finalize(st);
    return 0;
}

int db_add_bookmark(audiobook_db *adb, int64_t book_id, int64_t track_id, int64_t position_ms, const char *label) {
    if (!adb || !adb->db) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO bookmarks(book_id,track_id,position_ms,total_book_position_ms,label,created_at,updated_at)"
        " VALUES(?,?,?,?,?,?,?)";
    if (prepare(adb->db, &st, sql) != 0) return -1;
    int64_t now = (int64_t)ab_now_ms();
    sqlite3_bind_int64(st, 1, book_id);
    sqlite3_bind_int64(st, 2, track_id);
    sqlite3_bind_int64(st, 3, position_ms);
    sqlite3_bind_int64(st, 4, position_ms);
    sqlite3_bind_text(st, 5, label ? label : "Bookmark", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, now);
    sqlite3_bind_int64(st, 7, now);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int db_delete_bookmark(audiobook_db *adb, int64_t bookmark_id) {
    if (!adb || !adb->db) return -1;
    sqlite3_stmt *st = NULL;
    if (prepare(adb->db, &st, "DELETE FROM bookmarks WHERE bookmark_id=?") != 0) return -1;
    sqlite3_bind_int64(st, 1, bookmark_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

void db_free_bookmark_list(bookmark_list *list) {
    if (!list) return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

int db_mark_book_completed(audiobook_db *adb, int64_t book_id, int64_t completed_at) {
    return db_set_book_completion_txn(adb, book_id, 1, completed_at, completed_at);
}
