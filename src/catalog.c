/*
 * catalog.c — catalog.tsv parsing, album pattern loading,
 *             field lookups by path/root/index
 *
 * Spec section 2.5, section 14.
 *
 * catalog.tsv format (tab-separated, first line is header):
 *   root  index  count  media_id  path  title  album  (unused)  book_key
 */

#include "catalog.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Split a line on tabs.  Fills fields[] with pointers into the line buffer
 * (which is modified in place).  Returns the number of fields found. */
static int split_tabs(char *line, char *fields[], int max_fields) {
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (n < max_fields) {
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        fields[n++] = tab + 1;
        p = tab + 1;
    }
    return n;
}

/* Trim trailing whitespace (newline, CR, etc.) */
static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

/* ── Catalog loading ──────────────────────────────────────────────── */

static int parse_catalog_tsv(catalog_db *db, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Missing catalog is not an error — return empty */
        return 0;
    }

    char line[2048];
    int line_num = 0;
    size_t cap = 256;
    size_t count = 0;

    db->entries = malloc(cap * sizeof(catalog_entry));
    if (!db->entries) {
        fclose(fp);
        return -1;
    }
    memset(db->entries, 0, cap * sizeof(catalog_entry));

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Skip header line */
        if (line_num == 1) continue;

        /* Skip empty lines and comments */
        if (line[0] == '\n' || line[0] == '#') continue;

        rtrim(line);

        /* Split on tabs */
        char *fields[12];
        int nf = split_tabs(line, fields, 12);

        if (nf < 7) continue;  /* need at least 7 fields */

        if (count >= cap) {
            cap *= 2;
            catalog_entry *new_entries = realloc(db->entries, cap * sizeof(catalog_entry));
            if (!new_entries) {
                fclose(fp);
                return -1;
            }
            db->entries = new_entries;
            memset(db->entries + count, 0, (cap - count) * sizeof(catalog_entry));
        }

        catalog_entry *e = &db->entries[count];
        memset(e, 0, sizeof(*e));

        /* field 1: root */
        strncpy(e->root, fields[0], sizeof(e->root) - 1);
        /* field 2: track index */
        e->index = atoi(fields[1]);
        /* field 3: track count */
        e->count = atoi(fields[2]);
        /* field 4: media_id */
        if (nf > 3 && fields[3][0]) {
            e->media_id = atoi(fields[3]);
        } else {
            e->media_id = -1;
        }
        /* field 5: path */
        if (nf > 4) {
            strncpy(e->path, fields[4], sizeof(e->path) - 1);
        }
        /* field 6: title */
        if (nf > 5) {
            strncpy(e->title, fields[5], sizeof(e->title) - 1);
        }
        /* field 7: album */
        if (nf > 6) {
            strncpy(e->album, fields[6], sizeof(e->album) - 1);
        }
        /* field 9: book_key (field 8 is unused, may be empty) */
        if (nf > 8) {
            strncpy(e->book_key, fields[8], sizeof(e->book_key) - 1);
        }

        count++;
    }

    fclose(fp);
    db->count = count;
    return 0;
}

static int load_album_patterns(catalog_db *db, const char *albums_path) {
    FILE *fp = fopen(albums_path, "r");
    if (!fp) {
        /* Derive from catalog entries instead */
        return refresh_catalog_album_patterns(db);
    }

    char line[512];
    size_t cap = 64;
    size_t count = 0;

    db->album_patterns = malloc(cap * sizeof(char *));
    if (!db->album_patterns) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        rtrim(line);
        if (!line[0] || line[0] == '#') continue;

        if (count >= cap) {
            cap *= 2;
            char **newp = realloc(db->album_patterns, cap * sizeof(char *));
            if (!newp) {
                fclose(fp);
                return -1;
            }
            db->album_patterns = newp;
        }

        db->album_patterns[count] = xstrdup(line);
        if (db->album_patterns[count]) count++;
    }

    fclose(fp);
    db->album_pattern_count = count;
    return 0;
}

int catalog_load(catalog_db *db, const char *catalog_path,
                 const char *albums_path, const char *books_path) {
    if (!db) return -1;

    memset(db, 0, sizeof(*db));

    if (catalog_path) {
        if (parse_catalog_tsv(db, catalog_path) != 0) {
            return -1;
        }
    }

    if (albums_path && albums_path[0]) {
        load_album_patterns(db, albums_path);
    } else {
        refresh_catalog_album_patterns(db);
    }

    /* books_path is used by the memscan helper, not loaded into memory here */
    (void)books_path;

    return 0;
}

void catalog_free(catalog_db *db) {
    if (!db) return;

    if (db->entries) {
        free(db->entries);
        db->entries = NULL;
    }
    db->count = 0;

    if (db->album_patterns) {
        for (size_t i = 0; i < db->album_pattern_count; i++) {
            free(db->album_patterns[i]);
        }
        free(db->album_patterns);
        db->album_patterns = NULL;
    }
    db->album_pattern_count = 0;
}

int refresh_catalog_album_patterns(catalog_db *db) {
    if (!db) return -1;

    /* Free existing patterns */
    if (db->album_patterns) {
        for (size_t i = 0; i < db->album_pattern_count; i++) {
            free(db->album_patterns[i]);
        }
        free(db->album_patterns);
        db->album_patterns = NULL;
    }
    db->album_pattern_count = 0;

    if (db->count == 0) return 0;

    /* Allocate initial capacity */
    size_t cap = 64;
    db->album_patterns = malloc(cap * sizeof(char *));
    if (!db->album_patterns) return -1;

    for (size_t i = 0; i < db->count; i++) {
        const char *album = db->entries[i].album;
        if (!album[0]) continue;

        /* Check if already in the list */
        bool found = false;
        for (size_t j = 0; j < db->album_pattern_count; j++) {
            if (strcmp(db->album_patterns[j], album) == 0) {
                found = true;
                break;
            }
        }
        if (found) continue;

        if (db->album_pattern_count >= cap) {
            cap *= 2;
            char **newp = realloc(db->album_patterns, cap * sizeof(char *));
            if (!newp) return -1;
            db->album_patterns = newp;
        }

        db->album_patterns[db->album_pattern_count] = xstrdup(album);
        if (db->album_patterns[db->album_pattern_count]) {
            db->album_pattern_count++;
        }
    }

    return 0;
}

/* ── Field lookups ────────────────────────────────────────────────── */

const catalog_entry *catalog_field_for_path(const catalog_db *db, const char *path) {
    if (!db || !path || !db->entries) return NULL;

    for (size_t i = 0; i < db->count; i++) {
        if (strcmp(db->entries[i].path, path) == 0) {
            return &db->entries[i];
        }
    }
    return NULL;
}

const catalog_entry *catalog_field_for_root_index(const catalog_db *db,
                                                   const char *root, int index) {
    if (!db || !root || !db->entries) return NULL;

    for (size_t i = 0; i < db->count; i++) {
        if (db->entries[i].index == index && strcmp(db->entries[i].root, root) == 0) {
            return &db->entries[i];
        }
    }
    return NULL;
}

int catalog_first_path_for_root(const catalog_db *db, const char *root,
                                 char *out_path, size_t out_len) {
    if (!db || !root || !out_path || out_len < 2) return -1;

    for (size_t i = 0; i < db->count; i++) {
        if (strcmp(db->entries[i].root, root) == 0) {
            strncpy(out_path, db->entries[i].path, out_len - 1);
            out_path[out_len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

const char *book_key_for_path(const catalog_db *db, const char *path) {
    const catalog_entry *e = catalog_field_for_path(db, path);
    if (e && e->book_key[0]) {
        return e->book_key;
    }
    return "";
}