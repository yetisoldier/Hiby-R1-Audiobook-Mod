#include "scanner.h"
#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>

typedef struct scan_track {
    track_row row;
} scan_track;

typedef struct scan_book {
    book_row row;
    scan_track *tracks;
    size_t track_count;
    size_t track_cap;
} scan_book;

typedef struct scan_child_dir {
    char path[512];
    char name[256];
    bool disc_like;
} scan_child_dir;

static void free_scan_book(scan_book *b) {
    if (!b) return;
    free(b->tracks);
    memset(b, 0, sizeof(*b));
}

static bool is_disc_folder(const char *name) {
    return strncasecmp(name, "disc", 4) == 0 || strncasecmp(name, "disk", 4) == 0 ||
           strncasecmp(name, "cd", 2) == 0 || strncasecmp(name, "part", 4) == 0 ||
           strncasecmp(name, "volume", 6) == 0;
}

static int parse_disc_number(const char *name) {
    if (!name) return 1;
    while (*name && !isdigit((unsigned char)*name)) {
        name++;
    }
    int value = atoi(name);
    return value > 0 ? value : 1;
}

static const char *basename_ptr(const char *path) {
    const char *base = strrchr(path ? path : "", '/');
    return base ? base + 1 : (path ? path : "");
}

static int infer_track_number(const char *name) {
    const char *p = basename_ptr(name);
    while (*p && !isdigit((unsigned char)*p)) {
        p++;
    }
    if (!*p) return 0;
    return atoi(p);
}

static int natural_name_compare(const char *a, const char *b) {
    size_t ia = 0;
    size_t ib = 0;
    while (a && b && a[ia] && b[ib]) {
        unsigned char ca = (unsigned char)a[ia];
        unsigned char cb = (unsigned char)b[ib];
        if (isdigit(ca) && isdigit(cb)) {
            size_t a0 = ia;
            size_t b0 = ib;
            while (a[a0] == '0') a0++;
            while (b[b0] == '0') b0++;
            size_t ae = a0;
            size_t be = b0;
            while (isdigit((unsigned char)a[ae])) ae++;
            while (isdigit((unsigned char)b[be])) be++;
            size_t alen = ae - a0;
            size_t blen = be - b0;
            if (alen != blen) return alen < blen ? -1 : 1;
            for (size_t i = 0; i < alen; i++) {
                if (a[a0 + i] != b[b0 + i]) {
                    return (unsigned char)a[a0 + i] < (unsigned char)b[b0 + i] ? -1 : 1;
                }
            }
            size_t araw = ae - ia;
            size_t braw = be - ib;
            if (araw != braw) return araw < braw ? -1 : 1;
            ia = ae;
            ib = be;
            continue;
        }
        ca = (unsigned char)tolower(ca);
        cb = (unsigned char)tolower(cb);
        if (ca != cb) return ca < cb ? -1 : 1;
        ia++;
        ib++;
    }
    if (a && a[ia]) return 1;
    if (b && b[ib]) return -1;
    return 0;
}

static int compare_scan_track(const void *lhs, const void *rhs) {
    const scan_track *a = lhs;
    const scan_track *b = rhs;
    const char *a_name = basename_ptr(a->row.path);
    const char *b_name = basename_ptr(b->row.path);
    if (a->row.disc_number != b->row.disc_number) {
        return a->row.disc_number < b->row.disc_number ? -1 : 1;
    }
    if (a->row.track_number != b->row.track_number) {
        return a->row.track_number < b->row.track_number ? -1 : 1;
    }
    int natural = natural_name_compare(a_name, b_name);
    if (natural != 0) return natural;
    int alpha = strcmp(a->row.sort_title, b->row.sort_title);
    if (alpha != 0) return alpha < 0 ? -1 : 1;
    return strcmp(a->row.path, b->row.path);
}

static void normalize_title(char *dst, size_t dst_len, const char *src) {
    ab_copy_str(dst, dst_len, src);
}

static void derive_sort_title(char *dst, size_t dst_len, const char *title) {
    size_t i = 0;
    for (; title && title[i] && i + 1 < dst_len; i++) {
        dst[i] = (char)tolower((unsigned char)title[i]);
    }
    dst[i] = '\0';
}

static void book_key_from_path(char *dst, size_t dst_len, const char *path) {
    size_t di = 0;
    for (size_t i = 0; path && path[i] && di + 1 < dst_len; i++) {
        char c = path[i];
        dst[di++] = isalnum((unsigned char)c) ? c : '_';
    }
    dst[di] = '\0';
}

static int ensure_track(scan_book *book) {
    if (book->track_count == book->track_cap) {
        size_t new_cap = book->track_cap ? book->track_cap * 2 : 8;
        book->tracks = ab_xrealloc(book->tracks, new_cap * sizeof(*book->tracks));
        memset(book->tracks + book->track_cap, 0, (new_cap - book->track_cap) * sizeof(*book->tracks));
        book->track_cap = new_cap;
    }
    return 0;
}

static int add_track(scan_book *book, const char *path, const struct stat *st, int disc) {
    ensure_track(book);
    scan_track *t = &book->tracks[book->track_count++];
    memset(t, 0, sizeof(*t));
    t->row.book_id = 0;
    t->row.ordinal = 0;
    t->row.disc_number = disc;
    t->row.track_number = infer_track_number(path);
    ab_copy_str(t->row.path, sizeof(t->row.path), path);
    const char *base = basename_ptr(path);
    normalize_title(t->row.title, sizeof(t->row.title), base);
    derive_sort_title(t->row.sort_title, sizeof(t->row.sort_title), base);
    t->row.file_size = st ? (int64_t)st->st_size : 0;
    t->row.file_mtime = st ? (int64_t)st->st_mtime : 0;
    t->row.duration_ms = (int64_t)((t->row.file_size > 0) ? (t->row.file_size / 160) : 0);
    return 0;
}

static int collect_tracks(scan_book *book, const char *path, int disc) {
    DIR *dir = opendir(path);
    if (!dir) return -1;
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.' && (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
            continue;
        }
        char child[512];
        ab_join_path(child, sizeof(child), path, de->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (is_disc_folder(de->d_name)) {
                if (collect_tracks(book, child, parse_disc_number(de->d_name)) != 0) rc = -1;
            }
            continue;
        }
        if (ab_is_audio_file(de->d_name)) {
            if (add_track(book, child, &st, disc > 0 ? disc : 1) != 0) rc = -1;
        }
    }
    closedir(dir);
    return rc;
}

static void finalize_track_order(scan_book *book) {
    if (!book || book->track_count == 0) return;
    qsort(book->tracks, book->track_count, sizeof(*book->tracks), compare_scan_track);
    for (size_t i = 0; i < book->track_count; i++) {
        book->tracks[i].row.ordinal = (int)(i + 1);
    }
}

static int persist_book(audiobook_db *db, scan_book *book, library_refresh_report *report) {
    if (!book || book->track_count == 0) return 0;
    book->row.track_count = (int)book->track_count;
    book->row.total_duration_ms = 0;
    for (size_t i = 0; i < book->track_count; i++) {
        book->row.total_duration_ms += book->tracks[i].row.duration_ms;
    }
    if (db_upsert_book(db, &book->row, &book->row.book_id) == 0) {
        for (size_t i = 0; i < book->track_count; i++) {
            book->tracks[i].row.book_id = book->row.book_id;
            if (db_upsert_track(db, &book->tracks[i].row, &book->tracks[i].row.track_id) == 0) {
                if (report) report->tracks_found++;
            }
        }
        if (report) report->books_found++;
    }
    return 0;
}

static int scan_tree(audiobook_db *db, const char *path, library_refresh_report *report) {
    DIR *dir = opendir(path);
    if (!dir) return -1;

    scan_child_dir children[128];
    size_t child_count = 0;
    bool direct_audio = false;
    bool has_child_dirs = false;
    bool all_disc_like = true;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.' && (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
            continue;
        }
        char child[512];
        ab_join_path(child, sizeof(child), path, de->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            has_child_dirs = true;
            if (!is_disc_folder(de->d_name)) all_disc_like = false;
            if (child_count < sizeof(children) / sizeof(children[0])) {
                ab_copy_str(children[child_count].path, sizeof(children[child_count].path), child);
                ab_copy_str(children[child_count].name, sizeof(children[child_count].name), de->d_name);
                children[child_count].disc_like = is_disc_folder(de->d_name);
                child_count++;
            }
        } else if (ab_is_audio_file(de->d_name)) {
            direct_audio = true;
        }
    }
    closedir(dir);

    if (direct_audio || (has_child_dirs && all_disc_like)) {
        scan_book book;
        memset(&book, 0, sizeof(book));
        ab_copy_str(book.row.root_path, sizeof(book.row.root_path), path);
        const char *slash = strrchr(path, '/');
        slash = slash ? slash + 1 : path;
        char base[256];
        normalize_title(base, sizeof(base), slash && slash[0] ? slash : "Audiobook");
        ab_copy_str(book.row.title, sizeof(book.row.title), base);
        derive_sort_title(book.row.sort_title, sizeof(book.row.sort_title), base);
        book.row.completed = 0;
        book_key_from_path(book.row.book_key, sizeof(book.row.book_key), path);

        if (collect_tracks(&book, path, 1) == 0 && book.track_count > 0) {
            finalize_track_order(&book);
            persist_book(db, &book, report);
        }
        free_scan_book(&book);
        return 0;
    }

    for (size_t i = 0; i < child_count; i++) {
        if (scan_tree(db, children[i].path, report) != 0) {
            if (report) report->errors++;
        }
    }
    return 0;
}

int library_refresh(audiobook_db *db, const audiobook_config *cfg, library_refresh_report *report) {
    if (!db || !cfg) return -1;
    if (report) memset(report, 0, sizeof(*report));
    db_clear_library(db);
    if (scan_tree(db, cfg->library_root, report) != 0) {
        if (report) report->errors++;
    } else if (report) {
        report->roots_scanned = 1;
    }
    return 0;
}
