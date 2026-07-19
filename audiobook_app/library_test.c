/* library_test.c — standalone test harness for the audiobook library.
 *
 * Can be built for the host (native, for testing) or for the device
 * (MIPS, for on-device scanning). Runs the scanner on a given directory
 * and reports the results.
 *
 * Usage: library_test <library_root> [db_path]
 *   default db_path: ./library_test.db
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "library.h"
#include "scan.h"

static int book_count = 0;
static int track_total = 0;
static int chapter_total = 0;

static int print_book_cb(const audiobook_book_t *b, void *ctx) {
    (void)ctx;
    printf("  [%d] %s\n", b->book_id, b->title);
    printf("      key:   %s\n", b->book_key);
    printf("      path:  %s\n", b->root_path);
    printf("      dur:   %lld ms (%.1f min)\n",
           (long long)b->total_duration_ms,
           b->total_duration_ms / 60000.0);
    printf("      tracks: %d\n", b->track_count);
    if (b->author[0]) printf("      author: %s\n", b->author);
    if (b->narrator[0]) printf("      narrator: %s\n", b->narrator);
    if (b->series[0]) printf("      series: %s (%.1f)\n", b->series,
                              b->series_number);
    if (b->cover_path[0]) printf("      cover: %s\n", b->cover_path);
    if (b->completed) printf("      COMPLETED\n");
    if (b->playback_speed != 1.0 && b->playback_speed > 0)
        printf("      speed: %.2fx\n", b->playback_speed);
    book_count++;
    return 0;
}

static int print_track_cb(const audiobook_track_t *t, void *ctx) {
    (void)ctx;
    printf("    %3d. %s (%lld ms, %lld bytes, ch=%d)\n",
           t->ordinal, t->title, (long long)t->duration_ms,
           (long long)t->file_size, t->embedded_chapters);
    track_total++;
    return 0;
}

static int print_chapter_cb(const audiobook_chapter_t *c, void *ctx) {
    (void)ctx;
    printf("    ch %d: %s [%lld-%lld ms]\n", c->ordinal,
           c->title, (long long)c->start_ms, (long long)c->end_ms);
    chapter_total++;
    return 0;
}

static int count_cb(const audiobook_book_t *b, void *ctx) {
    (void)b; (void)ctx;
    book_count++;
    return 0;
}

static void scan_progress(int stage, int current, int total,
                          const char *info, void *ctx) {
    (void)ctx;
    const char *stage_names[] = {
        "starting", "scanning", "processing book",
        "indexing", "cleaning", "done"
    };
    const char *name = stage < 6 ? stage_names[stage] : "?";
    if (stage == 2) {
        printf("\r  [%d/%d] %s: %s", current, total, name,
               info ? info : "");
        fflush(stdout);
    } else {
        if (current > 0 || total > 0)
            printf("  [%s] %d/%d %s\n", name, current, total,
                   info ? info : "");
        else
            printf("  [%s] %s\n", name, info ? info : "");
    }
}

int main(int argc, char **argv) {
    const char *root = AUDIOBOOK_LIBRARY_ROOT;
    const char *db_path = "library_test.db";

    if (argc >= 2) root = argv[1];
    if (argc >= 3) db_path = argv[2];

    printf("Audiobook Library Test\n");
    printf("  root: %s\n", root);
    printf("  db:   %s\n", db_path);
    printf("\n");

    sqlite3 *db = NULL;
    if (audiobook_db_open(db_path, &db) < 0) {
        fprintf(stderr, "Failed to open database: %s\n", db_path);
        return 1;
    }
    printf("Database opened + schema bootstrapped.\n\n");

    /* Scan */
    printf("Scanning library...\n");
    time_t start = time(NULL);
    if (audiobook_scan_library(db, root, scan_progress, NULL) < 0) {
        fprintf(stderr, "Scan failed.\n");
        audiobook_db_close(db);
        return 1;
    }
    time_t elapsed = time(NULL) - start;
    printf("\r  Scan complete in %lld s.                \n\n",
           (long long)elapsed);

    /* Report all books */
    printf("=== All Books ===\n");
    book_count = 0;
    audiobook_list_books(db, print_book_cb, NULL);
    printf("\nTotal books: %d\n\n", book_count);

    /* For each book, show tracks + chapters */
    printf("=== Book Details ===\n");
    book_count = 0;
    audiobook_list_books(db, NULL, NULL);  /* just count */
    {
        /* Re-list with detail */
        sqlite3_stmt *stmt = NULL;
        sqlite3_prepare_v2(db, "SELECT book_id FROM books ORDER BY sort_title",
                          -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int book_id = sqlite3_column_int(stmt, 0);
            audiobook_book_t b;
            if (audiobook_get_book(db, book_id, &b) > 0) {
                printf("\nBook: %s\n", b.title);
                printf("  Tracks:\n");
                track_total = 0;
                audiobook_get_tracks(db, book_id, print_track_cb, NULL);
                printf("  Chapters:\n");
                chapter_total = 0;
                audiobook_get_chapters(db, book_id, print_chapter_cb, NULL);
                if (chapter_total == 0)
                    printf("    (no embedded chapters)\n");

                audiobook_progress_t p;
                if (audiobook_get_progress(db, book_id, &p) > 0) {
                    printf("  Progress: track_ordinal=%d pos=%lldms elapsed=%lldms\n",
                           p.track_ordinal, (long long)p.position_ms,
                           (long long)p.total_book_elapsed_ms);
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    /* Continue listening */
    printf("\n=== Continue Listening ===\n");
    book_count = 0;
    int n = audiobook_list_continue(db, print_book_cb, NULL);
    if (n == 0) printf("  (none)\n");
    printf("\n");

    /* Finished */
    printf("=== Finished ===\n");
    book_count = 0;
    n = audiobook_list_finished(db, print_book_cb, NULL);
    if (n == 0) printf("  (none)\n");
    printf("\n");

    /* Authors */
    printf("=== Authors ===\n");
    audiobook_list_authors(db, NULL, NULL);
    /* Count + print */
    {
        sqlite3_stmt *stmt = NULL;
        sqlite3_prepare_v2(db,
            "SELECT DISTINCT COALESCE(a.display_name,'') FROM books b "
            "LEFT JOIN authors a ON a.author_id=b.author_id "
            "WHERE a.display_name IS NOT NULL AND a.display_name<>'' "
            "ORDER BY a.display_name COLLATE NOCASE", -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("  %s\n", sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    printf("\n");

    /* Folders */
    printf("=== Folders ===\n");
    {
        sqlite3_stmt *stmt = NULL;
        sqlite3_prepare_v2(db,
            "SELECT DISTINCT COALESCE(root_path,'') FROM books "
            "WHERE root_path IS NOT NULL AND root_path<>'' "
            "ORDER BY root_path COLLATE NOCASE", -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("  %s\n", sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    printf("\n");

    /* Search test */
    printf("=== Search Test (query: 'calypso') ===\n");
    book_count = 0;
    n = audiobook_search(db, "calypso", print_book_cb, NULL);
    printf("Found %d results.\n\n", n);

    /* Idempotent re-scan test */
    printf("=== Idempotent Re-scan ===\n");
    book_count = 0;
    audiobook_scan_library(db, root, NULL, NULL);
    audiobook_list_books(db, count_cb, NULL);
    printf("  Books after re-scan: %d (should be same as first scan)\n\n",
           book_count);

    /* DB integrity check */
    printf("=== DB Integrity ===\n");
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &stmt, NULL)
        == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("  %s\n", sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    printf("\nDone.\n");
    audiobook_db_close(db);
    return 0;
}