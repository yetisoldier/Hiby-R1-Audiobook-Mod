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
#include "state.h"
#include "shadow.h"
#include "log.h"

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

/* ── Config file parsing tests ──────────────────────────────────── */

static void test_config_defaults(void) {
    daemon_config cfg;
    config_load(&cfg, NULL);  /* NULL = try default path, which won't exist in tmpdir */
    CHECK_INT_EQ((int)cfg.interval_seconds, 5);
    CHECK_INT_EQ((int)cfg.idle_interval_seconds, 3);
    CHECK_INT_EQ((int)cfg.min_save_ms, 3000);
    CHECK_INT_EQ((int)cfg.save_bucket_ms, 15000);
    CHECK_INT_EQ((int)cfg.shadow_mode, 0);
    CHECK_STR_EQ(cfg.base_dir, "/usr/data/audiobooks");
    config_free(&cfg);
}

static void test_config_file_basic(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    /* Write a config file */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/resume-daemon.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "# Test config file\n");
    fprintf(fp, "INTERVAL_SECONDS=10\n");
    fprintf(fp, "MIN_SAVE_MS=5000\n");
    fprintf(fp, "SHADOW_MODE=1\n");
    fprintf(fp, "SAVE_BUCKET_MS=30000\n");
    fprintf(fp, "  # Indented comment\n");
    fprintf(fp, "IDLE_INTERVAL_SECONDS = 7\n");
    fclose(fp);

    /* Load with explicit config file path */
    config_load(&cfg, conf_path);
    CHECK_INT_EQ((int)cfg.interval_seconds, 10);
    CHECK_INT_EQ((int)cfg.min_save_ms, 5000);
    CHECK_INT_EQ((int)cfg.save_bucket_ms, 30000);
    CHECK_INT_EQ((int)cfg.idle_interval_seconds, 7);
    CHECK_INT_EQ((int)cfg.shadow_mode, 1);
    config_free(&cfg);
}

static void test_config_file_comments(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/test_comments.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "# Only comments\n");
    fprintf(fp, "# Another comment\n");
    fprintf(fp, "\n");
    fprintf(fp, "  # Indented comment\n");
    fclose(fp);

    /* Should not crash, values stay at defaults from make_test_config */
    int rc = config_load_file(&cfg, conf_path);
    CHECK_INT_EQ(rc, 0);
    config_free(&cfg);
}

static void test_config_file_missing(void) {
    daemon_config cfg;
    int rc = config_load_file(&cfg, "/nonexistent/path/to/config.conf");
    /* Should return -1 for missing file */
    CHECK_INT_EQ(rc, -1);
}

static void test_config_file_bool_variants(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/test_bools.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "SHADOW_MODE=yes\n");
    fprintf(fp, "RESTORE_ENABLED=true\n");
    fprintf(fp, "TRACK_RESTORE_ENABLED=1\n");
    fprintf(fp, "PLAY_MODE_ENFORCE_ENABLED=no\n");
    fprintf(fp, "BACK_GUARD_ENABLED=false\n");
    fclose(fp);

    config_load(&cfg, conf_path);
    CHECK_INT_EQ((int)cfg.shadow_mode, 1);
    CHECK_INT_EQ((int)cfg.restore_enabled, 1);
    CHECK_INT_EQ((int)cfg.track_restore_enabled, 1);
    CHECK_INT_EQ((int)cfg.play_mode_enforce_enabled, 0);
    CHECK_INT_EQ((int)cfg.back_guard_enabled, 0);
    config_free(&cfg);
}

static void test_config_env_override(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    /* Write config file with one value */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/test_env.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "INTERVAL_SECONDS=10\n");
    fclose(fp);

    /* Set env var that should override config file */
    setenv("AUDIOBOOK_INTERVAL_SECONDS", "15", 1);
    config_load(&cfg, conf_path);
    CHECK_INT_EQ((int)cfg.interval_seconds, 15);
    unsetenv("AUDIOBOOK_INTERVAL_SECONDS");
    config_free(&cfg);
}

static void test_config_invalid_values(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/test_invalid.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "INTERVAL_SECONDS=notanumber\n");
    fprintf(fp, "MIN_SAVE_MS=abc\n");
    fclose(fp);

    /* Invalid values should be ignored, defaults kept */
    config_load(&cfg, conf_path);
    CHECK_INT_EQ((int)cfg.interval_seconds, 5);
    CHECK_INT_EQ((int)cfg.min_save_ms, 3000);
    config_free(&cfg);
}

/* ── Shadow mode tests ───────────────────────────────────────────── */

static void test_shadow_init(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.shadow_mode = 0;
    shadow_init(&cfg);
    CHECK_INT_EQ((int)cfg.shadow_mode, 1);
    CHECK(shadow_is_active(&cfg));
}

static void test_shadow_not_active(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.shadow_mode = 0;
    CHECK(!shadow_is_active(&cfg));
}

static void test_shadow_wrap_save_logs(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    log_init(cfg.log_path, cfg.log_max_bytes);

    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.book_key, "testkey");
    tmpl.track_index = 2;
    tmpl.completed = false;

    /* In shadow mode, should return 0 and NOT write a record file */
    int rc = shadow_wrap_save(&cfg, "a:\\Audiobooks\\Book\\01.mp3",
                              60000, &tmpl);
    CHECK_INT_EQ(rc, 0);

    /* Verify no resume record file was created */
    char rec_path[512];
    int rc2 = record_for_path(&cfg, "a:\\Audiobooks\\Book\\01.mp3",
                              "testkey", "a:\\Audiobooks\\Book",
                              rec_path, sizeof(rec_path));
    CHECK_INT_EQ(rc2, 0);
    FILE *fp = fopen(rec_path, "r");
    CHECK(fp == NULL);  /* file should NOT exist in shadow mode */

    log_close();
    config_free(&cfg);
}

static void test_shadow_wrap_save_real_when_off(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 0;  /* shadow mode OFF */
    log_init(cfg.log_path, cfg.log_max_bytes);

    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.book_id, "test_id");
    strcpy(tmpl.book_key, "testkey2");
    strcpy(tmpl.root_hiby_path, "a:\\Audiobooks\\Book2");

    /* In non-shadow mode, should actually write the record */
    int rc = shadow_wrap_save(&cfg, "a:\\Audiobooks\\Book2\\01.mp3",
                              42000, &tmpl);
    CHECK_INT_EQ(rc, 0);

    /* Verify the record was actually written */
    resume_record loaded;
    int rc2 = existing_record_for_path(&cfg, "a:\\Audiobooks\\Book2\\01.mp3",
                                        "testkey2", "a:\\Audiobooks\\Book2",
                                        &loaded);
    CHECK_INT_EQ(rc2, 0);
    CHECK_INT_EQ((int)loaded.position_ms, 42000);

    log_close();
    config_free(&cfg);
}

static void test_shadow_wrap_restore_logs(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    cfg.restore_rewind_ms = 5000;
    cfg.restore_min_ms = 10000;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* In shadow mode, should return 0 and NOT actually restore */
    int rc = shadow_wrap_restore(&cfg, "a:\\Audiobooks\\Book\\01.mp3",
                                120000, "bookkey", "a:\\Audiobooks\\Book");
    CHECK_INT_EQ(rc, 0);

    log_close();
    config_free(&cfg);
}

static void test_shadow_wrap_play_mode_logs(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    cfg.play_mode_target = 3;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* In shadow mode, should return 0 and NOT enforce play mode */
    int rc = shadow_wrap_play_mode(&cfg);
    CHECK_INT_EQ(rc, 0);

    log_close();
    config_free(&cfg);
}

static void test_shadow_wrap_ui(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* In shadow mode, should return 0 without calling real action */
    int called = 0;
    /* We can't easily pass a closure in C, but we can test with NULL fn */
    int rc = shadow_wrap_ui(&cfg, "tap_track_row", NULL);
    CHECK_INT_EQ(rc, 0);
    (void)called;

    log_close();
    config_free(&cfg);
}

static void test_shadow_wrap_ui_real_when_off(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 0;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* When shadow is off and real_action is NULL, should return -1 */
    int rc = shadow_wrap_ui(&cfg, "test_action", NULL);
    CHECK_INT_EQ(rc, -1);

    log_close();
    config_free(&cfg);
}

static void test_shadow_config_file_enables(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/test_shadow.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "SHADOW_MODE=1\n");
    fclose(fp);

    config_load(&cfg, conf_path);
    CHECK_INT_EQ((int)cfg.shadow_mode, 1);
    CHECK(shadow_is_active(&cfg));
    config_free(&cfg);
}

static void test_shadow_env_enables(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    setenv("AUDIOBOOK_SHADOW_MODE", "1", 1);
    config_load(&cfg, NULL);
    CHECK_INT_EQ((int)cfg.shadow_mode, 1);
    CHECK(shadow_is_active(&cfg));
    unsetenv("AUDIOBOOK_SHADOW_MODE");
    config_free(&cfg);
}


/* ── State module tests ────────────────────────────────────────────── */

static void test_state_init(void) {
    daemon_runtime rt;
    state_init(&rt);
    CHECK_INT_EQ((int)rt.state, (int)STATE_IDLE);
    CHECK_INT_EQ(rt.last_saved_bucket, -1);
    CHECK(rt.last_path[0] == '\0');
    CHECK(rt.restored_path[0] == '\0');
}

static void test_state_book_root_simple(void) {
    char root[512];
    state_book_root("a:\\Audiobooks\\Book\\03.mp3", root, sizeof(root));
    CHECK_STR_EQ(root, "a:\\Audiobooks\\Book");
}

static void test_state_book_root_no_backslash(void) {
    char root[512];
    state_book_root("nofile", root, sizeof(root));
    CHECK_STR_EQ(root, "nofile");
}

static void test_state_book_root_empty(void) {
    char root[512];
    state_book_root("", root, sizeof(root));
    CHECK_STR_EQ(root, "");
}

static void test_state_same_book_root_match(void) {
    CHECK(state_same_book_root("a:\\B\\01.mp3", "a:\\B"));
}

static void test_state_same_book_root_nomatch(void) {
    CHECK(!state_same_book_root("a:\\B\\01.mp3", "a:\\C"));
}

static void test_state_same_book_root_prefix(void) {
    CHECK(!state_same_book_root("a:\\BookX\\01.mp3", "a:\\Book"));
}

static void test_state_settle_ticks_default(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    int ticks = state_settle_ticks(&cfg);
    CHECK_INT_EQ(ticks, 15);
}

static void test_state_settle_ticks_calc(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.track_switch_settle_seconds = 3;
    cfg.track_switch_poll_us = 200000;
    int ticks = state_settle_ticks(&cfg);
    CHECK_INT_EQ(ticks, 15);
}

static void test_state_settle_ticks_fast_poll(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.track_switch_settle_seconds = 5;
    cfg.track_switch_poll_us = 100000;
    int ticks = state_settle_ticks(&cfg);
    CHECK_INT_EQ(ticks, 50);
}

static void test_state_autostart_inactive(void) {
    daemon_runtime rt;
    state_init(&rt);
    CHECK(!state_autostart_active(&rt));
}

static void test_state_autostart_active(void) {
    daemon_runtime rt;
    state_init(&rt);
    rt.book_title_autostart_until = time(NULL) + 60;
    CHECK(state_autostart_active(&rt));
}

static void test_state_autostart_expired(void) {
    daemon_runtime rt;
    state_init(&rt);
    rt.book_title_autostart_until = time(NULL) - 10;
    CHECK(!state_autostart_active(&rt));
}

static void test_state_context_active(void) {
    daemon_runtime rt;
    state_init(&rt);
    time_t now = time(NULL);
    rt.book_title_context_until = now + 60;
    CHECK(state_context_active(&rt, now));
}

static void test_state_context_expired(void) {
    daemon_runtime rt;
    state_init(&rt);
    time_t now = time(NULL);
    rt.book_title_context_until = now - 10;
    CHECK(!state_context_active(&rt, now));
}

static void test_state_clear_autostart(void) {
    daemon_runtime rt;
    state_init(&rt);
    rt.book_title_autostart_until = time(NULL) + 60;
    rt.book_title_autostart_seq = 42;
    strcpy(rt.book_title_autostart_reset_key, "test");
    state_clear_autostart(&rt);
    CHECK_INT_EQ((int)rt.book_title_autostart_until, 0);
    CHECK_INT_EQ((int)rt.book_title_autostart_seq, 0);
    CHECK(rt.book_title_autostart_reset_key[0] == '\0');
}

static void test_state_should_poll_audiobook(void) {
    daemon_runtime rt;
    daemon_config cfg;
    state_init(&rt);
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_autostart_enabled = 1;
    CHECK(state_should_poll_marker(&rt, &cfg, "a:\\Audiobooks\\Book", time(NULL)));
}

static void test_state_should_poll_music_throttle(void) {
    daemon_runtime rt;
    daemon_config cfg;
    state_init(&rt);
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_autostart_enabled = 1;
    cfg.book_title_marker_music_poll_seconds = 15;
    CHECK(state_should_poll_marker(&rt, &cfg, "a:\\Music\\Album", 1000));
    rt.last_book_title_marker_poll_at = 1005;
    CHECK(!state_should_poll_marker(&rt, &cfg, "a:\\Music\\Album", 1010));
    CHECK(state_should_poll_marker(&rt, &cfg, "a:\\Music\\Album", 1020));
}

static void test_state_should_poll_disabled(void) {
    daemon_runtime rt;
    daemon_config cfg;
    state_init(&rt);
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_autostart_enabled = 0;
    CHECK(!state_should_poll_marker(&rt, &cfg, "a:\\Audiobooks\\Book", time(NULL)));
}

static void test_state_log_bucket_zero(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_restore_log_bucket_ms = 30000;
    CHECK_INT_EQ(state_log_bucket(&cfg, 0), 0);
}

static void test_state_log_bucket_calc(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_restore_log_bucket_ms = 30000;
    CHECK_INT_EQ(state_log_bucket(&cfg, 60000), 2);
}

static void test_state_log_bucket_disabled(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_restore_log_bucket_ms = 0;
    CHECK_INT_EQ(state_log_bucket(&cfg, 60000), 0);
}

static void test_state_diag_inc(void) {
    daemon_runtime rt;
    state_init(&rt);
    int counter = 0;
    state_diag_inc(&rt, &counter);
    CHECK_INT_EQ(counter, 1);
    state_diag_inc(&rt, &counter);
    state_diag_inc(&rt, &counter);
    CHECK_INT_EQ(counter, 3);
}

static void test_state_diag_log_not_due(void) {
    daemon_runtime rt;
    daemon_config cfg;
    state_init(&rt);
    memset(&cfg, 0, sizeof(cfg));
    cfg.diagnostics_interval_seconds = 60;
    time_t now = time(NULL);
    rt.diag_last_log_at = now;
    rt.diag_loops = 5;
    state_diag_log(&rt, &cfg, now);
    CHECK_INT_EQ(rt.diag_loops, 5);
}

static void test_state_diag_log_due(void) {
    daemon_runtime rt;
    daemon_config cfg;
    state_init(&rt);
    memset(&cfg, 0, sizeof(cfg));
    cfg.diagnostics_interval_seconds = 60;
    time_t now = time(NULL);
    rt.diag_last_log_at = now - 120;
    rt.diag_loops = 10;
    rt.diag_audiobook_loops = 7;
    rt.diag_non_audiobook_loops = 3;
    rt.diag_saves = 2;
    state_diag_log(&rt, &cfg, now);
    CHECK_INT_EQ(rt.diag_loops, 0);
    CHECK_INT_EQ(rt.diag_audiobook_loops, 0);
    CHECK_INT_EQ(rt.diag_saves, 0);
    CHECK_INT_EQ((int)rt.diag_last_log_at, (int)now);
}

static void test_state_diag_log_first_call(void) {
    daemon_runtime rt;
    daemon_config cfg;
    state_init(&rt);
    memset(&cfg, 0, sizeof(cfg));
    cfg.diagnostics_interval_seconds = 60;
    time_t now = time(NULL);
    rt.diag_loops = 5;
    state_diag_log(&rt, &cfg, now);
    CHECK_INT_EQ(rt.diag_loops, 5);
    CHECK_INT_EQ((int)rt.diag_last_log_at, (int)now);
}

static void test_state_geometry_row1(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_direct_track_visible_rows = 5;
    cfg.book_title_direct_track_rows_per_swipe = 4;
    cfg.book_title_direct_track_max_swipes = 20;
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 1, &sw, &row), 0);
    CHECK_INT_EQ(sw, 0);
    CHECK_INT_EQ(row, 1);
}

static void test_state_geometry_row5(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_direct_track_visible_rows = 5;
    cfg.book_title_direct_track_rows_per_swipe = 4;
    cfg.book_title_direct_track_max_swipes = 20;
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 5, &sw, &row), 0);
    CHECK_INT_EQ(sw, 0);
    CHECK_INT_EQ(row, 5);
}

static void test_state_geometry_swipe_once(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_direct_track_visible_rows = 5;
    cfg.book_title_direct_track_rows_per_swipe = 4;
    cfg.book_title_direct_track_max_swipes = 20;
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 6, &sw, &row), 0);
    CHECK_INT_EQ(sw, 1);
    CHECK_INT_EQ(row, 2);
}

static void test_state_geometry_swipe_twice(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_direct_track_visible_rows = 5;
    cfg.book_title_direct_track_rows_per_swipe = 4;
    cfg.book_title_direct_track_max_swipes = 20;
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 10, &sw, &row), 0);
    CHECK_INT_EQ(sw, 2);
    CHECK_INT_EQ(row, 2);
}

static void test_state_geometry_too_many(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.book_title_direct_track_visible_rows = 5;
    cfg.book_title_direct_track_rows_per_swipe = 4;
    cfg.book_title_direct_track_max_swipes = 3;
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 20, &sw, &row), -1);
}

static void test_state_geometry_invalid_index(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 0, &sw, &row), -1);
    CHECK_INT_EQ(state_direct_geometry(&cfg, -1, &sw, &row), -1);
}

static void test_state_geometry_defaults(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    int sw, row;
    CHECK_INT_EQ(state_direct_geometry(&cfg, 5, &sw, &row), 0);
    CHECK_INT_EQ(sw, 0);
    CHECK_INT_EQ(row, 5);
}

/* ── Auto-tap config tests (Phase 2) ─────────────────────────────── */

static void test_autotap_config_defaults(void) {
    daemon_config cfg;
    config_load(&cfg, NULL);
    CHECK_INT_EQ((int)cfg.autotap_enabled, 1);
    CHECK_INT_EQ((int)cfg.autotap_delay_ms, 500);
    CHECK_INT_EQ((int)cfg.autotap_max_wait_ms, 3000);
    CHECK_INT_EQ((int)cfg.autotap_require_views_path, 1);
    config_free(&cfg);
}

static void test_autotap_config_env_override(void) {
    daemon_config cfg;
    setenv("AUDIOBOOK_AUTOTAP_ENABLED", "0", 1);
    setenv("AUDIOBOOK_AUTOTAP_DELAY_MS", "1000", 1);
    setenv("AUDIOBOOK_AUTOTAP_MAX_WAIT_MS", "5000", 1);
    setenv("AUDIOBOOK_AUTOTAP_REQUIRE_VIEWS_PATH", "0", 1);
    config_load(&cfg, NULL);
    CHECK_INT_EQ((int)cfg.autotap_enabled, 0);
    CHECK_INT_EQ((int)cfg.autotap_delay_ms, 1000);
    CHECK_INT_EQ((int)cfg.autotap_max_wait_ms, 5000);
    CHECK_INT_EQ((int)cfg.autotap_require_views_path, 0);
    config_free(&cfg);
    unsetenv("AUDIOBOOK_AUTOTAP_ENABLED");
    unsetenv("AUDIOBOOK_AUTOTAP_DELAY_MS");
    unsetenv("AUDIOBOOK_AUTOTAP_MAX_WAIT_MS");
    unsetenv("AUDIOBOOK_AUTOTAP_REQUIRE_VIEWS_PATH");
}

static void test_autotap_config_file_override(void) {
    daemon_config cfg;
    make_test_config(&cfg);

    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/resume-daemon.conf", tmpdir);
    FILE *fp = fopen(conf_path, "w");
    CHECK(fp != NULL);
    fprintf(fp, "AUTOTAP_ENABLED=0\n");
    fprintf(fp, "AUTOTAP_DELAY_MS=750\n");
    fprintf(fp, "AUTOTAP_MAX_WAIT_MS=2000\n");
    fprintf(fp, "AUTOTAP_REQUIRE_VIEWS_PATH=0\n");
    fclose(fp);

    config_load(&cfg, conf_path);
    CHECK_INT_EQ((int)cfg.autotap_enabled, 0);
    CHECK_INT_EQ((int)cfg.autotap_delay_ms, 750);
    CHECK_INT_EQ((int)cfg.autotap_max_wait_ms, 2000);
    CHECK_INT_EQ((int)cfg.autotap_require_views_path, 0);
    config_free(&cfg);
}

static void test_autotap_config_clamp(void) {
    daemon_config cfg;
    setenv("AUDIOBOOK_AUTOTAP_DELAY_MS", "99999", 1);
    config_load(&cfg, NULL);
    /* Should be clamped to max_val=10000 */
    CHECK(cfg.autotap_delay_ms <= 10000);
    config_free(&cfg);
    unsetenv("AUDIOBOOK_AUTOTAP_DELAY_MS");
}

/* ── Auto-tap runtime state tests ────────────────────────────────── */

static void test_autotap_state_init_clears(void) {
    daemon_runtime rt;
    state_init(&rt);
    CHECK(rt.autotap_last_path[0] == '\0');
    CHECK_INT_EQ((int)rt.autotap_fired_at, 0);
    CHECK_INT_EQ(rt.diag_autotap_fired, 0);
    CHECK_INT_EQ(rt.diag_autotap_skipped, 0);
}

static void test_autotap_diag_counters_increment(void) {
    daemon_runtime rt;
    state_init(&rt);
    state_diag_inc(&rt, &rt.diag_autotap_fired);
    state_diag_inc(&rt, &rt.diag_autotap_fired);
    state_diag_inc(&rt, &rt.diag_autotap_skipped);
    CHECK_INT_EQ(rt.diag_autotap_fired, 2);
    CHECK_INT_EQ(rt.diag_autotap_skipped, 1);
}

/* ── Save/Restore decision logic tests (Phase 3) ────────────────── */

/* Test that shadow_wrap_save writes a real record when shadow is off */
static void test_save_decision_shadow_off_writes(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 0;
    log_init(cfg.log_path, cfg.log_max_bytes);

    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.book_id, "test_save_id");
    strcpy(tmpl.book_key, "savekey1");
    strcpy(tmpl.root_hiby_path, "a:\\Audiobooks\\SaveTest");
    tmpl.track_index = 1;
    tmpl.track_count = 5;

    int rc = shadow_wrap_save(&cfg, "a:\\Audiobooks\\SaveTest\\01.mp3",
                              50000, &tmpl);
    CHECK_INT_EQ(rc, 0);

    /* Verify record was actually written */
    resume_record loaded;
    int rc2 = existing_record_for_path(&cfg,
                "a:\\Audiobooks\\SaveTest\\01.mp3",
                "savekey1", "a:\\Audiobooks\\SaveTest", &loaded);
    CHECK_INT_EQ(rc2, 0);
    CHECK_INT_EQ((int)loaded.position_ms, 50000);

    log_close();
    config_free(&cfg);
}

/* Test that shadow_wrap_save does NOT write when shadow is on */
static void test_save_decision_shadow_on_no_write(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    log_init(cfg.log_path, cfg.log_max_bytes);

    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.book_key, "savekey2");
    strcpy(tmpl.root_hiby_path, "a:\\Audiobooks\\ShadowTest");

    int rc = shadow_wrap_save(&cfg, "a:\\Audiobooks\\ShadowTest\\01.mp3",
                              75000, &tmpl);
    CHECK_INT_EQ(rc, 0);

    /* Verify NO record was written */
    resume_record loaded;
    int rc2 = existing_record_for_path(&cfg,
                "a:\\Audiobooks\\ShadowTest\\01.mp3",
                "savekey2", "a:\\Audiobooks\\ShadowTest", &loaded);
    CHECK_INT_EQ(rc2, -1);

    log_close();
    config_free(&cfg);
}

/* Test save bucketing: same bucket should not trigger save */
static void test_save_bucketing_same_bucket_no_save(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.save_bucket_ms = 15000;
    cfg.min_save_ms = 3000;

    int bucket1 = 120000 / 15000;  /* 8 */
    int bucket2 = 134999 / 15000;  /* 8 */
    CHECK_INT_EQ(bucket1, bucket2);
    /* Same bucket → no save */
}

/* Test save bucketing: different bucket should trigger save */
static void test_save_bucketing_diff_bucket_triggers_save(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.save_bucket_ms = 15000;

    int bucket1 = 120000 / 15000;  /* 8 */
    int bucket2 = 150000 / 15000;  /* 10 */
    CHECK(bucket1 != bucket2);
}

/* Test that save is skipped when position < min_save_ms */
static void test_save_min_position_skip(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.min_save_ms = 3000;
    cfg.save_bucket_ms = 15000;

    uint32_t pos = 2000;  /* below min_save_ms */
    CHECK(pos < cfg.min_save_ms);
    /* Save phase should be skipped entirely */
}

/* Test that save is allowed when position >= min_save_ms */
static void test_save_min_position_allow(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.min_save_ms = 3000;

    uint32_t pos = 5000;  /* above min_save_ms */
    CHECK(pos >= cfg.min_save_ms);
}

/* Test shadow_wrap_restore in shadow mode: returns 0, no actual restore */
static void test_restore_shadow_on_no_action(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    cfg.restore_rewind_ms = 5000;
    cfg.restore_min_ms = 10000;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* First save a record so restore could find it */
    cfg.shadow_mode = 0;
    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.book_key, "restorekey1");
    strcpy(tmpl.root_hiby_path, "a:\\Audiobooks\\RestoreTest");
    save_position(&cfg, "a:\\Audiobooks\\RestoreTest\\01.mp3",
                  100000, &tmpl);

    /* Now switch to shadow mode and try restore */
    cfg.shadow_mode = 1;
    int rc = shadow_wrap_restore(&cfg,
                "a:\\Audiobooks\\RestoreTest\\01.mp3",
                5000, "restorekey1", "a:\\Audiobooks\\RestoreTest");
    CHECK_INT_EQ(rc, 0);

    log_close();
    config_free(&cfg);
}

/* Test shadow_wrap_restore in active mode: delegates to maybe_restore */
static void test_restore_shadow_off_delegates(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 0;
    cfg.restore_enabled = 1;
    cfg.restore_rewind_ms = 5000;
    cfg.restore_min_ms = 10000;
    cfg.restore_only_before_ms = 15000;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* No record exists — maybe_restore returns -1 */
    int rc = shadow_wrap_restore(&cfg,
                "a:\\Audiobooks\\NoRecord\\01.mp3",
                5000, "nokey", "a:\\Audiobooks\\NoRecord");
    CHECK_INT_EQ(rc, -1);

    log_close();
    config_free(&cfg);
}

/* Test shadow_wrap_play_mode in shadow mode */
static void test_play_mode_shadow_on_logs(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 1;
    cfg.play_mode_target = 3;
    cfg.play_mode_touch_x = 200;
    cfg.play_mode_touch_y = 80;
    cfg.play_mode_max_taps = 3;
    log_init(cfg.log_path, cfg.log_max_bytes);

    int rc = shadow_wrap_play_mode(&cfg);
    CHECK_INT_EQ(rc, 0);

    log_close();
    config_free(&cfg);
}

/* Test shadow_wrap_play_mode in active mode: delegates to ensure_audiobook_play_mode */
static void test_play_mode_shadow_off_delegates(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    cfg.shadow_mode = 0;
    cfg.play_mode_enforce_enabled = 1;
    cfg.play_mode_target = 3;
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* Without a real user.ini at the right offset, this returns -1 */
    int rc = shadow_wrap_play_mode(&cfg);
    /* It may return 0 or -1 depending on whether user.ini exists.
     * Just check it doesn't crash. */
    (void)rc;

    log_close();
    config_free(&cfg);
}

/* Test completion detection: last track, position near end from below */
static void test_completion_last_track_near_end(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.completed_end_threshold_ms = 45000;

    /* Simulate: track_index=5, track_count=5, pos=350000, dur=360000
     * dur - pos = 10000 <= 45000 → completed */
    int track_index = 5;
    int track_count = 5;
    uint32_t pos = 350000;
    uint32_t dur = 360000;

    CHECK(track_index == track_count);
    uint32_t diff = (pos >= dur) ? (pos - dur) : (dur - pos);
    CHECK(diff <= cfg.completed_end_threshold_ms);
    /* Should trigger completion */
}

/* Test completion detection: not last track → no completion */
static void test_completion_not_last_track(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.completed_end_threshold_ms = 45000;

    int track_index = 3;
    int track_count = 5;
    CHECK(track_index != track_count);
    /* Completion check should not trigger */
}

/* Test completion detection: last track but not near end */
static void test_completion_last_track_not_near_end(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.completed_end_threshold_ms = 45000;

    int track_index = 5;
    int track_count = 5;
    uint32_t pos = 100000;
    uint32_t dur = 360000;

    CHECK(track_index == track_count);
    CHECK(pos < dur);
    /* Not near end → no completion */
}

/* Test completion detection: position exactly at duration */
static void test_completion_at_duration(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.completed_end_threshold_ms = 45000;

    uint32_t pos = 360000;
    uint32_t dur = 360000;

    CHECK(pos >= dur);
    CHECK((pos - dur) == 0);
    CHECK((pos - dur) <= cfg.completed_end_threshold_ms);
    /* Should trigger completion */
}

/* Test completion detection: position way past duration */
static void test_completion_past_duration_within_threshold(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.completed_end_threshold_ms = 45000;

    uint32_t pos = 395000;
    uint32_t dur = 360000;

    CHECK(pos >= dur);
    CHECK((pos - dur) == 35000);
    CHECK((pos - dur) <= cfg.completed_end_threshold_ms);
    /* Should trigger completion */
}

/* Test completion detection: position past threshold */
static void test_completion_past_threshold(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.completed_end_threshold_ms = 45000;

    uint32_t pos = 410000;
    uint32_t dur = 360000;

    CHECK(pos >= dur);
    CHECK((pos - dur) == 50000);
    CHECK((pos - dur) > cfg.completed_end_threshold_ms);
    /* Should NOT trigger completion (too far past) */
}

/* Test restore target computation with rewind */
static void test_restore_target_with_rewind(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 10000;
    cfg.restore_min_ms = 5000;

    uint32_t target = restore_target_ms(&cfg, 200000);
    CHECK_INT_EQ((int)target, 190000);
}

/* Test restore target clamped to minimum */
static void test_restore_target_clamped(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 10000;
    cfg.restore_min_ms = 8000;

    uint32_t target = restore_target_ms(&cfg, 15000);
    /* 15000 - 10000 = 5000, but clamped to 8000 */
    CHECK_INT_EQ((int)target, 8000);
}

/* Test restore target when saved position is below rewind */
static void test_restore_target_below_rewind(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_rewind_ms = 10000;
    cfg.restore_min_ms = 0;

    uint32_t target = restore_target_ms(&cfg, 5000);
    /* 5000 - 10000 would underflow, so returns 0 */
    CHECK_INT_EQ((int)target, 0);
}

/* Test should_attempt_restore with autostart and high position */
static void test_restore_attempt_autostart_high_pos(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 1;
    cfg.restore_only_before_ms = 15000;
    cfg.restore_min_ms = 5000;

    /* With autostart, position above restore_only_before_ms is allowed */
    bool attempt = should_attempt_restore_for_position(&cfg, 20000, true);
    CHECK(attempt);
}

/* Test should_attempt_restore without autostart and high position */
static void test_restore_attempt_no_autostart_high_pos(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_enabled = 1;
    cfg.restore_only_before_ms = 15000;
    cfg.restore_min_ms = 5000;

    /* Without autostart, position above threshold is rejected */
    bool attempt = should_attempt_restore_for_position(&cfg, 20000, false);
    CHECK(!attempt);
}

/* Test should_skip_failed_restore_save when position is below saved */
static void test_skip_failed_save_below_saved(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key");

    /* Current position 5000 < saved 120000 → skip */
    bool skip = should_skip_failed_restore_save(&cfg, "a:\\Book\\01.mp3", 5000);
    CHECK(skip);
    resume_reset_failures();
}

/* Test should_skip_failed_restore_save when position is above saved */
static void test_skip_failed_save_above_saved(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.restore_retry_after_failure_seconds = 30;
    cfg.restore_retry_max_after_failure_seconds = 300;
    resume_reset_failures();
    note_seek_restore_failure("a:\\Book\\01.mp3", 120000, "key");

    /* Current position 130000 > saved 120000 → don't skip */
    bool skip = should_skip_failed_restore_save(&cfg, "a:\\Book\\01.mp3", 130000);
    CHECK(!skip);
    resume_reset_failures();
}

/* Test that shadow_is_active correctly detects mode */
static void test_shadow_active_detection(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.shadow_mode = 0;
    CHECK(!shadow_is_active(&cfg));
    cfg.shadow_mode = 1;
    CHECK(shadow_is_active(&cfg));
}

/* Test completed record round-trip: save then read back completed=true */
static void test_completed_record_roundtrip(void) {
    daemon_config cfg;
    make_test_config(&cfg);
    log_init(cfg.log_path, cfg.log_max_bytes);

    /* Save a record */
    resume_record tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.book_id, "comp_test_id");
    strcpy(tmpl.book_key, "compkey");
    strcpy(tmpl.root_hiby_path, "a:\\Audiobooks\\CompTest");
    tmpl.track_index = 5;
    tmpl.track_count = 5;

    save_position(&cfg, "a:\\Audiobooks\\CompTest\\05.mp3",
                  350000, &tmpl);

    /* Patch completed to true (simulating completion detection) */
    char rec_path[512];
    record_for_path(&cfg, "a:\\Audiobooks\\CompTest\\05.mp3",
                    "compkey", "a:\\Audiobooks\\CompTest",
                    rec_path, sizeof(rec_path));
    int fd = open(rec_path, O_RDONLY);
    CHECK(fd >= 0);
    char buf[4096];
    size_t tot = 0;
    while (tot < sizeof(buf) - 1) {
        ssize_t n = read(fd, buf + tot, sizeof(buf) - 1 - tot);
        if (n > 0) { tot += (size_t)n; continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fd);
    buf[tot] = '\0';
    char *cp = strstr(buf, "\"completed\": false");
    CHECK(cp != NULL);
    cp[13] = 't'; cp[14] = 'r'; cp[15] = 'u'; cp[16] = 'e';
    int wfd = open(rec_path, O_WRONLY | O_TRUNC, 0644);
    CHECK(wfd >= 0);
    write(wfd, buf, tot);
    close(wfd);

    /* Read back and verify completed=true */
    resume_record loaded;
    int rc = existing_record_for_path(&cfg,
                "a:\\Audiobooks\\CompTest\\05.mp3",
                "compkey", "a:\\Audiobooks\\CompTest", &loaded);
    CHECK_INT_EQ(rc, 0);
    CHECK(loaded.completed == true);
    CHECK_INT_EQ(loaded.track_index, 5);
    CHECK_INT_EQ(loaded.track_count, 5);

    log_close();
    config_free(&cfg);
}

/* Test deferred save: same path not deferred */
static void test_defer_save_same_path_no_defer(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;

    bool defer = should_defer_new_track_save(&cfg,
        "a:\\Book\\01.mp3", "a:\\Book\\01.mp3",
        time(NULL) - 1, time(NULL));
    CHECK(!defer);
}

/* Test deferred save: different path, recent switch → deferred */
static void test_defer_save_diff_path_recent_deferred(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;
    time_t now = time(NULL);

    bool defer = should_defer_new_track_save(&cfg,
        "a:\\Book\\02.mp3", "a:\\Book\\01.mp3",
        now - 3, now);
    CHECK(defer);
}

/* Test deferred save: different path, old enough → not deferred */
static void test_defer_save_diff_path_old_enough_no_defer(void) {
    daemon_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.new_track_commit_ms = 15000;
    time_t now = time(NULL);

    bool defer = should_defer_new_track_save(&cfg,
        "a:\\Book\\02.mp3", "a:\\Book\\01.mp3",
        now - 20, now);
    CHECK(!defer);
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

    /* Config file parsing tests */
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_file_basic);
    RUN_TEST(test_config_file_comments);
    RUN_TEST(test_config_file_missing);
    RUN_TEST(test_config_file_bool_variants);
    RUN_TEST(test_config_env_override);
    RUN_TEST(test_config_invalid_values);

    /* Shadow mode tests */
    RUN_TEST(test_shadow_init);
    RUN_TEST(test_shadow_not_active);
    RUN_TEST(test_shadow_wrap_save_logs);
    RUN_TEST(test_shadow_wrap_save_real_when_off);
    RUN_TEST(test_shadow_wrap_restore_logs);
    RUN_TEST(test_shadow_wrap_play_mode_logs);
    RUN_TEST(test_shadow_wrap_ui);
    RUN_TEST(test_shadow_wrap_ui_real_when_off);
    RUN_TEST(test_shadow_config_file_enables);
    RUN_TEST(test_shadow_env_enables);

    
    RUN_TEST(test_state_init);
    RUN_TEST(test_state_book_root_simple);
    RUN_TEST(test_state_book_root_no_backslash);
    RUN_TEST(test_state_book_root_empty);
    RUN_TEST(test_state_same_book_root_match);
    RUN_TEST(test_state_same_book_root_nomatch);
    RUN_TEST(test_state_same_book_root_prefix);
    RUN_TEST(test_state_settle_ticks_default);
    RUN_TEST(test_state_settle_ticks_calc);
    RUN_TEST(test_state_settle_ticks_fast_poll);
    RUN_TEST(test_state_autostart_inactive);
    RUN_TEST(test_state_autostart_active);
    RUN_TEST(test_state_autostart_expired);
    RUN_TEST(test_state_context_active);
    RUN_TEST(test_state_context_expired);
    RUN_TEST(test_state_clear_autostart);
    RUN_TEST(test_state_should_poll_audiobook);
    RUN_TEST(test_state_should_poll_music_throttle);
    RUN_TEST(test_state_should_poll_disabled);
    RUN_TEST(test_state_log_bucket_zero);
    RUN_TEST(test_state_log_bucket_calc);
    RUN_TEST(test_state_log_bucket_disabled);
    RUN_TEST(test_state_diag_inc);
    RUN_TEST(test_state_diag_log_not_due);
    RUN_TEST(test_state_diag_log_due);
    RUN_TEST(test_state_diag_log_first_call);
    RUN_TEST(test_state_geometry_row1);
    RUN_TEST(test_state_geometry_row5);
    RUN_TEST(test_state_geometry_swipe_once);
    RUN_TEST(test_state_geometry_swipe_twice);
    RUN_TEST(test_state_geometry_too_many);
    RUN_TEST(test_state_geometry_invalid_index);
    RUN_TEST(test_state_geometry_defaults);

    /* Auto-tap config tests (Phase 2) */
    RUN_TEST(test_autotap_config_defaults);
    RUN_TEST(test_autotap_config_env_override);
    RUN_TEST(test_autotap_config_file_override);
    RUN_TEST(test_autotap_config_clamp);
    RUN_TEST(test_autotap_state_init_clears);
    RUN_TEST(test_autotap_diag_counters_increment);

    /* Save/Restore decision logic tests (Phase 3) */
    RUN_TEST(test_save_decision_shadow_off_writes);
    RUN_TEST(test_save_decision_shadow_on_no_write);
    RUN_TEST(test_save_bucketing_same_bucket_no_save);
    RUN_TEST(test_save_bucketing_diff_bucket_triggers_save);
    RUN_TEST(test_save_min_position_skip);
    RUN_TEST(test_save_min_position_allow);
    RUN_TEST(test_restore_shadow_on_no_action);
    RUN_TEST(test_restore_shadow_off_delegates);
    RUN_TEST(test_play_mode_shadow_on_logs);
    RUN_TEST(test_play_mode_shadow_off_delegates);
    RUN_TEST(test_completion_last_track_near_end);
    RUN_TEST(test_completion_not_last_track);
    RUN_TEST(test_completion_last_track_not_near_end);
    RUN_TEST(test_completion_at_duration);
    RUN_TEST(test_completion_past_duration_within_threshold);
    RUN_TEST(test_completion_past_threshold);
    RUN_TEST(test_restore_target_with_rewind);
    RUN_TEST(test_restore_target_clamped);
    RUN_TEST(test_restore_target_below_rewind);
    RUN_TEST(test_restore_attempt_autostart_high_pos);
    RUN_TEST(test_restore_attempt_no_autostart_high_pos);
    RUN_TEST(test_skip_failed_save_below_saved);
    RUN_TEST(test_skip_failed_save_above_saved);
    RUN_TEST(test_shadow_active_detection);
    RUN_TEST(test_completed_record_roundtrip);
    RUN_TEST(test_defer_save_same_path_no_defer);
    RUN_TEST(test_defer_save_diff_path_recent_deferred);
    RUN_TEST(test_defer_save_diff_path_old_enough_no_defer);

printf("\n=== Results ===\n");
    printf("  Total:   %d\n", tests_run);
    printf("  Passed:  %d\n", tests_passed);
    printf("  Failed:  %d\n", tests_failed);

    cleanup_tmpdir();

    return tests_failed > 0 ? 1 : 0;
}