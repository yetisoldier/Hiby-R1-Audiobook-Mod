#include "music_catalog.h"

#include <errno.h>
#include "sqlite3.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define AUDIOBOOK_PATH_SQL \
    "(path LIKE 'a:\\Audiobooks\\%' COLLATE NOCASE " \
    "OR path LIKE '/mnt/sd_0/Audiobooks/%' COLLATE NOCASE " \
    "OR path LIKE '/usr/data/mnt/sd_0/Audiobooks/%' COLLATE NOCASE)"

static int table_exists(sqlite3 *db, const char *table) {
    sqlite3_stmt *stmt = NULL;
    int exists = 0;
    if (sqlite3_prepare_v2(
            db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
            -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) exists = 1;
    sqlite3_finalize(stmt);
    return exists;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "music catalog cleanup: %s: %s\n",
                errmsg ? errmsg : sqlite3_errmsg(db), sql);
        sqlite3_free(errmsg);
    }
    return rc;
}

static int scalar_int(sqlite3 *db, const char *sql, int *value) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    *value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}

static int delete_path_rows(sqlite3 *db, const char *table) {
    if (!table_exists(db, table)) return SQLITE_OK;
    char sql[512];
    snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE %s", table,
             AUDIOBOOK_PATH_SQL);
    return exec_sql(db, sql);
}

static int capture_affected_catalog(sqlite3 *db, const char *column) {
    char sql[1536];
    snprintf(
        sql, sizeof(sql),
        "DROP TABLE IF EXISTS temp.r1_affected_%s;"
        "CREATE TEMP TABLE r1_affected_%s("
        "name TEXT COLLATE NOCASE PRIMARY KEY,removed_cn INT) WITHOUT ROWID;"
        "INSERT INTO r1_affected_%s(name,removed_cn) "
        "SELECT CASE WHEN %s IS NULL OR %s='' THEN 'Unknown' ELSE %s END,"
        "COUNT(*) FROM MEDIA_TABLE "
        "WHERE id IN (SELECT id FROM r1_removed_media_ids) GROUP BY "
        "CASE WHEN %s IS NULL OR %s='' THEN 'Unknown' ELSE %s END "
        "COLLATE NOCASE",
        column, column, column, column, column, column,
        column, column, column);
    return exec_sql(db, sql);
}

static int build_catalog_aggregate(sqlite3 *db, const char *column) {
    char sql[2048];
    snprintf(
        sql, sizeof(sql),
        "DROP TABLE IF EXISTS temp.r1_catalog_agg_%s;"
        "CREATE TEMP TABLE r1_catalog_agg_%s("
        "name TEXT COLLATE NOCASE PRIMARY KEY,id INT,cn INT,"
        "ctime INT,mtime INT) WITHOUT ROWID;"
        "INSERT INTO r1_catalog_agg_%s(name,id,cn,ctime,mtime) "
        "SELECT CASE WHEN %s IS NULL OR %s='' THEN 'Unknown' ELSE %s END,"
        "MIN(id),COUNT(*),MIN(COALESCE(ctime,0)),MAX(COALESCE(mtime,0)) "
        "FROM MEDIA_TABLE WHERE "
        "CASE WHEN %s IS NULL OR %s='' THEN 'Unknown' ELSE %s END "
        "IN (SELECT name FROM r1_affected_%s) GROUP BY "
        "CASE WHEN %s IS NULL OR %s='' THEN 'Unknown' ELSE %s END "
        "COLLATE NOCASE",
        column, column, column,
        column, column, column,
        column, column, column, column,
        column, column, column);
    return exec_sql(db, sql);
}

static int reconcile_named_catalog(
    sqlite3 *db, const char *table, const char *column) {
    if (!table_exists(db, table)) return SQLITE_OK;
    /* Aggregate each affected metadata column once and reuse it for both of
     * HiBy's catalog variants. Preserve unrelated rows exactly as the stock
     * scanner wrote them; some cn fields are flags rather than track counts. */
    char sql[3072];
    snprintf(
        sql, sizeof(sql),
        "DELETE FROM %s WHERE %s IN (SELECT name FROM r1_affected_%s) "
        "AND NOT EXISTS (SELECT 1 FROM r1_catalog_agg_%s a WHERE "
        "a.name=%s.%s COLLATE NOCASE);"
        "UPDATE %s SET "
        "id=(SELECT a.id FROM r1_catalog_agg_%s a WHERE "
        "a.name=%s.%s COLLATE NOCASE),"
        "cn=MAX(0,COALESCE(cn,0)-(SELECT x.removed_cn "
        "FROM r1_affected_%s x WHERE x.name=%s.%s COLLATE NOCASE)),"
        "ctime=(SELECT a.ctime FROM r1_catalog_agg_%s a WHERE "
        "a.name=%s.%s COLLATE NOCASE),"
        "mtime=(SELECT a.mtime FROM r1_catalog_agg_%s a WHERE "
        "a.name=%s.%s COLLATE NOCASE) "
        "WHERE %s IN (SELECT name FROM r1_affected_%s) "
        "AND EXISTS (SELECT 1 FROM r1_catalog_agg_%s a WHERE "
        "a.name=%s.%s COLLATE NOCASE)",
        table, column, column, column, table, column,
        table,
        column, table, column,
        column, table, column,
        column, table, column,
        column, table, column,
        column, column, column, table, column);
    return exec_sql(db, sql);
}

static int capture_affected_formats(sqlite3 *db) {
    if (exec_sql(db,
                 "DROP TABLE IF EXISTS temp.r1_affected_format;"
                 "CREATE TEMP TABLE r1_affected_format("
                 "format TEXT COLLATE NOCASE PRIMARY KEY) WITHOUT ROWID")
        != SQLITE_OK)
        return SQLITE_ERROR;
    static const char *tables[] = {"FORMAT_TABLE", "FORMAT2_TABLE"};
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        if (!table_exists(db, tables[i])) continue;
        char sql[768];
        snprintf(sql, sizeof(sql),
                 "INSERT OR IGNORE INTO r1_affected_format(format) "
                 "SELECT f.format FROM %s f WHERE EXISTS ("
                 "SELECT 1 FROM MEDIA_TABLE m "
                 "WHERE m.id IN (SELECT id FROM r1_removed_media_ids) "
                 "AND LOWER(m.path) LIKE '%%.' || LOWER(f.format))",
                 tables[i]);
        if (exec_sql(db, sql) != SQLITE_OK) return SQLITE_ERROR;
    }
    return SQLITE_OK;
}

static int reconcile_format_catalog(sqlite3 *db, const char *table) {
    if (!table_exists(db, table)) return SQLITE_OK;
    char sql[1536];
    snprintf(
        sql, sizeof(sql),
        "DELETE FROM %s WHERE format IN (SELECT format FROM r1_affected_format) "
        "AND NOT EXISTS (SELECT 1 FROM MEDIA_TABLE m "
        "WHERE LOWER(m.path) LIKE '%%.' || LOWER(%s.format));"
        "UPDATE %s SET "
        "id=(SELECT MIN(id) FROM MEDIA_TABLE "
        "WHERE LOWER(path) LIKE '%%.' || LOWER(%s.format)) "
        "WHERE format IN (SELECT format FROM r1_affected_format) "
        "AND id IN (SELECT id FROM r1_removed_media_ids)",
        table, table, table, table);
    return exec_sql(db, sql);
}

static int decrement_count_table(sqlite3 *db, int removed_media) {
    if (!table_exists(db, "COUNT_TABLE")) return SQLITE_OK;
    char sql[3072];
    snprintf(
        sql, sizeof(sql),
        "UPDATE COUNT_TABLE SET cn=MAX(0,cn-%d) WHERE rowid=("
        "SELECT rowid FROM COUNT_TABLE ORDER BY rowid LIMIT 1 OFFSET 0);"
        "UPDATE COUNT_TABLE SET cn=MAX(0,cn-(SELECT COUNT(*) "
        "FROM r1_affected_album x WHERE NOT EXISTS (SELECT 1 "
        "FROM r1_catalog_agg_album a WHERE a.name=x.name COLLATE NOCASE))) "
        "WHERE rowid=(SELECT rowid FROM COUNT_TABLE ORDER BY rowid LIMIT 1 OFFSET 1);"
        "UPDATE COUNT_TABLE SET cn=MAX(0,cn-(SELECT COUNT(*) "
        "FROM r1_affected_artist x WHERE NOT EXISTS (SELECT 1 "
        "FROM r1_catalog_agg_artist a WHERE a.name=x.name COLLATE NOCASE))) "
        "WHERE rowid=(SELECT rowid FROM COUNT_TABLE ORDER BY rowid LIMIT 1 OFFSET 2);"
        "UPDATE COUNT_TABLE SET cn=MAX(0,cn-(SELECT COUNT(*) "
        "FROM r1_affected_genre x WHERE NOT EXISTS (SELECT 1 "
        "FROM r1_catalog_agg_genre a WHERE a.name=x.name COLLATE NOCASE))) "
        "WHERE rowid=(SELECT rowid FROM COUNT_TABLE ORDER BY rowid LIMIT 1 OFFSET 3);"
        "UPDATE COUNT_TABLE SET cn=MAX(0,cn-(SELECT COUNT(*) "
        "FROM r1_affected_album_artist x WHERE NOT EXISTS (SELECT 1 "
        "FROM r1_catalog_agg_album_artist a "
        "WHERE a.name=x.name COLLATE NOCASE))) WHERE rowid=("
        "SELECT rowid FROM COUNT_TABLE ORDER BY rowid LIMIT 1 OFFSET 4)",
        removed_media);
    return exec_sql(db, sql);
}

static int cleanup_one_database(
    const char *path, int *rows_removed, int *changed) {
    sqlite3 *db = NULL;
    int before = 0;
    int rc = sqlite3_open_v2(path, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
                             NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "music catalog cleanup: open failed %s: %s\n", path,
                db ? sqlite3_errmsg(db) : "unknown error");
        if (db) sqlite3_close(db);
        return -1;
    }
    /* This runs behind a live UI. Prefer a quick retry by the worker over a
     * long SQLite wait that could delay Back-to-Menu during a stock scan. */
    sqlite3_busy_timeout(db, 250);
    if (!table_exists(db, "MEDIA_TABLE")) {
        sqlite3_close(db);
        return 0;
    }
    if (scalar_int(db, "SELECT COUNT(*) FROM MEDIA_TABLE WHERE "
                       AUDIOBOOK_PATH_SQL, &before) != 0) {
        sqlite3_close(db);
        return -1;
    }
    if (before == 0) {
        sqlite3_close(db);
        return 0;
    }

    if (exec_sql(db, "PRAGMA foreign_keys=OFF;BEGIN IMMEDIATE;") != SQLITE_OK)
        goto fail;
    if (exec_sql(db,
                 "CREATE TEMP TABLE r1_removed_media_ids(id INTEGER PRIMARY KEY);"
                 "INSERT OR IGNORE INTO r1_removed_media_ids "
                 "SELECT id FROM MEDIA_TABLE WHERE " AUDIOBOOK_PATH_SQL) != SQLITE_OK)
        goto rollback;

    static const char *affected_columns[] = {
        "artist", "album", "genre", "album_artist"
    };
    for (size_t i = 0;
         i < sizeof(affected_columns) / sizeof(affected_columns[0]); i++)
        if (capture_affected_catalog(db, affected_columns[i]) != SQLITE_OK)
            goto rollback;
    if (capture_affected_formats(db) != SQLITE_OK) goto rollback;

    static const char *path_tables[] = {
        "MEDIA_TABLE", "MEDIA2_TABLE", "MEDIA3_TABLE", "SEARCH_TABLE",
        "RECENT_TABLE", "HISTORY_TABLE", "COLLECT_TABLE",
        "COLLECT_OPERATE_TABLE"
    };
    for (size_t i = 0; i < sizeof(path_tables) / sizeof(path_tables[0]); i++)
        if (delete_path_rows(db, path_tables[i]) != SQLITE_OK) goto rollback;

    static const char *id_tables[] = {"CTIME_TABLE", "MTIME_TABLE"};
    for (size_t i = 0; i < sizeof(id_tables) / sizeof(id_tables[0]); i++) {
        if (!table_exists(db, id_tables[i])) continue;
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "DELETE FROM %s WHERE media_id IN "
                 "(SELECT id FROM r1_removed_media_ids)", id_tables[i]);
        if (exec_sql(db, sql) != SQLITE_OK) goto rollback;
    }
    if (table_exists(db, "INFO_TABLE") &&
        exec_sql(db, "DELETE FROM INFO_TABLE WHERE id IN "
                     "(SELECT id FROM r1_removed_media_ids)") != SQLITE_OK)
        goto rollback;

    static const struct { const char *table; const char *column; } catalogs[] = {
        {"ARTIST_TABLE", "artist"}, {"ARTIST2_TABLE", "artist"},
        {"ALBUM_TABLE", "album"}, {"ALBUM2_TABLE", "album"},
        {"GENRE_TABLE", "genre"}, {"GENRE2_TABLE", "genre"},
        {"ALBUM_ARTIST_TABLE", "album_artist"},
        {"ALBUM_ARTIST2_TABLE", "album_artist"}
    };
    for (size_t i = 0;
         i < sizeof(affected_columns) / sizeof(affected_columns[0]); i++)
        if (build_catalog_aggregate(db, affected_columns[i]) != SQLITE_OK)
            goto rollback;
    for (size_t i = 0; i < sizeof(catalogs) / sizeof(catalogs[0]); i++)
        if (reconcile_named_catalog(db, catalogs[i].table,
                                    catalogs[i].column) != SQLITE_OK)
            goto rollback;
    if (reconcile_format_catalog(db, "FORMAT_TABLE") != SQLITE_OK ||
        reconcile_format_catalog(db, "FORMAT2_TABLE") != SQLITE_OK ||
        decrement_count_table(db, before) != SQLITE_OK)
        goto rollback;

    if (exec_sql(db,
                 "DROP TABLE r1_affected_artist;"
                 "DROP TABLE r1_affected_album;"
                 "DROP TABLE r1_affected_genre;"
                 "DROP TABLE r1_affected_album_artist;"
                 "DROP TABLE r1_affected_format;"
                 "DROP TABLE r1_catalog_agg_artist;"
                 "DROP TABLE r1_catalog_agg_album;"
                 "DROP TABLE r1_catalog_agg_genre;"
                 "DROP TABLE r1_catalog_agg_album_artist;"
                 "DROP TABLE r1_removed_media_ids;COMMIT;") != SQLITE_OK)
        goto fail;
    sqlite3_close(db);
    *rows_removed = before;
    *changed = 1;
    return 0;

rollback:
    exec_sql(db, "ROLLBACK");
fail:
    sqlite3_close(db);
    return -1;
}

int music_catalog_remove_audiobooks(
    const char *const *db_paths,
    size_t path_count,
    music_catalog_cleanup_result_t *result) {
    music_catalog_cleanup_result_t local = {0};
    struct stat seen[8];
    size_t seen_count = 0;

    for (size_t i = 0; i < path_count; i++) {
        struct stat st;
        if (!db_paths[i] || stat(db_paths[i], &st) != 0) {
            if (errno != ENOENT && errno != ENOTDIR)
                local.databases_failed++;
            continue;
        }
        int duplicate = 0;
        for (size_t j = 0; j < seen_count; j++) {
            if (seen[j].st_dev == st.st_dev && seen[j].st_ino == st.st_ino) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        if (seen_count < sizeof(seen) / sizeof(seen[0])) seen[seen_count++] = st;

        local.databases_checked++;
        int removed = 0;
        int changed = 0;
        if (cleanup_one_database(db_paths[i], &removed, &changed) != 0) {
            local.databases_failed++;
            continue;
        }
        local.databases_changed += changed;
        local.audiobook_rows_removed += removed;
    }
    if (result) *result = local;
    return local.databases_failed ? -1 : 0;
}

int music_catalog_remove_audiobooks_default(
    music_catalog_cleanup_result_t *result) {
    static const char *paths[] = {
        "/usr/data/usrlocal_media.db",
        "/data/usrlocal_media.db",
        "/usr/data/mnt/sd_0/usrlocal_media.db"
    };
    return music_catalog_remove_audiobooks(
        paths, sizeof(paths) / sizeof(paths[0]), result);
}
