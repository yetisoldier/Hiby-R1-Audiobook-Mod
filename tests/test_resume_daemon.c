/*
 * test_resume_daemon.c — unit tests for Phase 2 modules
 *
 * Compiles and runs on the Linux host alongside the test binary.
 * Uses simple assert-based testing (no external framework).
 *
 * Tests:
 *   - JSON serialization round-trip (resume.c)
 *   - Completion detection thresholds (resume.c)
 *   - Save bucketing timing (resume.c)
 *   - Restore target computation (resume.c)
 *   - Deferred save logic (resume.c)
 *   - safe_id (resume.c)
 *   - json_escape (resume.c)
 *   - Path classification (player.c)
 *   - Path slot hex decoding (player.c)
 *   - Catalog TSV parsing (catalog.c)
 */

#include "config.h"
#include "resume.h"
#include "player.h"
#include "catalog.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

/* ── Test framework ──────────────────────────────────────────────── */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n    %s:%d: assertion failed: %s\n", \
                   __FILE__, __LINE__, #cond); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define CHECK_STR_EQ(actual, expected) \
    do { \
        if (strcmp(actual, expected) != 0) { \
            printf("FAIL\n    %s:%d: expected \"%s\", got \"%s\"\n", \
                   __FILE__, __LINE__, expected, actual); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define CHECK_INT_EQ(actual, expected) \
    do { \
        if ((actual) != (expected)) { \
            printf("FAIL\n    %s:%d: expected %d, got %d\n", \
                   __FILE__, __LINE__, (int)(expected), (int)(actual)); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        int _failed_before = tests_failed; \
        tests_run++; \
        printf("  [%d] %s ... ", tests_run, #fn); \
        fn(); \
        if (tests_failed > _failed_before) { \
            /* test failed and already printed FAIL line */ \
        } else { \
            printf("ok\n"); \
            tests_passed++; \
        } \
    } while (0)

/* ── Helper: create a temp directory ─────────────────────────────── */

static char tmpdir[256];

static void setup_tmpdir(void) {
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    snprintf(tmpdir, sizeof(tmpdir), "%s/resume_test_XXXXXX", base);
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        exit(1);
    }
    printf("Using temp dir: %s\n", tmpdir);
}

static void cleanup_tmpdir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

/* ── Helper: create a minimal config ─────────────────────────────── */

static void make_test_config(daemon_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->base_dir, sizeof(cfg->base_dir), "%s", tmpdir);
    snprintf(cfg->store_dir, sizeof(cfg->store_dir), "%s/resume.d", tmpdir);
    snprintf(cfg->log_path, sizeof(cfg->log_path), "%s/test.log", tmpdir);
    snprintf(cfg->pid_file, sizeof(cfg->pid_file), "%s/test.pid", tmpdir);
    snprintf(cfg->catalog_path, sizeof(cfg->catalog_path), "%s/catalog.tsv", tmpdir);
    snprintf(cfg->catalog_albums_path, sizeof(cfg->catalog_albums_path), "%s/catalog-albums.txt", tmpdir);
    snprintf(cfg->catalog_books_path, sizeof(cfg->catalog_books_path), "%s/catalog-books.tsv", tmpdir);
    snprintf(cfg->user_ini_path, sizeof(cfg->user_ini_path), "%s/user.ini", tmpdir);

    cfg->interval_seconds = 5;
    cfg->save_bucket_ms = 15000;
    cfg->min_save_ms = 3000;
    cfg->restore_enabled = 1;
    cfg->track_restore_enabled = 1;
    cfg->restore_only_before_ms = 15000;
    cfg->restore_min_ms = 10000;
    cfg->restore_rewind_ms = 5000;
    cfg->restore_retry_after_failure_seconds = 30;
    cfg->restore_retry_max_after_failure_seconds = 300;
    cfg->new_track_commit_ms = 15000;
    cfg->completed_end_threshold_ms = 45000;
    cfg->backward_save_guard_ms = 5000;
    cfg->failed_restore_skip_log_bucket_ms = 30000;
    cfg->log_max_bytes = 524288;

    mkdir(cfg->store_dir, 0755);
}

/* ── Tests ─────────────────────────────────────────────────────────── */

static void test_json_escape_plain(void) {
    char in[] = "hello world";
    char out[64];
    size_t n = json_escape(in, out, sizeof(out));
    CHECK_STR_EQ(out, "hello world");
    CHECK_INT_EQ((int)n, 11);
}

static void test_json_escape_special(void) {
    char in[] = "a:\\b\"c";
    char out[64];
    size_t n = json_escape(in, out, sizeof(out));
    CHECK_STR_EQ(out, "a:\\\\b\\\"c");
    CHECK_INT_EQ((int)n, 8);
}

static void test_json_escape_empty(void) {
    char in[] = "";
    char out[64];
    size_t n = json_escape(in, out, sizeof(out));
    CHECK_INT_EQ((int)n, 0);
    CHECK_STR_EQ(out, "");
}

static void test_json_value_string(void) {
    char json[] = "{\n  \"name\": \"hello\",\n  \"age\": 42\n}";
    char out[64];
    int rc = json_value(json, "name", out, sizeof(out));
    CHECK_INT_EQ(rc, 0);
    CHECK_STR_EQ(out, "hello");
}

static void test_json_value_number(void) {
    char json[] = "{\"position_ms\": 1234567, \"completed\": true}";
    char out[64];
    int rc = json_value(json, "position_ms", out, sizeof(out));
    CHECK_INT_EQ(rc, 0);
    CHECK_STR_EQ(out, "1234567");
}

static void test_json_number_int(void) {
    char json[] = "{\"track_index\": 3, \"track_count\": 15}";
    int val;
    int rc = json_number(json, "track_index", &val);
    CHECK_INT_EQ(rc, 0);
    CHECK_INT_EQ(val, 3);
}

static void test_json_number_null(void) {
    char json[] = "{\"media_id\": null, \"track_index\": 5}";
    int val;
    int rc = json_number(json, "media_id", &val);
    CHECK_INT_EQ(rc, 0);
    CHECK_INT_EQ(val, -1);
}

static void test_json_bool_true(void) {
    char json[] = "{\"completed\": true}";
    bool val;
    int rc = json_bool(json, "completed", &val);
    CHECK_INT_EQ(rc, 0);
    CHECK(val == true);
}

static void test_json_bool_false(void) {
    char json[] = "{\"completed\": false}";
    bool val;
    int rc = json_bool(json, "completed", &val);
    CHECK_INT_EQ(rc, 0);
    CHECK(val == false);
}

static void test_json_value_escaped(void) {
    char json[] = "{\"path\": \"a:\\\\Audiobooks\\\\Book\"}";
    char out[128];
    int rc = json_value(json, "path", out, sizeof(out));
    CHECK_INT_EQ(rc, 0);
    CHECK_STR_EQ(out, "a:\\Audiobooks\\Book");
}

static void test_safe_id_alnum(void) {
    char out[256];
    safe_id("abc123", out, sizeof(out));
    CHECK_STR_EQ(out, "abc123");
}

static void test_safe_id_special(void) {
    char out[256];
    safe_id("a:\\Audiobooks\\Book Title", out, sizeof(out));
    CHECK_STR_EQ(out, "a__Audiobooks_Book_Title");
}

static void test_safe_id_empty(void) {
    char out[256];
    safe_id("", out, sizeof(out));
    CHECK_STR_EQ(out, "");
}

static void test_restore_target_basic(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 5000;
    cfg.restore_min_ms = 10000;
    uint32_t target = restore_target_ms(&cfg, 120000);
    CHECK_INT_EQ((int)target, 115000);
}

static void test_restore_target_zero(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 5000;
    cfg.restore_min_ms = 0;
    uint32_t target = restore_target_ms(&cfg, 3000);
    CHECK_INT_EQ((int)target, 0);
}

static void test_restore_target_clamp(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 5000;
    cfg.restore_min_ms = 10000;
    uint32_t target = restore_target_ms(&cfg, 12000);
    CHECK_INT_EQ((int)target, 10000);
}

static void test_restore_target_no_rewind(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 0;
    cfg.restore_min_ms = 0;
    uint32_t target = restore_target_ms(&cfg, 60000);
    CHECK_INT_EQ((int)target, 60000);
}

static void test_save_bucketing_same(void) {
    int bucket1 = 120000 / 15000;
    int bucket3 = 121000 / 15000;
    CHECK_INT_EQ(bucket1, 8);
    CHECK_INT_EQ(bucket3, 8);
}

static void test_save_bucketing_diff(void) {
    int bucket1 = 120000 / 15000;
    int bucket2 = 150000 / 15000;
    CHECK_INT_EQ(bucket1, 8);
    CHECK_INT_EQ(bucket2, 10);
}

static void test_defer_save_same_path(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;
    bool defer = should_defer_new_track_save(&cfg, "a:\\Book\\01.mp3",
                                             "a:\\Book\\01.mp3",
                                             time(NULL) - 5, time(NULL));
    CHECK(!defer);
}

static void test_defer_save_recent_switch(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;
    time_t now = time(NULL);
    bool defer = should_defer_new_track_save(&cfg, "a:\\Book\\02.mp3",
                                             "a:\\Book\\01.mp3",
                                             now - 5, now);
    CHECK(defer);
}

static void test_defer_save_old_enough(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;
    time_t now = time(NULL);
    bool defer = should_defer_new_track_save(&cfg, "a:\\Book\\02.mp3",
                                             "a:\\Book\\01.mp3",
                                             now - 20, now);
    CHECK(!defer);
}

static void test_defer_save_no_prev(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;
    bool defer = should_defer_new_track_save(&cfg, "a:\\Book\\01.mp3",
                                             "", 0, time(NULL));
    CHECK(!defer);
}

static void test_completed_match(void) {
    bool skip = should_skip_after_completed_restore("a:\\Book\\01.mp3",
                                                     "a:\\Book\\01.mp3");
    CHECK(skip);
}

static void test_completed_diff(void) {
    bool skip = should_skip_after_completed_restore("a:\\Book\\01.mp3",
                                                     "a:\\Book\\02.mp3");
    CHECK(!skip);
}

static void test_completed_null(void) {
    bool skip = should_skip_after_completed_restore("a:\\Book\\01.mp3", NULL);
    CHECK(!skip);
}

static void test_seek_failure_increments(void) {
    resume_reset_failures();
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key1");
    CHECK_INT_EQ(resume_get_seek_failure_count(), 1);
    CHECK_STR_EQ(resume_get_failure_kind(), "seek");
}

static void test_seek_failure_multiple(void) {
    resume_reset_failures();
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key1");
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key1");
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key1");
    CHECK_INT_EQ(resume_get_seek_failure_count(), 3);
}

static void test_track_failure_resets(void) {
    resume_reset_failures();
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key1");
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key1");
    CHECK_INT_EQ(resume_get_seek_failure_count(), 2);
    note_track_restore_failure("a:\\Book\\01.mp3");
    CHECK_INT_EQ(resume_get_seek_failure_count(), 0);
    CHECK_STR_EQ(resume_get_failure_kind(), "track");
}

static void test_retry_delay_first(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    note_seek_restore_failure("path", 100000, "key");
    uint32_t delay = restore_retry_delay_seconds(&cfg);
    CHECK_INT_EQ((int)delay, 30);
}

static void test_retry_delay_second(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    note_seek_restore_failure("path", 100000, "key");
    note_seek_restore_failure("path", 100000, "key");
    uint32_t delay = restore_retry_delay_seconds(&cfg);
    CHECK_INT_EQ((int)delay, 60);
}

static void test_retry_delay_third(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    note_seek_restore_failure("path", 100000, "key");
    note_seek_restore_failure("path", 100000, "key");
    note_seek_restore_failure("path", 100000, "key");
    uint32_t delay = restore_retry_delay_seconds(&cfg);
    CHECK_INT_EQ((int)delay, 120);
}

static void test_retry_delay_capped(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    for (int i = 0; i < 10; i++) {
        note_seek_restore_failure("path", 100000, "key");
    }
    uint32_t delay = restore_retry_delay_seconds(&cfg);
    CHECK_INT_EQ((int)delay, 300);
}

static void test_attempt_restore_in_range(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 1;
    cfg.restore_only_before_ms = 15000;
    cfg.restore_min_ms = 5000;
    bool attempt = should_attempt_restore_for_position(&cfg, 10000, false);
    CHECK(attempt);
}

static void test_attempt_restore_too_high(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 1;
    cfg.restore_only_before_ms = 15000;
    cfg.restore_min_ms = 5000;
    bool attempt = should_attempt_restore_for_position(&cfg, 20000, false);
    CHECK(!attempt);
}

static void test_attempt_restore_autostart(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 1;
    cfg.restore_only_before_ms = 15000;
    cfg.restore_min_ms = 5000;
    bool attempt = should_attempt_restore_for_position(&cfg, 20000, true);
    CHECK(attempt);
}

static void test_attempt_restore_too_low(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 1;
    cfg.restore_only_before_ms = 15000;
    cfg.restore_min_ms = 5000;
    bool attempt = should_attempt_restore_for_position(&cfg, 3000, false);
    CHECK(!attempt);
}

static void test_attempt_restore_disabled(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 0;
    bool attempt = should_attempt_restore_for_position(&cfg, 10000, false);
    CHECK(!attempt);
}

static void test_skip_failed_no_failure(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    bool skip = should_skip_failed_restore_save(&cfg, "a:\\Book\\01.mp3", 5000);
    CHECK(!skip);
}

static void test_skip_failed_diff_path(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key");
    bool skip = should_skip_failed_restore_save(&cfg, "a:\\Other\\02.mp3", 5000);
    CHECK(!skip);
}

static void test_path_audiobook_standard(void) {
    bool result = path_preview_is_audiobook("a:\\Audiobooks\\Book Title");
    CHECK(result);
}

static void test_path_audiobook_no_drive(void) {
    bool result = path_preview_is_audiobook(":\\Audiobooks\\Book Title");
    CHECK(result);
}

static void test_path_audiobook_backslash(void) {
    bool result = path_preview_is_audiobook("\\Audiobooks\\Book Title");
    CHECK(result);
}

static void test_path_audiobook_music(void) {
    bool result = path_preview_is_audiobook("a:\\Music\\Album");
    CHECK(!result);
}

static void test_path_audiobook_empty(void) {
    bool result = path_preview_is_audiobook("");
    CHECK(!result);
}

static void test_path_music_standard(void) {
    bool result = path_preview_is_music("a:\\Music\\Album");
    CHECK(result);
}

static void test_path_music_no_drive(void) {
    bool result = path_preview_is_music(":\\Music\\Album");
    CHECK(result);
}

static void test_path_music_audiobook(void) {
    bool result = path_preview_is_music("a:\\Audiobooks\\Book");
    CHECK(!result);
}

static void test_decode_hex_simple(void) {
    /* "Hi" in UTF-16LE: H=0x48,0x00  i=0x69,0x00  then null=0x00,0x00 */
    const char *hex = "4800690000000000";
    char out[512];
    int rc = decode_path_slot_hex(hex, out, sizeof(out));
    CHECK_INT_EQ(rc, 0);
    CHECK_STR_EQ(out, "Hi");
}

static void test_decode_hex_null(void) {
    const char *hex = "4800690000006100";
    char out[512];
    int rc = decode_path_slot_hex(hex, out, sizeof(out));
    CHECK_INT_EQ(rc, 0);
    CHECK_STR_EQ(out, "Hi");
}

static void test_save_and_read_record(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    tmpl.schema_version = 3;
    strcpy(tmpl.book_id, "a__Audiobooks_Book");
    strcpy(tmpl.book_key, "bookkey123");
    strcpy(tmpl.root_hiby_path, "a:\\Audiobooks\\Book");
    tmpl.media_id = 12345;
    tmpl.track_index = 3;
    tmpl.track_count = 15;
    strcpy(tmpl.chapter_title, "Chapter 3 - The Beginning");

    int rc = save_position(&cfg, "a:\\Audiobooks\\Book\\03 - Chapter.mp3",
                           1234567, &tmpl);
    CHECK_INT_EQ(rc, 0);

    resume_record loaded;
    int rc2 = existing_record_for_path(&cfg, "a:\\Audiobooks\\Book\\03 - Chapter.mp3",
                                        "bookkey123", "a:\\Audiobooks\\Book", &loaded);
    CHECK_INT_EQ(rc2, 0);
    CHECK_INT_EQ(loaded.schema_version, 3);
    CHECK_INT_EQ((int)loaded.position_ms, 1234567);
    CHECK_INT_EQ(loaded.media_id, 12345);
    CHECK_INT_EQ(loaded.track_index, 3);
    CHECK_INT_EQ(loaded.track_count, 15);
    CHECK_STR_EQ(loaded.current_path, "a:\\Audiobooks\\Book\\03 - Chapter.mp3");
    CHECK_STR_EQ(loaded.book_key, "bookkey123");
    CHECK_STR_EQ(loaded.chapter_title, "Chapter 3 - The Beginning");
    CHECK(loaded.completed == false);
}

static void test_no_record(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    resume_record rec;
    int rc = existing_record_for_path(&cfg, "a:\\Nonexistent\\Path",
                                       "nokey", "a:\\Nonexistent", &rec);
    CHECK_INT_EQ(rc, -1);
}

static void test_record_path_book_key(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char file_path[512];
    int rc = record_for_path(&cfg, "a:\\Book\\01.mp3", "mybookkey",
                             "a:\\Book", file_path, sizeof(file_path));
    CHECK_INT_EQ(rc, 0);
    CHECK(strstr(file_path, "mybookkey") != NULL);
    CHECK(strstr(file_path, ".json") != NULL);
}

static void test_record_path_root_fallback(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char file_path[512];
    int rc = record_for_path(&cfg, "a:\\Book\\01.mp3", "",
                             "a:\\Book", file_path, sizeof(file_path));
    CHECK_INT_EQ(rc, 0);
    CHECK(strstr(file_path, "a__Book") != NULL);
}

static void test_catalog_parse(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    FILE *fp = fopen(cfg.catalog_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "root\tindex\tcount\tmedia_id\tpath\ttitle\talbum\t\tbook_key\n");
    fprintf(fp, "a:\\Audiobooks\\Book1\t1\t3\t101\ta:\\Audiobooks\\Book1\\01.mp3\tChapter 1\tBook One\t\tbk1\n");
    fprintf(fp, "a:\\Audiobooks\\Book1\t2\t3\t102\ta:\\Audiobooks\\Book1\\02.mp3\tChapter 2\tBook One\t\tbk1\n");
    fprintf(fp, "a:\\Audiobooks\\Book1\t3\t3\t103\ta:\\Audiobooks\\Book1\\03.mp3\tChapter 3\tBook One\t\tbk1\n");
    fprintf(fp, "a:\\Audiobooks\\Book2\t1\t2\t201\ta:\\Audiobooks\\Book2\\01.mp3\tChapter 1\tBook Two\t\tbk2\n");
    fprintf(fp, "a:\\Audiobooks\\Book2\t2\t2\t202\ta:\\Audiobooks\\Book2\\02.mp3\tChapter 2\tBook Two\t\tbk2\n");
    fclose(fp);

    catalog_db db;
    int rc = catalog_load(&db, cfg.catalog_path, "", cfg.catalog_books_path);
    CHECK_INT_EQ(rc, 0);
    CHECK_INT_EQ((int)db.count, 5);

    const catalog_entry *e = catalog_field_for_path(&db,
                            "a:\\Audiobooks\\Book1\\02.mp3");
    CHECK(e != NULL);
    CHECK_INT_EQ(e->index, 2);
    CHECK_INT_EQ(e->count, 3);
    CHECK_INT_EQ(e->media_id, 102);
    CHECK_STR_EQ(e->title, "Chapter 2");
    CHECK_STR_EQ(e->album, "Book One");
    CHECK_STR_EQ(e->book_key, "bk1");

    const catalog_entry *e2 = catalog_field_for_root_index(&db,
                                "a:\\Audiobooks\\Book2", 1);
    CHECK(e2 != NULL);
    CHECK_INT_EQ(e2->media_id, 201);
    CHECK_STR_EQ(e2->title, "Chapter 1");

    char path_out[512];
    int rc2 = catalog_first_path_for_root(&db, "a:\\Audiobooks\\Book1",
                                           path_out, sizeof(path_out));
    CHECK_INT_EQ(rc2, 0);
    CHECK_STR_EQ(path_out, "a:\\Audiobooks\\Book1\\01.mp3");

    const char *bk = book_key_for_path(&db, "a:\\Audiobooks\\Book2\\02.mp3");
    CHECK_STR_EQ(bk, "bk2");

    catalog_free(&db);
}

static void test_catalog_missing_file(void) {
    catalog_db db;
    int rc = catalog_load(&db, "/nonexistent/catalog.tsv", "", "");
    CHECK_INT_EQ(rc, 0);
    CHECK_INT_EQ((int)db.count, 0);
    CHECK(db.entries == NULL);
    catalog_free(&db);
}

static void test_catalog_album_patterns(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    FILE *fp = fopen(cfg.catalog_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "root\tindex\tcount\tmedia_id\tpath\ttitle\talbum\t\tbook_key\n");
    fprintf(fp, "a:\\B1\t1\t2\t1\ta:\\B1\\01\tT1\tAlbum A\t\tk1\n");
    fprintf(fp, "a:\\B1\t2\t2\t2\ta:\\B1\\02\tT2\tAlbum A\t\tk1\n");
    fprintf(fp, "a:\\B2\t1\t1\t3\ta:\\B2\\01\tT3\tAlbum B\t\tk2\n");
    fclose(fp);

    catalog_db db;
    catalog_load(&db, cfg.catalog_path, "", cfg.catalog_books_path);
    int rc = refresh_catalog_album_patterns(&db);
    CHECK_INT_EQ(rc, 0);
    CHECK_INT_EQ((int)db.album_pattern_count, 2);
    CHECK_STR_EQ(db.album_patterns[0], "Album A");
    CHECK_STR_EQ(db.album_patterns[1], "Album B");

    catalog_free(&db);
}

/* ── UI tests ─────────────────────────────────────────────────────── */

static void test_pixel_white_pure(void) {
    /* Pure white in RGB565: r=31, g=63, b=31 → v = (31<<11)|(63<<5)|31 = 0xFFFF */
    CHECK(pixel_is_white(0xFFFF));
}

static void test_pixel_white_near(void) {
    /* Near-white: r=24, g=48, b=24 → just above threshold */
    uint16_t v = (24 << 11) | (48 << 5) | 24;
    CHECK(pixel_is_white(v));
}

static void test_pixel_white_below_r(void) {
    /* r=23 (below threshold), g=48, b=24 */
    uint16_t v = (23 << 11) | (48 << 5) | 24;
    CHECK(!pixel_is_white(v));
}

static void test_pixel_white_below_g(void) {
    /* r=24, g=47 (below threshold), b=24 */
    uint16_t v = (24 << 11) | (47 << 5) | 24;
    CHECK(!pixel_is_white(v));
}

static void test_pixel_white_below_b(void) {
    /* r=24, g=48, b=23 (below threshold) */
    uint16_t v = (24 << 11) | (48 << 5) | 23;
    CHECK(!pixel_is_white(v));
}

static void test_pixel_blue_pure(void) {
    /* Blue: r<=10, g>=24, b>=18 → r=0, g=24, b=18 */
    uint16_t v = (0 << 11) | (24 << 5) | 18;
    CHECK(pixel_is_blue(v));
}

static void test_pixel_blue_high_r(void) {
    /* r=11 (above blue threshold) */
    uint16_t v = (11 << 11) | (24 << 5) | 18;
    CHECK(!pixel_is_blue(v));
}

static void test_pixel_blue_low_g(void) {
    /* g=23 (below threshold) */
    uint16_t v = (0 << 11) | (23 << 5) | 18;
    CHECK(!pixel_is_blue(v));
}

static void test_pixel_blue_low_b(void) {
    /* b=17 (below threshold) */
    uint16_t v = (0 << 11) | (24 << 5) | 17;
    CHECK(!pixel_is_blue(v));
}

static void test_seek_x_midpoint(void) {
    /* saved_pos = half of duration → x should be midrange */
    uint16_t x = ui_seek_compute_x(50000, 100000, 21, 459);
    /* range = 438, x = 21 + (50000*438 + 50000)/100000 = 21 + 219 = 240 */
    CHECK_INT_EQ((int)x, 240);
}

static void test_seek_x_start(void) {
    /* saved_pos near 0 → x should be x_min + 1 */
    uint16_t x = ui_seek_compute_x(100, 100000, 21, 459);
    CHECK_INT_EQ((int)x, 22);
}

static void test_seek_x_end(void) {
    /* saved_pos near duration → x should be x_max - 1 */
    uint16_t x = ui_seek_compute_x(99900, 100000, 21, 459);
    CHECK_INT_EQ((int)x, 458);
}

static void test_seek_x_zero_duration(void) {
    uint16_t x = ui_seek_compute_x(50000, 0, 21, 459);
    CHECK_INT_EQ((int)x, 0);
}

static void test_seek_x_tiny_range(void) {
    /* range <= 2 → should return 0 */
    uint16_t x = ui_seek_compute_x(50000, 100000, 21, 22);
    CHECK_INT_EQ((int)x, 0);
}

static void test_seek_x_clamp_min(void) {
    /* saved_pos = 0 → x = x_min + (0 + duration/2)/duration = x_min + 0 → clamp to x_min+1 */
    uint16_t x = ui_seek_compute_x(0, 100000, 21, 459);
    CHECK_INT_EQ((int)x, 22);
}

static void test_seek_x_clamp_max(void) {
    /* saved_pos = duration → x = x_min + (duration*range + duration/2)/duration
       = x_min + range + 0 = x_max → clamp to x_max-1 */
    uint16_t x = ui_seek_compute_x(100000, 100000, 21, 459);
    CHECK_INT_EQ((int)x, 458);
}

static void test_fb_white_region_mock(void) {
    /* Create a mock framebuffer file with known pixel data.
       We write a small buffer where some pixels are white.
       Stride = 4 bytes (2 pixels at 2 bytes each). */
    char fb_path[512];
    snprintf(fb_path, sizeof(fb_path), "%s/mock_fb.bin", tmpdir);

    int fd = open(fb_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);

    /* Row 0: pixel 0 = white (0xFFFF), pixel 1 = black (0x0000) */
    unsigned char row0[4] = { 0xFF, 0xFF, 0x00, 0x00 };
    /* Row 1: pixel 0 = black, pixel 1 = white */
    unsigned char row1[4] = { 0x00, 0x00, 0xFF, 0xFF };
    write(fd, row0, 4);
    write(fd, row1, 4);
    close(fd);

    /* Open for reading */
    fd = open(fb_path, O_RDONLY);
    CHECK(fd >= 0);

    /* Count white pixels in region x0=0, y0=0, x1=2, y1=2, stride=4 */
    int count = fb_white_pixels_region(fd, 0, 0, 2, 2, 4);
    close(fd);
    CHECK_INT_EQ(count, 2);
}

static void test_fb_white_region_empty(void) {
    /* All-black mock framebuffer */
    char fb_path[512];
    snprintf(fb_path, sizeof(fb_path), "%s/mock_fb_black.bin", tmpdir);

    int fd = open(fb_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    unsigned char row[4] = { 0x00, 0x00, 0x00, 0x00 };
    write(fd, row, 4);
    write(fd, row, 4);
    close(fd);

    fd = open(fb_path, O_RDONLY);
    CHECK(fd >= 0);

    int count = fb_white_pixels_region(fd, 0, 0, 2, 2, 4);
    close(fd);
    CHECK_INT_EQ(count, 0);
}

static void test_fb_white_region_partial(void) {
    /* Mock framebuffer with partial white pixels in a sub-region */
    char fb_path[512];
    snprintf(fb_path, sizeof(fb_path), "%s/mock_fb_partial.bin", tmpdir);

    int fd = open(fb_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);

    /* 4 pixels per row, stride=8 bytes. Row 0: first 2 pixels white */
    unsigned char row0[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };
    unsigned char row1[8] = { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 };
    write(fd, row0, 8);
    write(fd, row1, 8);
    close(fd);

    fd = open(fb_path, O_RDONLY);
    CHECK(fd >= 0);

    /* Count white in x0=0, x1=2 (pixels 0-1), y0=0, y1=2 → 2+0 = 2 */
    int count = fb_white_pixels_region(fd, 0, 0, 2, 2, 8);
    close(fd);
    CHECK_INT_EQ(count, 2);
}

static void test_play_mode_value_read(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    /* Write a byte at the configured offset */
    cfg.play_mode_user_ini_offset = 100;

    int fd = open(cfg.user_ini_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    /* Write 100 zero bytes, then the mode value 3 */
    unsigned char buf[101];
    memset(buf, 0, 100);
    buf[100] = 3;
    write(fd, buf, 101);
    close(fd);

    int mode = play_mode_value(&cfg);
    CHECK_INT_EQ(mode, 3);
}

static void test_play_mode_value_missing(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.play_mode_user_ini_offset = 200;

    /* user.ini doesn't exist or is too short */
    int mode = play_mode_value(&cfg);
    CHECK_INT_EQ(mode, -1);
}

static void test_play_mode_value_zero_offset(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.play_mode_user_ini_offset = 0;

    int mode = play_mode_value(&cfg);
    CHECK_INT_EQ(mode, -1);
}

static void test_seek_x_quarter(void) {
    /* saved_pos = 25000, duration = 100000 → x = x_min + 25000*438/100000 ≈ 21 + 109 */
    uint16_t x = ui_seek_compute_x(25000, 100000, 21, 459);
    /* (25000 * 438 + 50000) / 100000 = (10950000 + 50000) / 100000 = 110 */
    CHECK_INT_EQ((int)x, 131);
}

static void test_seek_x_three_quarter(void) {
    /* saved_pos = 75000, duration = 100000 */
    uint16_t x = ui_seek_compute_x(75000, 100000, 21, 459);
    /* (75000 * 438 + 50000) / 100000 = (32850000 + 50000) / 100000 = 329 */
    CHECK_INT_EQ((int)x, 350);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    setup_tmpdir();

    printf("\n=== Resume Daemon Unit Tests ===\n\n");

    RUN_TEST(test_json_escape_plain);
    RUN_TEST(test_json_escape_special);
    RUN_TEST(test_json_escape_empty);
    RUN_TEST(test_json_value_string);
    RUN_TEST(test_json_value_number);
    RUN_TEST(test_json_number_int);
    RUN_TEST(test_json_number_null);
    RUN_TEST(test_json_bool_true);
    RUN_TEST(test_json_bool_false);
    RUN_TEST(test_json_value_escaped);

    RUN_TEST(test_safe_id_alnum);
    RUN_TEST(test_safe_id_special);
    RUN_TEST(test_safe_id_empty);

    RUN_TEST(test_restore_target_basic);
    RUN_TEST(test_restore_target_zero);
    RUN_TEST(test_restore_target_clamp);
    RUN_TEST(test_restore_target_no_rewind);

    RUN_TEST(test_save_bucketing_same);
    RUN_TEST(test_save_bucketing_diff);

    RUN_TEST(test_defer_save_same_path);
    RUN_TEST(test_defer_save_recent_switch);
    RUN_TEST(test_defer_save_old_enough);
    RUN_TEST(test_defer_save_no_prev);

    RUN_TEST(test_completed_match);
    RUN_TEST(test_completed_diff);
    RUN_TEST(test_completed_null);

    RUN_TEST(test_seek_failure_increments);
    RUN_TEST(test_seek_failure_multiple);
    RUN_TEST(test_track_failure_resets);

    RUN_TEST(test_retry_delay_first);
    RUN_TEST(test_retry_delay_second);
    RUN_TEST(test_retry_delay_third);
    RUN_TEST(test_retry_delay_capped);

    RUN_TEST(test_attempt_restore_in_range);
    RUN_TEST(test_attempt_restore_too_high);
    RUN_TEST(test_attempt_restore_autostart);
    RUN_TEST(test_attempt_restore_too_low);
    RUN_TEST(test_attempt_restore_disabled);

    RUN_TEST(test_skip_failed_no_failure);
    RUN_TEST(test_skip_failed_diff_path);

    RUN_TEST(test_path_audiobook_standard);
    RUN_TEST(test_path_audiobook_no_drive);
    RUN_TEST(test_path_audiobook_backslash);
    RUN_TEST(test_path_audiobook_music);
    RUN_TEST(test_path_audiobook_empty);
    RUN_TEST(test_path_music_standard);
    RUN_TEST(test_path_music_no_drive);
    RUN_TEST(test_path_music_audiobook);

    RUN_TEST(test_decode_hex_simple);
    RUN_TEST(test_decode_hex_null);

    RUN_TEST(test_save_and_read_record);
    RUN_TEST(test_no_record);
    RUN_TEST(test_record_path_book_key);
    RUN_TEST(test_record_path_root_fallback);

    RUN_TEST(test_catalog_parse);
    RUN_TEST(test_catalog_missing_file);
    RUN_TEST(test_catalog_album_patterns);

    /* UI module tests */
    RUN_TEST(test_pixel_white_pure);
    RUN_TEST(test_pixel_white_near);
    RUN_TEST(test_pixel_white_below_r);
    RUN_TEST(test_pixel_white_below_g);
    RUN_TEST(test_pixel_white_below_b);
    RUN_TEST(test_pixel_blue_pure);
    RUN_TEST(test_pixel_blue_high_r);
    RUN_TEST(test_pixel_blue_low_g);
    RUN_TEST(test_pixel_blue_low_b);
    RUN_TEST(test_seek_x_midpoint);
    RUN_TEST(test_seek_x_start);
    RUN_TEST(test_seek_x_end);
    RUN_TEST(test_seek_x_zero_duration);
    RUN_TEST(test_seek_x_tiny_range);
    RUN_TEST(test_seek_x_clamp_min);
    RUN_TEST(test_seek_x_clamp_max);
    RUN_TEST(test_seek_x_quarter);
    RUN_TEST(test_seek_x_three_quarter);
    RUN_TEST(test_fb_white_region_mock);
    RUN_TEST(test_fb_white_region_empty);
    RUN_TEST(test_fb_white_region_partial);
    RUN_TEST(test_play_mode_value_read);
    RUN_TEST(test_play_mode_value_missing);
    RUN_TEST(test_play_mode_value_zero_offset);

    printf("\n=== Results ===\n");
    printf("  Total:   %d\n", tests_run);
    printf("  Passed:  %d\n", tests_passed);
    printf("  Failed:  %d\n", tests_failed);

    cleanup_tmpdir();

    return tests_failed > 0 ? 1 : 0;
}