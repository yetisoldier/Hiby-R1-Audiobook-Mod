/* bookmark_sd.c — SD-primary bookmark store (see bookmark_sd.h).
 *
 * Mirrors posstore.h's discipline: one tiny file per book under POS_DIR,
 * temp-write-then-rename so a power cut can't corrupt an existing file, and
 * a no-op (return -1) if the SD is absent/read-only so a missing SD never
 * crashes the app — the bookmark just isn't persisted, same as a missing SD
 * loses new .pos writes today. */

#include "bookmark_sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

static void bm_dir_ensure(void) {
    /* Idempotent; ignore EEXIST. If the SD is gone/RO the fopen in
     * bm_write_all simply fails and the caller returns -1. */
    (void)mkdir(POS_DIR, 0777);
}

static void bm_path(char *buf, size_t n, int book_id, const char *ext) {
    snprintf(buf, n, "%s/%d.%s", POS_DIR, book_id, ext);
}

/* Sanitize a label for the tab-delimited line format: replace tab/newline/
 * control chars with space so a label can never split a line or break the
 * parser. Guarantee non-empty (fallback "Bookmark"). Cap at dst_len-1. */
static void bm_sanitize_label(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    size_t i = 0;
    if (src) {
        for (; i + 1 < dst_len && src[i]; i++) {
            unsigned char c = (unsigned char)src[i];
            dst[i] = (c == '\t' || c == '\n' || c == '\r' || c < 32 || c == 127)
                         ? ' ' : (char)c;
        }
    }
    dst[i] = '\0';
    if (i == 0) {
        strncpy(dst, "Bookmark", dst_len - 1);
        dst[dst_len - 1] = '\0';
    }
}

/* Parse one "created_at\ttrack_id\tpos\tbookpos\tlabel\n" line into bm.
 * Returns 1 on success, 0 on malformed (caller skips). %255[^\n] reads the
 * rest of the line as the label; labels are sanitized on write so they hold
 * no embedded tabs/newlines. */
static int bm_parse_line(const char *line, audiobook_bookmark_t *bm) {
    int created_at, track_id;
    long long pos, bookpos;
    char label[256];
    int n = sscanf(line, "%d\t%d\t%lld\t%lld\t%255[^\n]",
                   &created_at, &track_id, &pos, &bookpos, label);
    if (n < 5) return 0;
    memset(bm, 0, sizeof(*bm));
    bm->bookmark_id = created_at;
    bm->book_id = 0;                 /* not stored per line; filename carries it */
    bm->track_id = track_id;
    bm->position_ms = (int64_t)pos;
    bm->total_book_position_ms = (int64_t)bookpos;
    strncpy(bm->label, label, sizeof(bm->label) - 1);
    bm->label[sizeof(bm->label) - 1] = '\0';
    bm->created_at = created_at;
    bm->updated_at = created_at;
    return 1;
}

/* Read all bookmarks for a book into a malloc'd array in FILE ORDER (oldest
 * first, since new bookmarks are appended). *out_arr is set even when the
 * file is missing or empty (NULL then). Returns count, or -1 on alloc/read
 * error. */
static int bm_read_all(int book_id, audiobook_bookmark_t **out_arr) {
    *out_arr = NULL;
    if (book_id <= 0) return 0;
    char path[160];
    bm_path(path, sizeof path, book_id, "bm");
    FILE *f = fopen(path, "r");
    if (!f) return 0;                 /* no file yet = no bookmarks */

    int cap = 8, count = 0;
    audiobook_bookmark_t *arr = malloc(cap * sizeof(*arr));
    if (!arr) { fclose(f); return -1; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        audiobook_bookmark_t bm;
        if (!bm_parse_line(line, &bm)) continue;   /* skip malformed lines */
        if (count == cap) {
            int ncap = cap * 2;
            audiobook_bookmark_t *na = realloc(arr, ncap * sizeof(*arr));
            if (!na) { free(arr); fclose(f); return -1; }
            arr = na; cap = ncap;
        }
        arr[count++] = bm;
    }
    fclose(f);
    *out_arr = arr;
    return count;
}

/* Write the array (in the given order) to the .bm file atomically. count may
 * be 0 (creates an empty marker file). Labels are sanitized here so the
 * tab-delimited format stays intact regardless of where the row came from. */
static int bm_write_all(int book_id, const audiobook_bookmark_t *arr, int count) {
    bm_dir_ensure();
    char path[160], tmp[160];
    bm_path(path, sizeof path, book_id, "bm");
    bm_path(tmp,  sizeof tmp,  book_id, "bm.tmp");
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    for (int i = 0; i < count; i++) {
        char lab[256];
        bm_sanitize_label(lab, sizeof lab, arr[i].label);
        fprintf(f, "%d\t%d\t%lld\t%lld\t%s\n",
                arr[i].created_at, arr[i].track_id,
                (long long)arr[i].position_ms,
                (long long)arr[i].total_book_position_ms, lab);
    }
    fclose(f);
    /* Rename within one directory is a single exFAT metadata op, so the .bm
     * is never seen half-written. On rename failure drop the tmp and leave
     * the previous .bm intact. */
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int bookmark_save_sd(int book_id, int track_id, int64_t position_ms,
                     int64_t total_book_position_ms, const char *label,
                     int *out_created_at) {
    if (book_id <= 0) return -1;
    audiobook_bookmark_t *arr = NULL;
    int count = bm_read_all(book_id, &arr);
    if (count < 0) return -1;

    int created_at = (int)time(NULL);
    /* Uniqueness: bump until no existing line shares this created_at (only
     * matters if two marks are added within the same second). */
    for (;;) {
        int dup = 0;
        for (int i = 0; i < count; i++)
            if (arr[i].created_at == created_at) { dup = 1; break; }
        if (!dup) break;
        created_at++;
    }

    audiobook_bookmark_t *na = realloc(arr, (count + 1) * sizeof(*arr));
    if (!na) { free(arr); return -1; }
    arr = na;
    audiobook_bookmark_t *bm = &arr[count];
    memset(bm, 0, sizeof(*bm));
    bm->bookmark_id = created_at;
    bm->track_id = track_id;
    bm->position_ms = position_ms;
    bm->total_book_position_ms = total_book_position_ms;
    bm->created_at = created_at;
    bm->updated_at = created_at;
    /* Store the raw label here; bm_write_all sanitizes it for the file. */
    if (label) {
        strncpy(bm->label, label, sizeof(bm->label) - 1);
        bm->label[sizeof(bm->label) - 1] = '\0';
    } else {
        strcpy(bm->label, "Bookmark");
    }

    int rc = bm_write_all(book_id, arr, count + 1);
    free(arr);
    if (rc != 0) return -1;
    if (out_created_at) *out_created_at = created_at;
    return 0;
}

int bookmark_list_sd(int book_id,
                     int (*cb)(const audiobook_bookmark_t *bm, void *ctx),
                     void *ctx) {
    if (book_id <= 0) return 0;
    audiobook_bookmark_t *arr = NULL;
    int count = bm_read_all(book_id, &arr);
    if (count < 0) return -1;
    /* File order is oldest-first (append); list newest-first. */
    int reported = 0;
    for (int i = count - 1; i >= 0; i--) {
        reported++;
        if (cb && cb(&arr[i], ctx) != 0) break;
    }
    free(arr);
    return reported;
}

int bookmark_delete_sd(int book_id, int created_at) {
    if (book_id <= 0) return -1;
    audiobook_bookmark_t *arr = NULL;
    int count = bm_read_all(book_id, &arr);
    if (count < 0) return -1;
    int kept = 0;
    for (int i = 0; i < count; i++)
        if (arr[i].created_at != created_at)
            arr[kept++] = arr[i];

    int rc = 0;
    if (kept == 0) {
        /* File would be empty — drop it outright (and any stale tmp). */
        char path[160];
        bm_path(path, sizeof path, book_id, "bm");
        unlink(path);
        bm_path(path, sizeof path, book_id, "bm.tmp");
        unlink(path);
    } else {
        rc = bm_write_all(book_id, arr, kept);
    }
    free(arr);
    return rc;
}

int bookmark_file_exists_sd(int book_id) {
    if (book_id <= 0) return 0;
    char path[160];
    bm_path(path, sizeof path, book_id, "bm");
    return access(path, F_OK) == 0 ? 1 : 0;
}

int bookmark_migrate_sd(int book_id, const audiobook_bookmark_t *rows, int count) {
    if (book_id <= 0) return -1;
    if (count <= 0)
        return bm_write_all(book_id, NULL, 0);   /* empty marker file */
    audiobook_bookmark_t *arr = malloc(count * sizeof(*arr));
    if (!arr) return -1;
    /* rows arrive newest-first (DB ORDER BY created_at DESC); the .bm file is
     * oldest-first so bookmark_list_sd's reverse yields newest-first. */
    for (int i = 0; i < count; i++)
        arr[i] = rows[count - 1 - i];
    int rc = bm_write_all(book_id, arr, count);
    free(arr);
    return rc;
}

void bookmark_remove_book_sd(int book_id) {
    if (book_id <= 0) return;
    char path[160];
    bm_path(path, sizeof path, book_id, "bm");
    unlink(path);
    bm_path(path, sizeof path, book_id, "bm.tmp");
    unlink(path);
}