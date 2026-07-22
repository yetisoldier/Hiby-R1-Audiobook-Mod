/* library.c — audiobook library database layer.
 *
 * SQLite-based library management matching the orphaned r1_audiobook_app
 * schema. All SQL is recovered from the binary's string table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include "library.h"
#include "bookmark_sd.h"

/* ---- Schema bootstrap SQL (from orphaned binary) ----------------------- */

static const char *SCHEMA_SQL =
"PRAGMA journal_mode=WAL;"
"PRAGMA foreign_keys=ON;"
"CREATE TABLE IF NOT EXISTS schema_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
"CREATE TABLE IF NOT EXISTS authors(author_id INTEGER PRIMARY KEY, sort_name TEXT NOT NULL, display_name TEXT NOT NULL UNIQUE);"
"CREATE TABLE IF NOT EXISTS series(series_id INTEGER PRIMARY KEY, sort_name TEXT NOT NULL, display_name TEXT NOT NULL UNIQUE);"
"CREATE TABLE IF NOT EXISTS books("
  "book_id INTEGER PRIMARY KEY,"
  "book_key TEXT NOT NULL UNIQUE,"
  "title TEXT NOT NULL,"
  "sort_title TEXT NOT NULL,"
  "author_id INTEGER REFERENCES authors(author_id),"
  "narrator TEXT,"
  "series_id INTEGER REFERENCES series(series_id),"
  "series_number REAL,"
  "root_path TEXT NOT NULL,"
  "cover_path TEXT,"
  "cover_cache_path TEXT,"
  "total_duration_ms INTEGER NOT NULL DEFAULT 0,"
  "track_count INTEGER NOT NULL DEFAULT 0,"
  "fingerprint TEXT,"
  "date_added INTEGER,"
  "date_modified INTEGER,"
  "last_played_at INTEGER,"
  "completed INTEGER NOT NULL DEFAULT 0,"
  "completed_at INTEGER,"
  "playback_speed REAL NOT NULL DEFAULT 1.0"
");"
"CREATE TABLE IF NOT EXISTS tracks("
  "track_id INTEGER PRIMARY KEY,"
  "book_id INTEGER NOT NULL REFERENCES books(book_id) ON DELETE CASCADE,"
  "ordinal INTEGER NOT NULL,"
  "disc_number INTEGER NOT NULL DEFAULT 1,"
  "track_number INTEGER NOT NULL DEFAULT 0,"
  "path TEXT NOT NULL UNIQUE,"
  "title TEXT NOT NULL,"
  "sort_title TEXT NOT NULL,"
  "duration_ms INTEGER NOT NULL DEFAULT 0,"
  "embedded_chapters INTEGER NOT NULL DEFAULT 0,"
  "file_size INTEGER NOT NULL DEFAULT 0,"
  "file_mtime INTEGER NOT NULL DEFAULT 0,"
  "fingerprint TEXT,"
  "UNIQUE(book_id, ordinal)"
");"
"CREATE TABLE IF NOT EXISTS chapters("
  "chapter_id INTEGER PRIMARY KEY,"
  "track_id INTEGER NOT NULL REFERENCES tracks(track_id) ON DELETE CASCADE,"
  "ordinal INTEGER NOT NULL,"
  "title TEXT,"
  "start_ms INTEGER NOT NULL,"
  "end_ms INTEGER NOT NULL,"
  "bookmarkable INTEGER NOT NULL DEFAULT 1,"
  "UNIQUE(track_id, ordinal)"
");"
"CREATE TABLE IF NOT EXISTS progress("
  "book_id INTEGER PRIMARY KEY REFERENCES books(book_id) ON DELETE CASCADE,"
  "track_id INTEGER REFERENCES tracks(track_id),"
  "track_ordinal INTEGER NOT NULL DEFAULT 1,"
  "position_ms INTEGER NOT NULL DEFAULT 0,"
  "total_book_elapsed_ms INTEGER NOT NULL DEFAULT 0,"
  "playback_speed REAL NOT NULL DEFAULT 1.0,"
  "last_played_at INTEGER NOT NULL DEFAULT 0,"
  "completed INTEGER NOT NULL DEFAULT 0,"
  "completed_at INTEGER NOT NULL DEFAULT 0,"
  "last_saved_at INTEGER NOT NULL DEFAULT 0,"
  "protected_until_ms INTEGER NOT NULL DEFAULT 0"
");"
"CREATE TABLE IF NOT EXISTS bookmarks("
  "bookmark_id INTEGER PRIMARY KEY,"
  "book_id INTEGER NOT NULL REFERENCES books(book_id) ON DELETE CASCADE,"
  "track_id INTEGER REFERENCES tracks(track_id),"
  "position_ms INTEGER NOT NULL,"
  "total_book_position_ms INTEGER NOT NULL,"
  "label TEXT NOT NULL,"
  "created_at INTEGER NOT NULL,"
  "updated_at INTEGER NOT NULL"
");"
"CREATE TABLE IF NOT EXISTS library_roots("
  "root_id INTEGER PRIMARY KEY,"
  "path TEXT NOT NULL UNIQUE,"
  "label TEXT,"
  "enabled INTEGER NOT NULL DEFAULT 1,"
  "last_scan_started_at INTEGER,"
  "last_scan_completed_at INTEGER,"
  "last_scan_status TEXT,"
  "last_scan_error TEXT,"
  "last_seen_mtime INTEGER,"
  "last_seen_size INTEGER"
");"
"CREATE TABLE IF NOT EXISTS scan_state("
  "scan_id INTEGER PRIMARY KEY,"
  "root_id INTEGER REFERENCES library_roots(root_id),"
  "started_at INTEGER,"
  "finished_at INTEGER,"
  "status TEXT,"
  "changed_count INTEGER NOT NULL DEFAULT 0,"
  "error TEXT"
");"
"CREATE TABLE IF NOT EXISTS settings("
  "key TEXT PRIMARY KEY,"
  "value TEXT NOT NULL,"
  "scope TEXT NOT NULL DEFAULT 'global'"
");"
"CREATE VIRTUAL TABLE IF NOT EXISTS book_search USING fts5("
  "book_id UNINDEXED, title, author, narrator, series, chapter_titles,"
  "tokenize='unicode61 remove_diacritics 2'"
");"
"CREATE INDEX IF NOT EXISTS idx_books_title_sort ON books(sort_title);"
"CREATE INDEX IF NOT EXISTS idx_books_author ON books(author_id, sort_title);"
"CREATE INDEX IF NOT EXISTS idx_books_series ON books(series_id, series_number, sort_title);"
"CREATE INDEX IF NOT EXISTS idx_books_continue ON progress(completed, last_played_at DESC);"
"CREATE INDEX IF NOT EXISTS idx_tracks_book_ordinal ON tracks(book_id, ordinal);"
"CREATE INDEX IF NOT EXISTS idx_tracks_path ON tracks(path);"
"CREATE INDEX IF NOT EXISTS idx_bookmarks_book_created ON bookmarks(book_id, created_at DESC);";

/* ---- SQL statements (from orphaned binary) ---------------------------- */

#define SQL_GET_BOOK_BY_KEY \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b LEFT JOIN authors a ON a.author_id=b.author_id " \
    "LEFT JOIN series s ON s.series_id=b.series_id WHERE b.book_key=?"

#define SQL_GET_BOOK_BY_ID \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b LEFT JOIN authors a ON a.author_id=b.author_id " \
    "LEFT JOIN series s ON s.series_id=b.series_id WHERE b.book_id=?"

#define SQL_LIST_BOOKS \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b LEFT JOIN authors a ON a.author_id=b.author_id " \
    "LEFT JOIN series s ON s.series_id=b.series_id " \
    "ORDER BY b.sort_title COLLATE NOCASE"

#define SQL_LIST_CONTINUE \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b INNER JOIN progress p ON p.book_id=b.book_id " \
    "LEFT JOIN authors a ON a.author_id=b.author_id " \
    "LEFT JOIN series s ON s.series_id=b.series_id " \
    "WHERE p.completed=0 AND p.last_played_at>0 " \
    "ORDER BY p.last_played_at DESC"

#define SQL_LIST_FINISHED \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b LEFT JOIN authors a ON a.author_id=b.author_id " \
    "LEFT JOIN series s ON s.series_id=b.series_id " \
    "WHERE b.completed=1 ORDER BY b.completed_at DESC"

#define SQL_LIST_AUTHORS \
    "SELECT DISTINCT COALESCE(a.display_name,'') FROM books b " \
    "LEFT JOIN authors a ON a.author_id=b.author_id " \
    "WHERE a.display_name IS NOT NULL AND a.display_name<>'' " \
    "ORDER BY a.display_name COLLATE NOCASE"

#define SQL_LIST_BOOKS_BY_AUTHOR \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b INNER JOIN authors a ON a.author_id=b.author_id " \
    "LEFT JOIN series s ON s.series_id=b.series_id " \
    "WHERE a.display_name=? " \
    "ORDER BY b.series_number, b.sort_title COLLATE NOCASE"

#define SQL_LIST_BOOKS_BY_SERIES \
    "SELECT b.book_id,b.book_key,b.title,b.sort_title," \
    "COALESCE(a.display_name,''),COALESCE(b.narrator,'')," \
    "COALESCE(s.display_name,''),COALESCE(b.series_number,0)," \
    "b.root_path,COALESCE(b.cover_path,''),COALESCE(b.cover_cache_path,'')," \
    "b.total_duration_ms,b.track_count,COALESCE(b.fingerprint,'')," \
    "COALESCE(b.date_added,0),COALESCE(b.date_modified,0)," \
    "COALESCE(b.last_played_at,0),b.completed,COALESCE(b.completed_at,0)," \
    "b.playback_speed " \
    "FROM books b INNER JOIN series s ON s.series_id=b.series_id " \
    "LEFT JOIN authors a ON a.author_id=b.author_id " \
    "WHERE s.display_name=? " \
    "ORDER BY b.series_number, b.sort_title COLLATE NOCASE"

#define SQL_LIST_SERIES \
    "SELECT DISTINCT COALESCE(s.display_name,'') FROM books b " \
    "LEFT JOIN series s ON s.series_id=b.series_id " \
    "WHERE s.display_name IS NOT NULL AND s.display_name<>'' " \
    "ORDER BY s.display_name COLLATE NOCASE"

#define SQL_LIST_FOLDERS \
    "SELECT DISTINCT COALESCE(root_path,'') FROM books " \
    "WHERE root_path IS NOT NULL AND root_path<>'' " \
    "ORDER BY root_path COLLATE NOCASE"

#define SQL_GET_TRACKS \
    "SELECT track_id,book_id,ordinal,disc_number,track_number,path,title," \
    "sort_title,duration_ms,embedded_chapters,file_size,file_mtime," \
    "COALESCE(fingerprint,'') FROM tracks WHERE book_id=? " \
    "ORDER BY disc_number,track_number,ordinal," \
    "sort_title COLLATE NOCASE"

#define SQL_GET_CHAPTERS \
    "SELECT c.chapter_id,c.track_id,c.ordinal,COALESCE(c.title,'')," \
    "c.start_ms,c.end_ms,c.bookmarkable " \
    "FROM chapters c JOIN tracks t ON t.track_id=c.track_id " \
    "WHERE t.book_id=? ORDER BY t.disc_number,t.track_number,c.ordinal"

#define SQL_GET_PROGRESS \
    "SELECT book_id,track_id,track_ordinal,position_ms," \
    "total_book_elapsed_ms,playback_speed,last_played_at,completed," \
    "completed_at,last_saved_at,protected_until_ms " \
    "FROM progress WHERE book_id=?"

#define SQL_SAVE_PROGRESS \
    "INSERT INTO progress(book_id,track_id,track_ordinal,position_ms," \
    "total_book_elapsed_ms,playback_speed,last_played_at,completed," \
    "completed_at,last_saved_at,protected_until_ms) " \
    "VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(book_id) DO UPDATE SET " \
    "track_id=excluded.track_id,track_ordinal=excluded.track_ordinal," \
    "position_ms=excluded.position_ms," \
    "total_book_elapsed_ms=excluded.total_book_elapsed_ms," \
    "playback_speed=excluded.playback_speed," \
    "last_played_at=excluded.last_played_at,completed=excluded.completed," \
    "completed_at=excluded.completed_at,last_saved_at=excluded.last_saved_at," \
    "protected_until_ms=excluded.protected_until_ms"

#define SQL_ADD_BOOKMARK \
    "INSERT INTO bookmarks(book_id,track_id,position_ms," \
    "total_book_position_ms,label,created_at,updated_at) " \
    "VALUES(?,?,?,?,?,?,?)"

#define SQL_LIST_BOOKMARKS \
    "SELECT bookmark_id,book_id,track_id,position_ms," \
    "total_book_position_ms,label,created_at,updated_at " \
    "FROM bookmarks WHERE book_id=? ORDER BY created_at DESC"

#define SQL_DELETE_BOOKMARK \
    "DELETE FROM bookmarks WHERE bookmark_id=?"

#define SQL_GET_BOOK_BY_KEY_ONLY \
    "SELECT book_id FROM books WHERE book_key=?"

/* ---- Helpers ------------------------------------------------------------ */

static void safe_strcpy(char *dst, int dst_len, const char *src) {
    if (!src || !dst || dst_len <= 0) return;
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static int mkdir_p(const char *path) {
    char tmp[512];
    int len = snprintf(tmp, sizeof(tmp), "%s", path);
    if (len <= 0 || len >= (int)sizeof(tmp)) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static int fill_book_from_stmt(sqlite3_stmt *stmt, audiobook_book_t *b) {
    memset(b, 0, sizeof(*b));
    b->book_id = sqlite3_column_int(stmt, 0);
    safe_strcpy(b->book_key, sizeof(b->book_key),
                (const char *)sqlite3_column_text(stmt, 1));
    safe_strcpy(b->title, sizeof(b->title),
                (const char *)sqlite3_column_text(stmt, 2));
    safe_strcpy(b->sort_title, sizeof(b->sort_title),
                (const char *)sqlite3_column_text(stmt, 3));
    safe_strcpy(b->author, sizeof(b->author),
                (const char *)sqlite3_column_text(stmt, 4));
    safe_strcpy(b->narrator, sizeof(b->narrator),
                (const char *)sqlite3_column_text(stmt, 5));
    safe_strcpy(b->series, sizeof(b->series),
                (const char *)sqlite3_column_text(stmt, 6));
    b->series_number = sqlite3_column_double(stmt, 7);
    safe_strcpy(b->root_path, sizeof(b->root_path),
                (const char *)sqlite3_column_text(stmt, 8));
    safe_strcpy(b->cover_path, sizeof(b->cover_path),
                (const char *)sqlite3_column_text(stmt, 9));
    safe_strcpy(b->cover_cache_path, sizeof(b->cover_cache_path),
                (const char *)sqlite3_column_text(stmt, 10));
    b->total_duration_ms = sqlite3_column_int64(stmt, 11);
    b->track_count = sqlite3_column_int(stmt, 12);
    safe_strcpy(b->fingerprint, sizeof(b->fingerprint),
                (const char *)sqlite3_column_text(stmt, 13));
    b->date_added = sqlite3_column_int(stmt, 14);
    b->date_modified = sqlite3_column_int(stmt, 15);
    b->last_played_at = sqlite3_column_int(stmt, 16);
    b->completed = sqlite3_column_int(stmt, 17);
    b->completed_at = sqlite3_column_int(stmt, 18);
    b->playback_speed = sqlite3_column_double(stmt, 19);
    return 1;
}

static int fill_track_from_stmt(sqlite3_stmt *stmt, audiobook_track_t *t) {
    memset(t, 0, sizeof(*t));
    t->track_id = sqlite3_column_int(stmt, 0);
    t->book_id = sqlite3_column_int(stmt, 1);
    t->ordinal = sqlite3_column_int(stmt, 2);
    t->disc_number = sqlite3_column_int(stmt, 3);
    t->track_number = sqlite3_column_int(stmt, 4);
    safe_strcpy(t->path, sizeof(t->path),
                (const char *)sqlite3_column_text(stmt, 5));
    safe_strcpy(t->title, sizeof(t->title),
                (const char *)sqlite3_column_text(stmt, 6));
    safe_strcpy(t->sort_title, sizeof(t->sort_title),
                (const char *)sqlite3_column_text(stmt, 7));
    t->duration_ms = sqlite3_column_int64(stmt, 8);
    t->embedded_chapters = sqlite3_column_int(stmt, 9);
    t->file_size = sqlite3_column_int64(stmt, 10);
    t->file_mtime = sqlite3_column_int(stmt, 11);
    safe_strcpy(t->fingerprint, sizeof(t->fingerprint),
                (const char *)sqlite3_column_text(stmt, 12));
    return 1;
}

static int fill_chapter_from_stmt(sqlite3_stmt *stmt, audiobook_chapter_t *c) {
    memset(c, 0, sizeof(*c));
    c->chapter_id = sqlite3_column_int(stmt, 0);
    c->track_id = sqlite3_column_int(stmt, 1);
    c->ordinal = sqlite3_column_int(stmt, 2);
    safe_strcpy(c->title, sizeof(c->title),
                (const char *)sqlite3_column_text(stmt, 3));
    c->start_ms = sqlite3_column_int64(stmt, 4);
    c->end_ms = sqlite3_column_int64(stmt, 5);
    c->bookmarkable = sqlite3_column_int(stmt, 6);
    return 1;
}

static int fill_progress_from_stmt(sqlite3_stmt *stmt, audiobook_progress_t *p) {
    memset(p, 0, sizeof(*p));
    p->book_id = sqlite3_column_int(stmt, 0);
    p->track_id = sqlite3_column_int(stmt, 1);
    p->track_ordinal = sqlite3_column_int(stmt, 2);
    p->position_ms = sqlite3_column_int64(stmt, 3);
    p->total_book_elapsed_ms = sqlite3_column_int64(stmt, 4);
    p->playback_speed = sqlite3_column_double(stmt, 5);
    p->last_played_at = sqlite3_column_int(stmt, 6);
    p->completed = sqlite3_column_int(stmt, 7);
    p->completed_at = sqlite3_column_int(stmt, 8);
    p->last_saved_at = sqlite3_column_int(stmt, 9);
    p->protected_until_ms = sqlite3_column_int64(stmt, 10);
    return 1;
}

/* ---- DB open/close ------------------------------------------------------ */

/* In-process write mutex. The build is -DSQLITE_THREADSAFE=0 so each connection
 * is single-thread-owned, but exFAT fcntl locks may be no-ops. This mutex
 * serializes the two writers (scan on the event thread + save_progress on the
 * player thread) so they never collide in the WAL. */
static pthread_mutex_t g_db_write_lock = PTHREAD_MUTEX_INITIALIZER;

void audiobook_db_write_lock(void)   { pthread_mutex_lock(&g_db_write_lock); }
void audiobook_db_write_unlock(void) { pthread_mutex_unlock(&g_db_write_lock); }

/* Best-effort file copy (small files only — the DB is <1 MB). */
static void copy_file(const char *src_path, const char *dst_path) {
    int src = open(src_path, O_RDONLY);
    if (src < 0) return;
    int dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) { close(src); return; }
    char buf[8192];
    ssize_t n;
    while ((n = read(src, buf, sizeof(buf))) > 0)
        write(dst, buf, (size_t)n);
    close(dst);
    close(src);
}

int audiobook_db_open(const char *db_path, sqlite3 **db_out) {
    if (!db_path || !db_out) return -1;

    /* Ensure parent dirs exist */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", db_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(dir);
    }

    /* One-time migration: if the SD path doesn't exist yet but the old
     * /usr/data DB does, copy it (and -wal/-shm if present) to SD. The
     * SD directory was just created by mkdir_p above. Best-effort: if the
     * copy fails (e.g. SD read-only), we still try to open the SD path —
     * sqlite3_open will create a fresh DB, and the user can re-scan. */
    struct stat sd_st, old_st;
    if (stat(db_path, &sd_st) < 0 && stat(AUDIOBOOK_DB_PATH_OLD, &old_st) == 0) {
        copy_file(AUDIOBOOK_DB_PATH_OLD, db_path);
        char old_aux[512], new_aux[512];
        snprintf(old_aux, sizeof(old_aux), "%s-wal", AUDIOBOOK_DB_PATH_OLD);
        snprintf(new_aux, sizeof(new_aux), "%s-wal", db_path);
        copy_file(old_aux, new_aux);
        snprintf(old_aux, sizeof(old_aux), "%s-shm", AUDIOBOOK_DB_PATH_OLD);
        snprintf(new_aux, sizeof(new_aux), "%s-shm", db_path);
        copy_file(old_aux, new_aux);
    }

    sqlite3 *db = NULL;
    int rc_open = sqlite3_open(db_path, &db);
    if (rc_open != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    /* Bootstrap schema */
    char *err = NULL;
    int rc_schema = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err);
    if (rc_schema != SQLITE_OK) {
        fprintf(stderr, "[library] schema bootstrap failed: %s\n", err ? err : "?");
        sqlite3_free(err);
        sqlite3_close(db);
        return -1;
    }

    /* Set schema version */
    char ver_sql[128];
    snprintf(ver_sql, sizeof(ver_sql),
             "INSERT INTO schema_meta(key,value) VALUES('schema_version','%s') "
             "ON CONFLICT(key) DO UPDATE SET value=excluded.value", SCHEMA_VERSION);
    sqlite3_exec(db, ver_sql, NULL, NULL, NULL);

    /* Set busy timeout to handle concurrent access */
    sqlite3_busy_timeout(db, 5000);

    /* Cap the page cache at ~512 KB (default ~2 MB). On this ~56 MB / handful-
     * of-MB-free device the default cache is a big chunk of resident heap per
     * connection, and we now open two (event ui->db + player g_pl.db). The DB
     * is tiny (<1 MB) so a 512 KB cache is plenty; this saves ~1.5 MB ×2.
     * Negative value = KB (per SQLite docs). Keep WAL (set in SCHEMA_SQL). */
    sqlite3_exec(db, "PRAGMA cache_size=-512;", NULL, NULL, NULL);

    *db_out = db;
    return 0;
}

void audiobook_db_close(sqlite3 *db) {
    if (db) sqlite3_close(db);
}

/* ---- Book queries ------------------------------------------------------- */

int audiobook_get_book_by_key(sqlite3 *db, const char *book_key,
                              audiobook_book_t *out) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET_BOOK_BY_KEY, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, book_key, -1, SQLITE_TRANSIENT);
    int ret = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fill_book_from_stmt(stmt, out);
        ret = 1;
    }
    sqlite3_finalize(stmt);
    return ret;
}

int audiobook_get_book(sqlite3 *db, int book_id, audiobook_book_t *out) {
    if (!db) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET_BOOK_BY_ID, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, book_id);
    int ret = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fill_book_from_stmt(stmt, out);
        ret = 1;
    }
    sqlite3_finalize(stmt);
    return ret;
}

static int list_books_generic(sqlite3 *db, const char *sql,
                              int (*cb)(const audiobook_book_t *, void *),
                              void *ctx) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        audiobook_book_t b;
        fill_book_from_stmt(stmt, &b);
        count++;
        if (cb && cb(&b, ctx) != 0) break;
    }
    sqlite3_finalize(stmt);
    return count;
}

int audiobook_list_books(sqlite3 *db,
                         int (*cb)(const audiobook_book_t *book, void *ctx),
                         void *ctx) {
    return list_books_generic(db, SQL_LIST_BOOKS, cb, ctx);
}

/* list_books_generic with one text bind (?1) — for filtered lists
 * (by author / by series display_name). */
static int list_books_generic_bind(sqlite3 *db, const char *sql,
                                   const char *filter,
                                   int (*cb)(const audiobook_book_t *, void *),
                                   void *ctx) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, filter ? filter : "", -1, SQLITE_TRANSIENT);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        audiobook_book_t b;
        fill_book_from_stmt(stmt, &b);
        count++;
        if (cb && cb(&b, ctx) != 0) break;
    }
    sqlite3_finalize(stmt);
    return count;
}

int audiobook_list_continue(sqlite3 *db,
                            int (*cb)(const audiobook_book_t *book, void *ctx),
                            void *ctx) {
    return list_books_generic(db, SQL_LIST_CONTINUE, cb, ctx);
}

int audiobook_list_finished(sqlite3 *db,
                           int (*cb)(const audiobook_book_t *book, void *ctx),
                           void *ctx) {
    return list_books_generic(db, SQL_LIST_FINISHED, cb, ctx);
}

static int list_strings_generic(sqlite3 *db, const char *sql,
                                int (*cb)(const char *, void *), void *ctx) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        if (val && val[0]) {
            count++;
            if (cb && cb(val, ctx) != 0) break;
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

int audiobook_list_authors(sqlite3 *db,
                          int (*cb)(const char *author, void *ctx),
                          void *ctx) {
    return list_strings_generic(db, SQL_LIST_AUTHORS, cb, ctx);
}

int audiobook_list_series(sqlite3 *db,
                         int (*cb)(const char *series, void *ctx),
                         void *ctx) {
    return list_strings_generic(db, SQL_LIST_SERIES, cb, ctx);
}

int audiobook_list_folders(sqlite3 *db,
                          int (*cb)(const char *path, void *ctx),
                          void *ctx) {
    return list_strings_generic(db, SQL_LIST_FOLDERS, cb, ctx);
}

int audiobook_list_books_by_author(sqlite3 *db, const char *author,
                                    int (*cb)(const audiobook_book_t *book,
                                              void *ctx),
                                    void *ctx) {
    return list_books_generic_bind(db, SQL_LIST_BOOKS_BY_AUTHOR, author,
                                   cb, ctx);
}

int audiobook_list_books_by_series(sqlite3 *db, const char *series,
                                    int (*cb)(const audiobook_book_t *book,
                                              void *ctx),
                                    void *ctx) {
    return list_books_generic_bind(db, SQL_LIST_BOOKS_BY_SERIES, series,
                                   cb, ctx);
}

/* ---- Track queries ------------------------------------------------------ */

int audiobook_get_tracks(sqlite3 *db, int book_id,
                        int (*cb)(const audiobook_track_t *track, void *ctx),
                        void *ctx) {
    if (!db) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET_TRACKS, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, book_id);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        audiobook_track_t t;
        fill_track_from_stmt(stmt, &t);
        count++;
        if (cb && cb(&t, ctx) != 0) break;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---- Chapter queries ---------------------------------------------------- */

int audiobook_get_chapters(sqlite3 *db, int book_id,
                          int (*cb)(const audiobook_chapter_t *ch, void *ctx),
                          void *ctx) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET_CHAPTERS, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, book_id);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        audiobook_chapter_t c;
        fill_chapter_from_stmt(stmt, &c);
        count++;
        if (cb && cb(&c, ctx) != 0) break;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---- Progress ----------------------------------------------------------- */

int audiobook_get_progress(sqlite3 *db, int book_id,
                           audiobook_progress_t *out) {
    if (!db) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET_PROGRESS, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, book_id);
    int ret = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fill_progress_from_stmt(stmt, out);
        ret = 1;
    }
    sqlite3_finalize(stmt);
    return ret;
}

int audiobook_save_progress(sqlite3 *db, const audiobook_progress_t *p) {
    if (!db) return -1;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, SQL_SAVE_PROGRESS, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, p->book_id);
    sqlite3_bind_int(stmt, 2, p->track_id);
    sqlite3_bind_int(stmt, 3, p->track_ordinal);
    sqlite3_bind_int64(stmt, 4, p->position_ms);
    sqlite3_bind_int64(stmt, 5, p->total_book_elapsed_ms);
    sqlite3_bind_double(stmt, 6, p->playback_speed);
    sqlite3_bind_int(stmt, 7, p->last_played_at);
    sqlite3_bind_int(stmt, 8, p->completed);
    sqlite3_bind_int(stmt, 9, p->completed_at);
    sqlite3_bind_int(stmt, 10, p->last_saved_at);
    sqlite3_bind_int64(stmt, 11, p->protected_until_ms);
    int ret = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_finalize(stmt);
    return ret;
}

/* ---- Bookmarks ---------------------------------------------------------- */
/* Bookmarks are SD-primary (bookmark_sd.{c,h}); library.db's `bookmarks`
 * table is no longer read or written. It stays in the schema (no migration)
 * but is inert. The db param is retained on these signatures only so
 * audiobook_list_bookmarks can run a one-time DB->SD migration the first time
 * a book's bookmark screen is opened. */

int audiobook_add_bookmark(sqlite3 *db, int book_id, int track_id,
                           int64_t position_ms, int64_t total_book_position_ms,
                           const char *label) {
    (void)db;   /* SD-primary; DB no longer holds bookmarks */
    int created_at = -1;
    if (bookmark_save_sd(book_id, track_id, position_ms,
                         total_book_position_ms, label, &created_at) != 0)
        return -1;
    return created_at;
}

int audiobook_list_bookmarks(sqlite3 *db, int book_id,
                            int (*cb)(const audiobook_bookmark_t *bm, void *ctx),
                            void *ctx) {
    /* One-time migration: the first time a book's bookmarks are listed and no
     * .bm file exists yet, export any legacy in-DB bookmarks to SD (preserving
     * their created_at ids) and leave an empty marker so the DB is never
     * re-queried for this book. After migration SD is authoritative. */
    if (!bookmark_file_exists_sd(book_id)) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, SQL_LIST_BOOKMARKS, -1, &stmt, NULL)
                == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, book_id);
            int cap = 8, n = 0;
            audiobook_bookmark_t *rows = malloc(cap * sizeof(*rows));
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!rows) break;
                if (n == cap) {
                    int ncap = cap * 2;
                    audiobook_bookmark_t *nr = realloc(rows, ncap * sizeof(*rows));
                    if (!nr) break;
                    rows = nr; cap = ncap;
                }
                audiobook_bookmark_t *bm = &rows[n++];
                memset(bm, 0, sizeof(*bm));
                bm->bookmark_id = sqlite3_column_int(stmt, 0);
                bm->book_id = sqlite3_column_int(stmt, 1);
                bm->track_id = sqlite3_column_int(stmt, 2);
                bm->position_ms = sqlite3_column_int64(stmt, 3);
                bm->total_book_position_ms = sqlite3_column_int64(stmt, 4);
                safe_strcpy(bm->label, sizeof(bm->label),
                            (const char *)sqlite3_column_text(stmt, 5));
                bm->created_at = sqlite3_column_int(stmt, 6);
                bm->updated_at = sqlite3_column_int(stmt, 7);
            }
            sqlite3_finalize(stmt);
            /* rows arrive newest-first (SQL_LIST_BOOKMARKS ORDER BY DESC);
             * bookmark_migrate_sd reverses to oldest-first for the file. */
            bookmark_migrate_sd(book_id, rows, n);
            free(rows);
        } else {
            /* DB unreadable for this book — still drop a marker so we don't
             * retry the migration every list. */
            bookmark_migrate_sd(book_id, NULL, 0);
        }
    }
    return bookmark_list_sd(book_id, cb, ctx);
}

int audiobook_delete_bookmark(sqlite3 *db, int book_id, int bookmark_id) {
    (void)db;   /* SD-primary; DB no longer holds bookmarks */
    return bookmark_delete_sd(book_id, bookmark_id);
}

/* ---- Search ------------------------------------------------------------- */

int audiobook_search(sqlite3 *db, const char *query,
                    int (*cb)(const audiobook_book_t *book, void *ctx),
                    void *ctx) {
    /* Use FTS5 to find book_ids, then fetch each book */
    const char *fts_sql =
        "SELECT book_id FROM book_search WHERE book_search MATCH ? "
        "ORDER BY rank";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, fts_sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int book_id = sqlite3_column_int(stmt, 0);
        audiobook_book_t b;
        if (audiobook_get_book(db, book_id, &b) > 0) {
            count++;
            if (cb && cb(&b, ctx) != 0) break;
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---- book.json sidecar -------------------------------------------------- */

int audiobook_save_book_json(const char *root_path, const char *book_key,
                             const audiobook_progress_t *p) {
    if (!root_path || !book_key || !p) return -1;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.json", root_path, book_key);

    /* Write to temp file then rename (atomic) */
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.%s.json.tmp", root_path, book_key);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"book_id\": %d,\n", p->book_id);
    fprintf(f, "  \"book_key\": \"%s\",\n", book_key);
    fprintf(f, "  \"track_ordinal\": %d,\n", p->track_ordinal);
    fprintf(f, "  \"position_ms\": %lld,\n", (long long)p->position_ms);
    fprintf(f, "  \"total_book_elapsed_ms\": %lld,\n",
            (long long)p->total_book_elapsed_ms);
    fprintf(f, "  \"playback_speed\": %.2f,\n", p->playback_speed);
    fprintf(f, "  \"completed\": %d,\n", p->completed);
    fprintf(f, "  \"completed_at\": %lld,\n", (long long)p->completed_at);
    fprintf(f, "  \"protected_until_ms\": %lld,\n",
            (long long)p->protected_until_ms);
    fprintf(f, "  \"last_saved_at\": %lld\n", (long long)p->last_saved_at);
    fprintf(f, "}\n");

    fclose(f);

    if (rename(tmp_path, path) < 0) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

/* Minimal JSON parser for book.json sidecar — extracts known integer/float
 * fields without a full JSON parser. */
int audiobook_load_book_json(const char *root_path, const char *book_key,
                            audiobook_progress_t *out) {
    if (!root_path || !book_key || !out) return -1;
    memset(out, 0, sizeof(*out));

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.json", root_path, book_key);

    FILE *f = fopen(path, "r");
    if (!f) return 0;  /* not found */

    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 0;
    buf[n] = '\0';

    /* Simple field extraction */
    const char *p;
    char tmp[256];

    if ((p = strstr(buf, "\"track_ordinal\""))) {
        sscanf(p, "\"track_ordinal\": %d", &out->track_ordinal);
    }
    if ((p = strstr(buf, "\"position_ms\""))) {
        long long v;
        if (sscanf(p, "\"position_ms\": %lld", &v) == 1) out->position_ms = v;
    }
    if ((p = strstr(buf, "\"total_book_elapsed_ms\""))) {
        long long v;
        if (sscanf(p, "\"total_book_elapsed_ms\": %lld", &v) == 1)
            out->total_book_elapsed_ms = v;
    }
    if ((p = strstr(buf, "\"playback_speed\""))) {
        double v;
        if (sscanf(p, "\"playback_speed\": %lf", &v) == 1) out->playback_speed = v;
    }
    if ((p = strstr(buf, "\"completed\""))) {
        sscanf(p, "\"completed\": %d", &out->completed);
    }
    if ((p = strstr(buf, "\"completed_at\""))) {
        long long v;
        if (sscanf(p, "\"completed_at\": %lld", &v) == 1) out->completed_at = v;
    }
    if ((p = strstr(buf, "\"protected_until_ms\""))) {
        long long v;
        if (sscanf(p, "\"protected_until_ms\": %lld", &v) == 1)
            out->protected_until_ms = v;
    }
    if ((p = strstr(buf, "\"last_saved_at\""))) {
        long long v;
        if (sscanf(p, "\"last_saved_at\": %lld", &v) == 1) out->last_saved_at = v;
    }
    (void)tmp;
    return 1;
}

/* ---- Settings ----------------------------------------------------------- */

int audiobook_get_setting(sqlite3 *db, const char *key, char *out, int out_len) {
    if (!db) return -1;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key=?", -1,
                          &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    int ret = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        if (val) {
            safe_strcpy(out, out_len, val);
            ret = 0;
        }
    }
    sqlite3_finalize(stmt);
    return ret;
}

int audiobook_set_setting(sqlite3 *db, const char *key, const char *value) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO settings(key,value) VALUES(?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    int ret = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_finalize(stmt);
    return ret;
}

/* ---- Utility ------------------------------------------------------------ */

void audiobook_derive_book_key(const char *root_path, char *out, int out_len) {
    if (!root_path || !out || out_len <= 0) return;
    /* Replace all non-alphanumeric chars with underscores, strip leading / */
    int j = 0;
    for (const char *p = root_path; *p && j < out_len - 1; p++) {
        if (isalnum((unsigned char)*p)) {
            out[j++] = *p;
        } else if (*p == '/' || *p == ' ' || *p == '-' || *p == '.' ||
                   *p == '_' || *p == '[' || *p == ']' || *p == '(' || *p == ')') {
            /* Double underscore for path separators to distinguish */
            if (*p == '/') {
                if (j > 0 && out[j-1] != '_') out[j++] = '_';
            } else {
                out[j++] = '_';
            }
        } else {
            out[j++] = '_';
        }
    }
    /* Collapse trailing underscores */
    while (j > 0 && out[j-1] == '_') j--;
    out[j] = '\0';
}

int audiobook_natural_cmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            /* Compare numeric chunks */
            long va = 0, vb = 0;
            while (isdigit((unsigned char)*a)) { va = va * 10 + (*a - '0'); a++; }
            while (isdigit((unsigned char)*b)) { vb = vb * 10 + (*b - '0'); b++; }
            if (va < vb) return -1;
            if (va > vb) return 1;
        } else {
            int ca = tolower((unsigned char)*a);
            int cb = tolower((unsigned char)*b);
            if (ca < cb) return -1;
            if (ca > cb) return 1;
            a++; b++;
        }
    }
    if (!*a && !*b) return 0;
    return *a ? 1 : -1;
}

void audiobook_derive_sort_title(const char *title, char *out, int out_len) {
    if (!title || !out || out_len <= 0) return;
    const char *p = title;
    /* Skip leading whitespace */
    while (*p == ' ') p++;
    /* Strip leading articles */
    if (strncasecmp(p, "the ", 4) == 0) p += 4;
    else if (strncasecmp(p, "a ", 2) == 0) p += 2;
    else if (strncasecmp(p, "an ", 3) == 0) p += 3;
    /* Lowercase the rest */
    int j = 0;
    for (; *p && j < out_len - 1; p++)
        out[j++] = tolower((unsigned char)*p);
    out[j] = '\0';
}