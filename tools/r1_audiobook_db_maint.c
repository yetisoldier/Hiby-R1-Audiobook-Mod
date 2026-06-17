#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_DB "/usr/data/usrlocal_media.db"
#define DEFAULT_SD_ROOT "/usr/data/mnt/sd_0"
#define DEFAULT_MUSIC_DIR "/usr/data/mnt/sd_0/Music"
#define DEFAULT_AUDIOBOOKS_DIR "/usr/data/mnt/sd_0/Audiobooks"
#define DEFAULT_BASE_DIR "/usr/data/audiobooks"
#define DEFAULT_CATALOG "/usr/data/audiobooks/catalog.tsv"
#define DEFAULT_ALBUM_PATTERNS "/usr/data/audiobooks/catalog-albums.txt"
#define DEFAULT_BOOKS_CATALOG "/usr/data/audiobooks/catalog-books.tsv"
#define DEFAULT_TITLES_CATALOG "/usr/data/audiobooks/catalog-view-title.tsv"
#define DEFAULT_AUTHORS_CATALOG "/usr/data/audiobooks/catalog-view-author.tsv"
#define DEFAULT_SERIES_CATALOG "/usr/data/audiobooks/catalog-view-series.tsv"
#define HIBY_MUSIC_PREFIX_LIKE "a:\\Music\\%"
#define HIBY_PREFIX "a:\\Audiobooks\\"
#define HIBY_PREFIX_LIKE "a:\\Audiobooks\\%"

typedef struct {
    int id;
    char *path;
    char *name;
    char *album;
    char *artist;
    char *genre;
    int year;
    int dis_id;
    int ck_id;
    int has_child_file;
    int begin_time;
    int end_time;
    int cue_id;
    char *character;
    sqlite3_int64 size;
    int sample_rate;
    int bit_rate;
    int bit;
    int channel;
    int format;
    char *quality;
    char *album_pic_path;
    char *lrc_path;
    double track_gain;
    double track_peak;
    sqlite3_int64 ctime;
    sqlite3_int64 mtime;
    char *pinyin_charater;
    char *album_artist;
} MediaRow;

typedef struct {
    MediaRow *items;
    size_t len;
    size_t cap;
} RowVec;

typedef struct {
    char *name;
    int first_id;
    int count;
} FormatCount;

typedef struct {
    FormatCount *items;
    size_t len;
    size_t cap;
} FormatVec;

typedef struct {
    char *root;
    char *album;
    char *author;
    char *book_key;
    char *series;
    char *series_part;
    int track_count;
    int first_media_id;
} BookViewRow;

typedef struct {
    BookViewRow *items;
    size_t len;
    size_t cap;
} BookViewVec;

typedef struct {
    const char *db_path;
    const char *sd_root;
    const char *music_dir;
    const char *audiobooks_dir;
    const char *base_dir;
    const char *catalog_path;
    const char *album_patterns_path;
    const char *books_catalog_path;
    const char *titles_catalog_path;
    const char *authors_catalog_path;
    const char *series_catalog_path;
    int verbose;
} Options;

static void die(const char *message) {
    fprintf(stderr, "r1_audiobook_db_maint: %s\n", message);
    exit(1);
}

static void die_sql(sqlite3 *db, const char *message) {
    fprintf(stderr, "r1_audiobook_db_maint: %s: %s\n", message, sqlite3_errmsg(db));
    exit(1);
}

static void *xcalloc(size_t n, size_t size) {
    void *ptr = calloc(n, size);
    if (!ptr) die("out of memory");
    return ptr;
}

static char *xstrdup(const char *text) {
    const char *src = text ? text : "";
    char *copy = strdup(src);
    if (!copy) die("out of memory");
    return copy;
}

static char *xstrndup0(const char *text, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy) die("out of memory");
    if (len) memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

static char *clean_column_text(sqlite3_stmt *stmt, int col) {
    const unsigned char *raw = sqlite3_column_text(stmt, col);
    int bytes = sqlite3_column_bytes(stmt, col);
    if (!raw || bytes <= 0) return xstrdup("");
    int len = 0;
    while (len < bytes && raw[len] != '\0') len++;
    return xstrndup0((const char *)raw, (size_t)len);
}

static int equals_ci(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int starts_ci(const char *text, const char *prefix) {
    while (*prefix) {
        if (!*text) return 0;
        if (tolower((unsigned char)*text++) != tolower((unsigned char)*prefix++)) return 0;
    }
    return 1;
}

static int ends_ci(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) return 0;
    return starts_ci(text + text_len - suffix_len, suffix);
}

static int is_audio_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    return ends_ci(dot, ".aac") || ends_ci(dot, ".aif") || ends_ci(dot, ".ape") ||
           ends_ci(dot, ".dff") || ends_ci(dot, ".dsf") || ends_ci(dot, ".flac") ||
           ends_ci(dot, ".iso") || ends_ci(dot, ".m4a") || ends_ci(dot, ".m4b") || ends_ci(dot, ".mp2") ||
           ends_ci(dot, ".mp3") || ends_ci(dot, ".oga") || ends_ci(dot, ".ogg") ||
           ends_ci(dot, ".opus") || ends_ci(dot, ".wav") || ends_ci(dot, ".wma");
}

static int format_code_for_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    if (ends_ci(dot, ".aif") || ends_ci(dot, ".wav")) return 1;
    if (ends_ci(dot, ".ape")) return 21574;
    if (ends_ci(dot, ".dff") || ends_ci(dot, ".dsf")) return 54736;
    if (ends_ci(dot, ".flac")) return 61868;
    if (ends_ci(dot, ".iso")) return 0;
    if (ends_ci(dot, ".mp2")) return 80;
    if (ends_ci(dot, ".mp3")) return 85;
    if (ends_ci(dot, ".oga") || ends_ci(dot, ".ogg")) return 26447;
    if (ends_ci(dot, ".opus")) return 28503;
    if (ends_ci(dot, ".wma")) return 353;
    if (ends_ci(dot, ".aac") || ends_ci(dot, ".m4a") || ends_ci(dot, ".m4b")) return 255;
    return 0;
}

static const char *format_name_for_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "UNKNOWN";
    if (ends_ci(dot, ".aac")) return "AAC";
    if (ends_ci(dot, ".aif")) return "AIF";
    if (ends_ci(dot, ".ape")) return "APE";
    if (ends_ci(dot, ".dff")) return "DFF";
    if (ends_ci(dot, ".dsf")) return "DSF";
    if (ends_ci(dot, ".flac")) return "FLAC";
    if (ends_ci(dot, ".iso")) return "ISO";
    if (ends_ci(dot, ".m4a")) return "M4A";
    if (ends_ci(dot, ".m4b")) return "M4B";
    if (ends_ci(dot, ".mp2")) return "MP2";
    if (ends_ci(dot, ".mp3")) return "MP3";
    if (ends_ci(dot, ".oga")) return "OGA";
    if (ends_ci(dot, ".ogg")) return "OGG";
    if (ends_ci(dot, ".opus")) return "OPUS";
    if (ends_ci(dot, ".wav")) return "WAV";
    if (ends_ci(dot, ".wma")) return "WMA";
    return dot[1] ? dot + 1 : "UNKNOWN";
}

static const char *quality_for_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "";
    if (ends_ci(dot, ".flac") || ends_ci(dot, ".wav") || ends_ci(dot, ".aif") ||
        ends_ci(dot, ".ape") || ends_ci(dot, ".dff") || ends_ci(dot, ".dsf") ||
        ends_ci(dot, ".iso")) {
        return "Lossless";
    }
    return "Lossy";
}

static char *file_stem(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *dot = strrchr(name, '.');
    if (!dot || dot <= name) return xstrdup(name);
    return xstrndup0(name, (size_t)(dot - name));
}

static char *hiby_file_stem(const char *path) {
    const char *name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    const char *dot = strrchr(name, '.');
    if (!dot || dot <= name) return xstrdup(name);
    return xstrndup0(name, (size_t)(dot - name));
}

static char *book_root_for_hiby_path(const char *path) {
    const char *slash = strrchr(path, '\\');
    if (!slash) return xstrdup(path);
    return xstrndup0(path, (size_t)(slash - path));
}

static char *last_component(const char *path) {
    const char *slash = strrchr(path, '\\');
    return xstrdup(slash ? slash + 1 : path);
}

static char *parent_component(const char *path) {
    const char *slash = strrchr(path, '\\');
    if (!slash) return xstrdup("");
    char *parent = xstrndup0(path, (size_t)(slash - path));
    char *result = last_component(parent);
    free(parent);
    return result;
}

static char *trim_copy(const char *text) {
    const char *start = text ? text : "";
    while (*start && isspace((unsigned char)*start)) start++;
    const char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) end--;
    return xstrndup0(start, (size_t)(end - start));
}

static int parse_year_prefix(const char *text) {
    if (!text) return 0;
    if (strlen(text) < 4) return 0;
    if (!isdigit((unsigned char)text[0]) || !isdigit((unsigned char)text[1]) ||
        !isdigit((unsigned char)text[2]) || !isdigit((unsigned char)text[3])) {
        return 0;
    }
    int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    if (year < 1900 || year > 2099) return 0;
    return year;
}

static char *clean_album_title(const char *component) {
    char *trimmed = trim_copy(component);
    int year = parse_year_prefix(trimmed);
    if (year && (trimmed[4] == ' ' || trimmed[4] == '-' || trimmed[4] == '_' || trimmed[4] == '.')) {
        const char *p = trimmed + 4;
        while (*p == ' ' || *p == '-' || *p == '_' || *p == '.') p++;
        char *clean = trim_copy(p);
        free(trimmed);
        return clean;
    }
    size_t len = strlen(trimmed);
    if (len > 7 && trimmed[len - 1] == ')' && isdigit((unsigned char)trimmed[len - 2]) &&
        isdigit((unsigned char)trimmed[len - 3]) && isdigit((unsigned char)trimmed[len - 4]) &&
        isdigit((unsigned char)trimmed[len - 5]) && trimmed[len - 6] == '(') {
        trimmed[len - 6] = '\0';
        char *clean = trim_copy(trimmed);
        free(trimmed);
        return clean;
    }
    return trimmed;
}

static int parse_track_number(const char *text) {
    char *stem = hiby_file_stem(text);
    size_t len = strlen(stem);
    int track = 0;
    for (size_t i = len; i > 0; i--) {
        if (stem[i - 1] != '-') continue;
        size_t left_end = i - 1;
        size_t right_start = i;
        if (left_end == 0 || !isdigit((unsigned char)stem[left_end - 1]) ||
            right_start >= len || !isdigit((unsigned char)stem[right_start])) {
            continue;
        }
        size_t left_start = left_end;
        while (left_start > 0 && isdigit((unsigned char)stem[left_start - 1])) left_start--;
        track = atoi(stem + left_start);
        break;
    }
    if (!track) {
        for (size_t i = 0; stem[i]; i++) {
            if (!isdigit((unsigned char)stem[i])) continue;
            track = atoi(stem + i);
            break;
        }
    }
    free(stem);
    return track;
}

static int is_sort_punctuation(char ch) {
    return ch == '(' || ch == '.' || ch == '"' || ch == '\'';
}

static int sort_article_len(const char *text) {
    static const char *articles[] = {
        "the",
        "der",
        "die",
        "das",
        "les",
        "il",
        "lo",
        "la",
        "le",
        "el",
    };
    for (size_t i = 0; i < sizeof(articles) / sizeof(articles[0]); i++) {
        size_t len = strlen(articles[i]);
        if (starts_ci(text, articles[i]) && isspace((unsigned char)text[len])) {
            return (int)len;
        }
    }
    return 0;
}

static const char *normalized_sort_start(const char *text) {
    const char *p = text ? text : "";
    while (*p && (isspace((unsigned char)*p) || is_sort_punctuation(*p))) p++;
    int article_len = sort_article_len(p);
    if (article_len > 0) {
        p += article_len;
        while (*p && isspace((unsigned char)*p)) p++;
    }
    return p;
}

static int sort_tier_for_char(unsigned char ch) {
    if (ch == '\0') return 0;
    if (ch >= '0' && ch <= '9') return 1;
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch >= 0xC0) return 2;
    return 0;
}

static int cmp_sort_text_ci(const char *a, const char *b) {
    const char *sa = normalized_sort_start(a);
    const char *sb = normalized_sort_start(b);
    int tier_a = sort_tier_for_char((unsigned char)*sa);
    int tier_b = sort_tier_for_char((unsigned char)*sb);
    if (tier_a != tier_b) return tier_a < tier_b ? -1 : 1;
    int c = strcasecmp(sa, sb);
    if (c) return c;
    return strcasecmp(a ? a : "", b ? b : "");
}

static char *character_for(const char *text) {
    const char *p = normalized_sort_start(text);
    if (!*p) return xstrdup("#");
    char out[2];
    out[0] = (char)toupper((unsigned char)*p);
    out[1] = '\0';
    return xstrdup(out);
}

static char *pinyin_for(const char *text) {
    char *out = trim_copy(normalized_sort_start(text));
    for (char *p = out; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    }
    return out;
}

static char *stable_slug(const char *text) {
    char *trimmed = trim_copy(text);
    size_t len = strlen(trimmed);
    char *out = (char *)malloc(len + 1);
    if (!out) die("out of memory");
    size_t j = 0;
    int previous_sep = 1;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)trimmed[i];
        if (isalnum(ch)) {
            out[j++] = (char)tolower(ch);
            previous_sep = 0;
        } else if (!previous_sep) {
            out[j++] = '_';
            previous_sep = 1;
        }
    }
    if (j > 0 && out[j - 1] == '_') j--;
    out[j] = '\0';
    free(trimmed);
    return out;
}

static char *book_key_for_catalog(const MediaRow *row, const char *root) {
    const char *author_src = (row->album_artist && *row->album_artist) ? row->album_artist : row->artist;
    char *author = stable_slug(author_src);
    char *album = stable_slug(row->album);
    char *fallback = stable_slug(root);
    char *key;

    if (*author && *album) {
        size_t len = strlen(author) + strlen(album) + 5;
        key = (char *)malloc(len);
        if (!key) die("out of memory");
        snprintf(key, len, "v1_%s_%s", author, album);
    } else if (*fallback) {
        size_t len = strlen(fallback) + 6;
        key = (char *)malloc(len);
        if (!key) die("out of memory");
        snprintf(key, len, "root_%s", fallback);
    } else {
        key = xstrdup("root_unknown");
    }

    free(author);
    free(album);
    free(fallback);
    return key;
}

static char *audiobook_author_from_root(const char *root) {
    const char *rel = root;
    if (starts_ci(rel, HIBY_PREFIX)) rel += strlen(HIBY_PREFIX);
    const char *slash = strchr(rel, '\\');
    if (slash && slash > rel) {
        char *author = xstrndup0(rel, (size_t)(slash - rel));
        char *clean = trim_copy(author);
        free(author);
        if (*clean) return clean;
        free(clean);
    }
    return parent_component(root);
}

static char *catalog_series_from_root(const char *root) {
    const char *rel = root;
    if (starts_ci(rel, HIBY_PREFIX)) rel += strlen(HIBY_PREFIX);
    const char *first = strchr(rel, '\\');
    if (!first) return xstrdup("");
    const char *second = strchr(first + 1, '\\');
    if (!second || second <= first + 1) return xstrdup("");
    char *series = xstrndup0(first + 1, (size_t)(second - first - 1));
    char *clean = trim_copy(series);
    free(series);
    return clean;
}

static char *catalog_series_part_from_root(const char *root, const char *series) {
    if (!series || !*series) return xstrdup("");
    char *book_dir = last_component(root);
    char *open = strrchr(book_dir, '[');
    char *close = open ? strchr(open + 1, ']') : NULL;
    if (!open || !close || close <= open + 1) {
        free(book_dir);
        return xstrdup("");
    }
    char *inside = xstrndup0(open + 1, (size_t)(close - open - 1));
    char *trimmed = trim_copy(inside);
    free(inside);
    char *part = xstrdup("");
    if (starts_ci(trimmed, series)) {
        const char *p = trimmed + strlen(series);
        while (*p && (isspace((unsigned char)*p) || *p == '-' || *p == ':' || *p == '#')) p++;
        free(part);
        part = trim_copy(p);
    }
    free(trimmed);
    free(book_dir);
    return part;
}

static char *strip_matching_series_suffix(const char *text, const char *series) {
    if (!series || !*series) return trim_copy(text);
    char *trimmed = trim_copy(text);
    size_t len = strlen(trimmed);
    if (len <= 2 || trimmed[len - 1] != ']') return trimmed;
    char *open = strrchr(trimmed, '[');
    if (!open || open <= trimmed) return trimmed;
    size_t inside_len = (size_t)((trimmed + len - 1) - (open + 1));
    char *inside = xstrndup0(open + 1, inside_len);
    char *clean_inside = trim_copy(inside);
    free(inside);
    int matches = starts_ci(clean_inside, series);
    free(clean_inside);
    if (!matches) return trimmed;
    *open = '\0';
    char *clean = trim_copy(trimmed);
    free(trimmed);
    return clean;
}

static int make_dir(const char *path) {
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (make_dir(tmp) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (make_dir(tmp) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void ensure_parent_dir(const char *file_path) {
    char *copy = xstrdup(file_path);
    char *slash = strrchr(copy, '/');
    if (slash) {
        *slash = '\0';
        if (*copy && mkdir_p(copy) != 0) {
            fprintf(stderr, "could not create directory %s: %s\n", copy, strerror(errno));
            exit(1);
        }
    }
    free(copy);
}

static char *device_to_hiby_path(const char *device_path, const char *sd_root) {
    const char *rel = device_path;
    size_t root_len = strlen(sd_root);
    if (strncmp(device_path, sd_root, root_len) == 0) {
        rel = device_path + root_len;
        if (*rel == '/' || *rel == '\\') rel++;
    }
    size_t rel_len = strlen(rel);
    char *out = (char *)malloc(rel_len + 4);
    if (!out) die("out of memory");
    out[0] = 'a';
    out[1] = ':';
    out[2] = '\\';
    size_t j = 3;
    for (size_t i = 0; i < rel_len; i++) {
        out[j++] = rel[i] == '/' ? '\\' : rel[i];
    }
    out[j] = '\0';
    return out;
}

static const char *last_path_separator(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash) return backslash;
    if (!backslash) return slash;
    return slash > backslash ? slash : backslash;
}

static char *hiby_sibling_path(const char *hiby_path, const char *filename) {
    const char *slash = strrchr(hiby_path, '\\');
    if (!slash) return xstrdup(filename);
    size_t dir_len = (size_t)(slash - hiby_path);
    size_t name_len = strlen(filename);
    char *out = (char *)malloc(dir_len + 1 + name_len + 1);
    if (!out) die("out of memory");
    memcpy(out, hiby_path, dir_len);
    out[dir_len] = '\\';
    memcpy(out + dir_len + 1, filename, name_len + 1);
    return out;
}

static int device_sibling_exists(const char *device_path, const char *filename) {
    const char *slash = last_path_separator(device_path);
    if (!slash) return 0;
    size_t dir_len = (size_t)(slash - device_path);
    size_t name_len = strlen(filename);
    if (dir_len + 1 + name_len >= PATH_MAX) return 0;
    char candidate[PATH_MAX];
    memcpy(candidate, device_path, dir_len);
    candidate[dir_len] = '/';
    memcpy(candidate + dir_len + 1, filename, name_len + 1);
    struct stat st;
    return stat(candidate, &st) == 0 && S_ISREG(st.st_mode);
}

static char *find_cover_path(const char *device_path, const char *hiby_path) {
    const char *names[] = {
        "cover.jpg", "folder.jpg", "front.jpg", "albumart.jpg",
        "cover.jpeg", "folder.jpeg", "front.jpeg", "albumart.jpeg",
        "cover.png", "folder.png", "front.png", "albumart.png",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (device_sibling_exists(device_path, names[i])) {
            return hiby_sibling_path(hiby_path, names[i]);
        }
    }
    return xstrdup("");
}

static char *find_lrc_path(const char *device_path, const char *hiby_path) {
    char *device_stem = file_stem(device_path);
    char *hiby_stem = hiby_file_stem(hiby_path);
    size_t device_len = strlen(device_stem);
    size_t hiby_len = strlen(hiby_stem);
    char *device_lrc = (char *)malloc(device_len + 5);
    char *hiby_lrc_name = (char *)malloc(hiby_len + 5);
    if (!device_lrc || !hiby_lrc_name) die("out of memory");
    memcpy(device_lrc, device_stem, device_len);
    memcpy(device_lrc + device_len, ".lrc", 5);
    memcpy(hiby_lrc_name, hiby_stem, hiby_len);
    memcpy(hiby_lrc_name + hiby_len, ".lrc", 5);
    char *result = device_sibling_exists(device_path, device_lrc) ? hiby_sibling_path(hiby_path, hiby_lrc_name) : xstrdup("");
    free(device_stem);
    free(hiby_stem);
    free(device_lrc);
    free(hiby_lrc_name);
    return result;
}

static void fill_sidecar_paths(MediaRow *row, const char *hiby_path, const char *device_path) {
    if (!row->album_pic_path || !*row->album_pic_path) {
        free(row->album_pic_path);
        row->album_pic_path = find_cover_path(device_path, hiby_path);
    }
    if (!row->lrc_path || !*row->lrc_path) {
        free(row->lrc_path);
        row->lrc_path = find_lrc_path(device_path, hiby_path);
    }
}

static void rowvec_push(RowVec *vec, MediaRow row) {
    if (vec->len == vec->cap) {
        size_t next = vec->cap ? vec->cap * 2 : 64;
        MediaRow *items = (MediaRow *)realloc(vec->items, next * sizeof(MediaRow));
        if (!items) die("out of memory");
        vec->items = items;
        vec->cap = next;
    }
    vec->items[vec->len++] = row;
}

static void free_row(MediaRow *row) {
    free(row->path);
    free(row->name);
    free(row->album);
    free(row->artist);
    free(row->genre);
    free(row->character);
    free(row->quality);
    free(row->album_pic_path);
    free(row->lrc_path);
    free(row->pinyin_charater);
    free(row->album_artist);
    memset(row, 0, sizeof(*row));
}

static void free_rowvec(RowVec *vec) {
    for (size_t i = 0; i < vec->len; i++) free_row(&vec->items[i]);
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static MediaRow copy_existing_base(const MediaRow *src) {
    MediaRow row = *src;
    row.path = xstrdup(src->path);
    row.name = xstrdup(src->name);
    row.album = xstrdup(src->album);
    row.artist = xstrdup(src->artist);
    row.genre = xstrdup(src->genre);
    row.character = xstrdup(src->character);
    row.quality = xstrdup(src->quality);
    row.album_pic_path = xstrdup(src->album_pic_path);
    row.lrc_path = xstrdup(src->lrc_path);
    row.pinyin_charater = xstrdup(src->pinyin_charater);
    row.album_artist = xstrdup(src->album_artist);
    return row;
}

static const MediaRow *find_existing(const RowVec *existing, const char *path) {
    for (size_t i = 0; i < existing->len; i++) {
        if (equals_ci(existing->items[i].path, path)) return &existing->items[i];
    }
    return NULL;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL failed: %s\n%s\n", errmsg ? errmsg : sqlite3_errmsg(db), sql);
        sqlite3_free(errmsg);
    }
    return rc;
}

static void bind_text0(sqlite3_stmt *stmt, int index, const char *text) {
    const char *value = text ? text : "";
    sqlite3_bind_text(stmt, index, value, (int)strlen(value) + 1, SQLITE_TRANSIENT);
}

static void bind_text_plain(sqlite3_stmt *stmt, int index, const char *text) {
    const char *value = text ? text : "";
    sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
}

static int table_exists(sqlite3 *db, const char *table) {
    sqlite3_stmt *stmt = NULL;
    int exists = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &stmt, NULL) != SQLITE_OK) {
        die_sql(db, "prepare table_exists");
    }
    bind_text_plain(stmt, 1, table);
    if (sqlite3_step(stmt) == SQLITE_ROW) exists = 1;
    sqlite3_finalize(stmt);
    return exists;
}

static void delete_audiobook_rows(sqlite3 *db, const char *table) {
    if (!table_exists(db, table)) return;
    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE path LIKE ? COLLATE NOCASE", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die_sql(db, "prepare delete audiobook rows");
    bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
    if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "delete audiobook rows");
    sqlite3_finalize(stmt);
}

static void delete_non_audiobook_rows(sqlite3 *db, const char *table) {
    if (!table_exists(db, table)) return;
    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE path NOT LIKE ? COLLATE NOCASE", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die_sql(db, "prepare delete non-audiobook rows");
    bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
    if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "delete non-audiobook rows");
    sqlite3_finalize(stmt);
}

static RowVec load_existing_audiobook_rows(sqlite3 *db) {
    RowVec rows = {0};
    const char *sql =
        "SELECT id,path,name,album,artist,genre,year,dis_id,ck_id,has_child_file,"
        "begin_time,end_time,cue_id,character,size,sample_rate,bit_rate,bit,channel,format,"
        "quality,album_pic_path,lrc_path,track_gain,track_peak,ctime,mtime,pinyin_charater,album_artist "
        "FROM MEDIA_TABLE WHERE path LIKE ? COLLATE NOCASE";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die_sql(db, "prepare load existing audiobooks");
    bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaRow row;
        memset(&row, 0, sizeof(row));
        row.id = sqlite3_column_int(stmt, 0);
        row.path = clean_column_text(stmt, 1);
        row.name = clean_column_text(stmt, 2);
        row.album = clean_column_text(stmt, 3);
        row.artist = clean_column_text(stmt, 4);
        row.genre = clean_column_text(stmt, 5);
        row.year = sqlite3_column_int(stmt, 6);
        row.dis_id = sqlite3_column_int(stmt, 7);
        row.ck_id = sqlite3_column_int(stmt, 8);
        row.has_child_file = sqlite3_column_int(stmt, 9);
        row.begin_time = sqlite3_column_int(stmt, 10);
        row.end_time = sqlite3_column_int(stmt, 11);
        row.cue_id = sqlite3_column_int(stmt, 12);
        row.character = clean_column_text(stmt, 13);
        row.size = sqlite3_column_int64(stmt, 14);
        row.sample_rate = sqlite3_column_int(stmt, 15);
        row.bit_rate = sqlite3_column_int(stmt, 16);
        row.bit = sqlite3_column_int(stmt, 17);
        row.channel = sqlite3_column_int(stmt, 18);
        row.format = sqlite3_column_int(stmt, 19);
        row.quality = clean_column_text(stmt, 20);
        row.album_pic_path = clean_column_text(stmt, 21);
        row.lrc_path = clean_column_text(stmt, 22);
        row.track_gain = sqlite3_column_double(stmt, 23);
        row.track_peak = sqlite3_column_double(stmt, 24);
        row.ctime = sqlite3_column_int64(stmt, 25);
        row.mtime = sqlite3_column_int64(stmt, 26);
        row.pinyin_charater = clean_column_text(stmt, 27);
        row.album_artist = clean_column_text(stmt, 28);
        rowvec_push(&rows, row);
    }
    sqlite3_finalize(stmt);
    return rows;
}

static int max_media_id(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int max_id = 0;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id), 0) FROM MEDIA_TABLE", -1, &stmt, NULL) != SQLITE_OK) {
        die_sql(db, "prepare max media id");
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) max_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return max_id;
}

static int count_real_music_rows(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int count = 0;
    const char *sql =
        "SELECT COUNT(*) FROM MEDIA_TABLE "
        "WHERE path LIKE ? COLLATE NOCASE "
        "AND path NOT LIKE ? COLLATE NOCASE";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        die_sql(db, "prepare count real music rows");
    }
    bind_text_plain(stmt, 1, HIBY_MUSIC_PREFIX_LIKE);
    bind_text_plain(stmt, 2, "%*");
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

static int path_component_count(const char *text, char sep) {
    int count = 0;
    for (const char *p = text; *p; p++) {
        if (*p == sep) count++;
    }
    return count;
}

static void normalize_row_after_copy(MediaRow *row, const char *hiby_path, const char *device_path, const struct stat *st, int *next_id) {
    char *root = book_root_for_hiby_path(hiby_path);
    char *book_dir = last_component(root);
    char *parent = parent_component(root);
    char *author_dir = audiobook_author_from_root(root);
    char *album = clean_album_title(book_dir);
    char *series_dir = catalog_series_from_root(root);
    char *album_without_series = strip_matching_series_suffix(album, series_dir);
    free(album);
    album = album_without_series;
    char *stem = hiby_file_stem(hiby_path);
    int year = parse_year_prefix(book_dir);

    free(row->path);
    row->path = xstrdup(hiby_path);
    if (row->id <= 0) row->id = (*next_id)++;
    if (!row->name || !*row->name) {
        free(row->name);
        row->name = xstrdup(stem);
    }
    if (!row->album || !*row->album || equals_ci(row->album, "Unknown")) {
        free(row->album);
        row->album = xstrdup(*album ? album : "Unknown");
    }
    if (!row->artist || !*row->artist || equals_ci(row->artist, "Unknown")) {
        free(row->artist);
        row->artist = xstrdup(*author_dir ? author_dir : (*parent ? parent : "Unknown"));
    }
    if (!row->album_artist || !*row->album_artist || equals_ci(row->album_artist, "Unknown")) {
        free(row->album_artist);
        row->album_artist = xstrdup(*author_dir ? author_dir : (*parent ? parent : row->artist));
    }
    free(row->genre);
    row->genre = xstrdup("Audiobook");
    if (row->year <= 0 && year > 0) row->year = year;
    if (row->ck_id <= 0) row->ck_id = parse_track_number(hiby_path);
    row->has_child_file = 0;
    row->begin_time = 0;
    row->end_time = -1;
    row->cue_id = -1;
    row->size = (sqlite3_int64)st->st_size;
    row->ctime = (sqlite3_int64)st->st_ctime;
    row->mtime = (sqlite3_int64)st->st_mtime;
    if (row->format == 0) row->format = format_code_for_path(device_path);
    if (!row->quality || !*row->quality) {
        free(row->quality);
        row->quality = xstrdup(quality_for_path(device_path));
    }
    fill_sidecar_paths(row, hiby_path, device_path);
    free(row->character);
    row->character = character_for(row->name);
    free(row->pinyin_charater);
    row->pinyin_charater = pinyin_for(row->name);

    free(root);
    free(book_dir);
    free(parent);
    free(author_dir);
    free(series_dir);
    free(album);
    free(stem);
}

static void normalize_music_row(MediaRow *row, const char *hiby_path, const char *device_path, const struct stat *st, int *next_id) {
    char *root = book_root_for_hiby_path(hiby_path);
    char *album_dir = last_component(root);
    char *parent = parent_component(root);
    char *album = clean_album_title(album_dir);
    char *stem = hiby_file_stem(hiby_path);
    int components = path_component_count(hiby_path, '\\');

    free(row->path);
    row->path = xstrdup(hiby_path);
    if (row->id <= 0) row->id = (*next_id)++;
    if (!row->name || !*row->name) {
        free(row->name);
        row->name = xstrdup(stem);
    }
    if (!row->album || !*row->album || equals_ci(row->album, "Unknown")) {
        free(row->album);
        row->album = xstrdup((components >= 3 && *album) ? album : "Unknown");
    }
    if (!row->artist || !*row->artist || equals_ci(row->artist, "Unknown")) {
        free(row->artist);
        row->artist = xstrdup((components >= 4 && *parent && !equals_ci(parent, "Music")) ? parent : "Unknown");
    }
    if (!row->album_artist || !*row->album_artist || equals_ci(row->album_artist, "Unknown")) {
        free(row->album_artist);
        row->album_artist = xstrdup(row->artist && *row->artist ? row->artist : "Unknown");
    }
    if (!row->genre || !*row->genre) {
        free(row->genre);
        row->genre = xstrdup("Unknown");
    }
    row->has_child_file = 0;
    row->begin_time = 0;
    row->end_time = -1;
    row->cue_id = -1;
    row->size = (sqlite3_int64)st->st_size;
    row->ctime = (sqlite3_int64)st->st_ctime;
    row->mtime = (sqlite3_int64)st->st_mtime;
    if (row->format == 0) row->format = format_code_for_path(device_path);
    if (!row->quality || !*row->quality) {
        free(row->quality);
        row->quality = xstrdup(quality_for_path(device_path));
    }
    fill_sidecar_paths(row, hiby_path, device_path);
    free(row->character);
    row->character = character_for(row->name);
    free(row->pinyin_charater);
    row->pinyin_charater = pinyin_for(row->name);

    free(root);
    free(album_dir);
    free(parent);
    free(album);
    free(stem);
}

static MediaRow generated_row_for_file(const char *hiby_path, const char *device_path, const struct stat *st, int *next_id) {
    MediaRow row;
    memset(&row, 0, sizeof(row));
    row.id = (*next_id)++;
    row.path = xstrdup(hiby_path);
    row.name = hiby_file_stem(hiby_path);
    row.album = xstrdup("");
    row.artist = xstrdup("");
    row.genre = xstrdup("Audiobook");
    row.year = 0;
    row.dis_id = 0;
    row.ck_id = 0;
    row.has_child_file = 0;
    row.begin_time = 0;
    row.end_time = -1;
    row.cue_id = -1;
    row.character = xstrdup("");
    row.size = (sqlite3_int64)st->st_size;
    row.sample_rate = 0;
    row.bit_rate = 0;
    row.bit = 0;
    row.channel = 0;
    row.format = format_code_for_path(device_path);
    row.quality = xstrdup(quality_for_path(device_path));
    row.album_pic_path = xstrdup("");
    row.lrc_path = xstrdup("");
    row.track_gain = 0.0;
    row.track_peak = 0.0;
    row.ctime = (sqlite3_int64)st->st_ctime;
    row.mtime = (sqlite3_int64)st->st_mtime;
    row.pinyin_charater = xstrdup("");
    row.album_artist = xstrdup("");
    normalize_row_after_copy(&row, hiby_path, device_path, st, next_id);
    return row;
}

static MediaRow generated_music_row_for_file(const char *hiby_path, const char *device_path, const struct stat *st, int *next_id) {
    MediaRow row;
    memset(&row, 0, sizeof(row));
    row.id = (*next_id)++;
    row.path = xstrdup(hiby_path);
    row.name = hiby_file_stem(hiby_path);
    row.album = xstrdup("");
    row.artist = xstrdup("");
    row.genre = xstrdup("Unknown");
    row.year = 0;
    row.dis_id = 0;
    row.ck_id = 0;
    row.has_child_file = 0;
    row.begin_time = 0;
    row.end_time = -1;
    row.cue_id = -1;
    row.character = xstrdup("");
    row.size = (sqlite3_int64)st->st_size;
    row.sample_rate = 0;
    row.bit_rate = 0;
    row.bit = 0;
    row.channel = 0;
    row.format = format_code_for_path(device_path);
    row.quality = xstrdup(quality_for_path(device_path));
    row.album_pic_path = xstrdup("");
    row.lrc_path = xstrdup("");
    row.track_gain = 0.0;
    row.track_peak = 0.0;
    row.ctime = (sqlite3_int64)st->st_ctime;
    row.mtime = (sqlite3_int64)st->st_mtime;
    row.pinyin_charater = xstrdup("");
    row.album_artist = xstrdup("");
    normalize_music_row(&row, hiby_path, device_path, st, next_id);
    return row;
}

static void scan_music_dir(const char *dir_path, const char *sd_root, RowVec *rows, int *next_id) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_music_dir(path, sd_root, rows, next_id);
        } else if (S_ISREG(st.st_mode) && is_audio_ext(path)) {
            char *hiby_path = device_to_hiby_path(path, sd_root);
            MediaRow row = generated_music_row_for_file(hiby_path, path, &st, next_id);
            rowvec_push(rows, row);
            free(hiby_path);
        }
    }
    closedir(dir);
}

static void scan_audiobook_dir(const char *dir_path, const char *sd_root, const RowVec *existing, RowVec *rows, int *next_id) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_audiobook_dir(path, sd_root, existing, rows, next_id);
        } else if (S_ISREG(st.st_mode) && is_audio_ext(path)) {
            char *hiby_path = device_to_hiby_path(path, sd_root);
            const MediaRow *old = find_existing(existing, hiby_path);
            MediaRow row = old ? copy_existing_base(old) : generated_row_for_file(hiby_path, path, &st, next_id);
            if (old) normalize_row_after_copy(&row, hiby_path, path, &st, next_id);
            rowvec_push(rows, row);
            free(hiby_path);
        }
    }
    closedir(dir);
}

static int cmp_path_ci(const void *a, const void *b) {
    const MediaRow *ra = (const MediaRow *)a;
    const MediaRow *rb = (const MediaRow *)b;
    return strcasecmp(ra->path, rb->path);
}

static int cmp_name_ci(const void *a, const void *b) {
    const MediaRow *ra = (const MediaRow *)a;
    const MediaRow *rb = (const MediaRow *)b;
    int c = cmp_sort_text_ci(ra->name, rb->name);
    if (c) return c;
    return strcasecmp(ra->path, rb->path);
}

static int cmp_album_track(const void *a, const void *b) {
    const MediaRow *ra = (const MediaRow *)a;
    const MediaRow *rb = (const MediaRow *)b;
    int c = cmp_sort_text_ci(ra->album, rb->album);
    if (c) return c;
    if (ra->dis_id != rb->dis_id) return ra->dis_id < rb->dis_id ? -1 : 1;
    if (ra->ck_id != rb->ck_id) return ra->ck_id < rb->ck_id ? -1 : 1;
    return strcasecmp(ra->path, rb->path);
}

static int cmp_catalog(const void *a, const void *b) {
    const MediaRow *ra = (const MediaRow *)a;
    const MediaRow *rb = (const MediaRow *)b;
    char *root_a = book_root_for_hiby_path(ra->path);
    char *root_b = book_root_for_hiby_path(rb->path);
    int c = strcasecmp(root_a, root_b);
    free(root_a);
    free(root_b);
    if (c) return c;
    if (ra->dis_id != rb->dis_id) return ra->dis_id < rb->dis_id ? -1 : 1;
    if (ra->ck_id != rb->ck_id) return ra->ck_id < rb->ck_id ? -1 : 1;
    return strcasecmp(ra->path, rb->path);
}

static void assign_missing_track_numbers(RowVec *rows) {
    if (rows->len == 0) return;
    qsort(rows->items, rows->len, sizeof(MediaRow), cmp_path_ci);
    char *current_root = NULL;
    int index = 0;
    for (size_t i = 0; i < rows->len; i++) {
        char *root = book_root_for_hiby_path(rows->items[i].path);
        if (!current_root || !equals_ci(current_root, root)) {
            free(current_root);
            current_root = xstrdup(root);
            index = 1;
        } else {
            index++;
        }
        if (rows->items[i].ck_id <= 0) rows->items[i].ck_id = index;
        free(root);
    }
    free(current_root);
}

static void insert_media_rows(
    sqlite3 *db,
    const char *table,
    RowVec *rows,
    int (*cmp)(const void *, const void *),
    int index_by_album
) {
    if (!table_exists(db, table)) return;
    qsort(rows->items, rows->len, sizeof(MediaRow), cmp);
    char sql[1024];
    snprintf(
        sql,
        sizeof(sql),
        "INSERT INTO %s "
        "(id,path,name,album,artist,genre,year,dis_id,ck_id,has_child_file,begin_time,end_time,cue_id,"
        "character,size,sample_rate,bit_rate,bit,channel,format,quality,album_pic_path,lrc_path,"
        "track_gain,track_peak,ctime,mtime,pinyin_charater,album_artist) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die_sql(db, "prepare insert media");
    for (size_t i = 0; i < rows->len; i++) {
        MediaRow *r = &rows->items[i];
        char *album_character = NULL;
        char *album_pinyin = NULL;
        const char *character = r->character;
        const char *pinyin = r->pinyin_charater;
        if (index_by_album && r->album && *r->album) {
            album_character = character_for(r->album);
            album_pinyin = pinyin_for(r->album);
            character = album_character;
            pinyin = album_pinyin;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int(stmt, 1, r->id);
        bind_text0(stmt, 2, r->path);
        bind_text0(stmt, 3, r->name);
        bind_text0(stmt, 4, r->album);
        bind_text0(stmt, 5, r->artist);
        bind_text0(stmt, 6, r->genre);
        sqlite3_bind_int(stmt, 7, r->year);
        sqlite3_bind_int(stmt, 8, r->dis_id);
        sqlite3_bind_int(stmt, 9, r->ck_id);
        sqlite3_bind_int(stmt, 10, r->has_child_file);
        sqlite3_bind_int(stmt, 11, r->begin_time);
        sqlite3_bind_int(stmt, 12, r->end_time);
        sqlite3_bind_int(stmt, 13, r->cue_id);
        bind_text0(stmt, 14, character);
        sqlite3_bind_int64(stmt, 15, r->size);
        sqlite3_bind_int(stmt, 16, r->sample_rate);
        sqlite3_bind_int(stmt, 17, r->bit_rate);
        sqlite3_bind_int(stmt, 18, r->bit);
        sqlite3_bind_int(stmt, 19, r->channel);
        sqlite3_bind_int(stmt, 20, r->format);
        bind_text0(stmt, 21, r->quality);
        bind_text0(stmt, 22, r->album_pic_path);
        bind_text0(stmt, 23, r->lrc_path);
        sqlite3_bind_double(stmt, 24, r->track_gain);
        sqlite3_bind_double(stmt, 25, r->track_peak);
        sqlite3_bind_int64(stmt, 26, r->ctime);
        sqlite3_bind_int64(stmt, 27, r->mtime);
        bind_text0(stmt, 28, pinyin);
        bind_text0(stmt, 29, r->album_artist);
        if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "insert media row");
        free(album_character);
        free(album_pinyin);
    }
    sqlite3_finalize(stmt);
}

static void rebuild_search_table(sqlite3 *db) {
    if (!table_exists(db, "SEARCH_TABLE")) return;
    if (exec_sql(db, "DELETE FROM SEARCH_TABLE") != SQLITE_OK) die_sql(db, "delete search table");
    const char *sql =
        "INSERT INTO SEARCH_TABLE "
        "(id,path,name,album,artist,genre,year,dis_id,ck_id,has_child_file,begin_time,end_time,cue_id,"
        "character,size,sample_rate,bit_rate,bit,channel,format,quality,album_pic_path,lrc_path,"
        "track_gain,track_peak,ctime,mtime,pinyin_charater,album_artist) "
        "SELECT id,path,name,album,artist,genre,year,dis_id,ck_id,has_child_file,begin_time,end_time,cue_id,"
        "character,size,sample_rate,bit_rate,bit,channel,format,quality,album_pic_path,lrc_path,"
        "track_gain,track_peak,ctime,mtime,pinyin_charater,album_artist "
        "FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die_sql(db, "prepare rebuild search");
    bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
    if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "rebuild search");
    sqlite3_finalize(stmt);
}

static void rebuild_named_catalog(sqlite3 *db, const char *table, const char *column, int has_mqa) {
    if (!table_exists(db, table)) return;
    char sql[512];
    snprintf(sql, sizeof(sql), "DELETE FROM %s", table);
    if (exec_sql(db, sql) != SQLITE_OK) die_sql(db, "delete named catalog");
    snprintf(
        sql,
        sizeof(sql),
        "SELECT %s, COUNT(*), MIN(id), MIN(COALESCE(ctime,0)), MAX(COALESCE(mtime,0)) "
        "FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE GROUP BY %s ORDER BY %s COLLATE NOCASE",
        column,
        column,
        column);
    sqlite3_stmt *select_stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &select_stmt, NULL) != SQLITE_OK) die_sql(db, "prepare named catalog select");
    bind_text_plain(select_stmt, 1, HIBY_PREFIX_LIKE);

    snprintf(
        sql,
        sizeof(sql),
        has_mqa ? "INSERT INTO %s (id,%s,character,cn,ctime,mtime,mqa,pinyin_charater) VALUES (?,?,?,?,?,?,?,?)"
                : "INSERT INTO %s (id,%s,character,cn,ctime,mtime,pinyin_charater) VALUES (?,?,?,?,?,?,?)",
        table,
        column);
    sqlite3_stmt *insert_stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &insert_stmt, NULL) != SQLITE_OK) die_sql(db, "prepare named catalog insert");
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        char *value = clean_column_text(select_stmt, 0);
        if (!*value) {
            free(value);
            value = xstrdup("Unknown");
        }
        char *character = character_for(value);
        char *pinyin = pinyin_for(value);
        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        sqlite3_bind_int(insert_stmt, 1, sqlite3_column_int(select_stmt, 2));
        bind_text0(insert_stmt, 2, value);
        bind_text0(insert_stmt, 3, character);
        sqlite3_bind_int(insert_stmt, 4, sqlite3_column_int(select_stmt, 1));
        sqlite3_bind_int64(insert_stmt, 5, sqlite3_column_int64(select_stmt, 3));
        sqlite3_bind_int64(insert_stmt, 6, sqlite3_column_int64(select_stmt, 4));
        if (has_mqa) {
            sqlite3_bind_int(insert_stmt, 7, 0);
            bind_text0(insert_stmt, 8, pinyin);
        } else {
            bind_text0(insert_stmt, 7, pinyin);
        }
        if (sqlite3_step(insert_stmt) != SQLITE_DONE) die_sql(db, "insert named catalog row");
        free(value);
        free(character);
        free(pinyin);
    }
    sqlite3_finalize(insert_stmt);
    sqlite3_finalize(select_stmt);
}

static void formatvec_add(FormatVec *vec, const char *name, int media_id) {
    for (size_t i = 0; i < vec->len; i++) {
        if (equals_ci(vec->items[i].name, name)) {
            if (media_id < vec->items[i].first_id) vec->items[i].first_id = media_id;
            vec->items[i].count++;
            return;
        }
    }
    if (vec->len == vec->cap) {
        size_t next = vec->cap ? vec->cap * 2 : 16;
        FormatCount *items = (FormatCount *)realloc(vec->items, next * sizeof(FormatCount));
        if (!items) die("out of memory");
        vec->items = items;
        vec->cap = next;
    }
    vec->items[vec->len].name = xstrdup(name);
    vec->items[vec->len].first_id = media_id;
    vec->items[vec->len].count = 1;
    vec->len++;
}

static int cmp_format(const void *a, const void *b) {
    const FormatCount *fa = (const FormatCount *)a;
    const FormatCount *fb = (const FormatCount *)b;
    return strcasecmp(fa->name, fb->name);
}

static void free_formatvec(FormatVec *vec) {
    for (size_t i = 0; i < vec->len; i++) free(vec->items[i].name);
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void bookview_push(
    BookViewVec *vec,
    const char *root,
    const char *album,
    const char *author,
    const char *book_key,
    const char *series,
    const char *series_part,
    int track_count,
    int first_media_id
) {
    if (vec->len == vec->cap) {
        size_t next = vec->cap ? vec->cap * 2 : 32;
        BookViewRow *items = (BookViewRow *)realloc(vec->items, next * sizeof(BookViewRow));
        if (!items) die("out of memory");
        vec->items = items;
        vec->cap = next;
    }
    BookViewRow *row = &vec->items[vec->len++];
    row->root = xstrdup(root);
    row->album = xstrdup(album);
    row->author = xstrdup(author);
    row->book_key = xstrdup(book_key);
    row->series = xstrdup(series);
    row->series_part = xstrdup(series_part);
    row->track_count = track_count;
    row->first_media_id = first_media_id;
}

static void free_bookview(BookViewVec *vec) {
    for (size_t i = 0; i < vec->len; i++) {
        free(vec->items[i].root);
        free(vec->items[i].album);
        free(vec->items[i].author);
        free(vec->items[i].book_key);
        free(vec->items[i].series);
        free(vec->items[i].series_part);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rebuild_format_tables(sqlite3 *db) {
    FormatVec formats = {0};
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT id,path FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE", -1, &stmt, NULL) != SQLITE_OK) {
        die_sql(db, "prepare format select");
    }
    bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        char *path = clean_column_text(stmt, 1);
        formatvec_add(&formats, format_name_for_path(path), id);
        free(path);
    }
    sqlite3_finalize(stmt);
    qsort(formats.items, formats.len, sizeof(FormatCount), cmp_format);

    for (int table_i = 0; table_i < 2; table_i++) {
        const char *table = table_i == 0 ? "FORMAT_TABLE" : "FORMAT2_TABLE";
        if (!table_exists(db, table)) continue;
        char sql[256];
        snprintf(sql, sizeof(sql), "DELETE FROM %s", table);
        if (exec_sql(db, sql) != SQLITE_OK) die_sql(db, "delete format table");
        snprintf(sql, sizeof(sql), "INSERT INTO %s (id,format,character,cn) VALUES (?,?,?,?)", table);
        sqlite3_stmt *insert_stmt = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &insert_stmt, NULL) != SQLITE_OK) die_sql(db, "prepare format insert");
        for (size_t i = 0; i < formats.len; i++) {
            char *character = character_for(formats.items[i].name);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            sqlite3_bind_int(insert_stmt, 1, formats.items[i].first_id);
            bind_text0(insert_stmt, 2, formats.items[i].name);
            bind_text0(insert_stmt, 3, character);
            sqlite3_bind_int(insert_stmt, 4, formats.items[i].count);
            if (sqlite3_step(insert_stmt) != SQLITE_DONE) die_sql(db, "insert format row");
            free(character);
        }
        sqlite3_finalize(insert_stmt);
    }
    free_formatvec(&formats);
}

static int scalar_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = NULL;
    int value = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die_sql(db, "prepare scalar");
    bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

static void rebuild_count_table(sqlite3 *db) {
    if (!table_exists(db, "COUNT_TABLE")) return;
    if (exec_sql(db, "DELETE FROM COUNT_TABLE") != SQLITE_OK) die_sql(db, "delete count table");
    const char *queries[] = {
        "SELECT COUNT(*) FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE",
        "SELECT COUNT(DISTINCT album) FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE",
        "SELECT COUNT(DISTINCT artist) FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE",
        "SELECT COUNT(DISTINCT genre) FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE",
        "SELECT COUNT(DISTINCT album_artist) FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE",
    };
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO COUNT_TABLE (cn) VALUES (?)", -1, &stmt, NULL) != SQLITE_OK) {
        die_sql(db, "prepare count insert");
    }
    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        int count = scalar_int(db, queries[i]);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int(stmt, 1, count);
        if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "insert count row");
    }
    sqlite3_finalize(stmt);
}

static void rebuild_time_tables(sqlite3 *db) {
    if (table_exists(db, "CTIME_TABLE")) {
        if (exec_sql(db, "DELETE FROM CTIME_TABLE") != SQLITE_OK) die_sql(db, "delete ctime table");
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, "INSERT INTO CTIME_TABLE (media_id) SELECT id FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE ORDER BY COALESCE(ctime,0) ASC, id ASC", -1, &stmt, NULL) != SQLITE_OK) {
            die_sql(db, "prepare ctime insert");
        }
        bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
        if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "insert ctime table");
        sqlite3_finalize(stmt);
    }
    if (table_exists(db, "MTIME_TABLE")) {
        if (exec_sql(db, "DELETE FROM MTIME_TABLE") != SQLITE_OK) die_sql(db, "delete mtime table");
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, "INSERT INTO MTIME_TABLE (media_id) SELECT id FROM MEDIA_TABLE WHERE path NOT LIKE ? COLLATE NOCASE ORDER BY COALESCE(mtime,0) DESC, id ASC", -1, &stmt, NULL) != SQLITE_OK) {
            die_sql(db, "prepare mtime insert");
        }
        bind_text_plain(stmt, 1, HIBY_PREFIX_LIKE);
        if (sqlite3_step(stmt) != SQLITE_DONE) die_sql(db, "insert mtime table");
        sqlite3_finalize(stmt);
    }
}

static void rebuild_catalog_tables(sqlite3 *db) {
    rebuild_search_table(db);
    rebuild_named_catalog(db, "ARTIST_TABLE", "artist", 0);
    rebuild_named_catalog(db, "ARTIST2_TABLE", "artist", 0);
    rebuild_named_catalog(db, "ALBUM_TABLE", "album", 1);
    rebuild_named_catalog(db, "ALBUM2_TABLE", "album", 1);
    rebuild_named_catalog(db, "GENRE_TABLE", "genre", 0);
    rebuild_named_catalog(db, "GENRE2_TABLE", "genre", 0);
    rebuild_named_catalog(db, "ALBUM_ARTIST_TABLE", "album_artist", 1);
    rebuild_named_catalog(db, "ALBUM_ARTIST2_TABLE", "album_artist", 1);
    rebuild_format_tables(db);
    rebuild_count_table(db);
    rebuild_time_tables(db);
}

static void write_field(FILE *fp, const char *text) {
    const char *p = text ? text : "";
    for (; *p; p++) {
        if (*p == '\t' || *p == '\r' || *p == '\n') fputc(' ', fp);
        else fputc(*p, fp);
    }
}

static int parse_leading_int(const char *text, int *out) {
    const char *p = text ? text : "";
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) return 0;
    int value = 0;
    while (*p && isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    *out = value;
    return 1;
}

static int cmp_series_part(const char *a, const char *b) {
    int ia = 0;
    int ib = 0;
    int has_a = parse_leading_int(a, &ia);
    int has_b = parse_leading_int(b, &ib);
    if (has_a && has_b && ia != ib) return ia < ib ? -1 : 1;
    if (has_a != has_b) return has_a ? -1 : 1;
    return cmp_sort_text_ci(a, b);
}

static int cmp_book_title_view(const void *a, const void *b) {
    const BookViewRow *ra = (const BookViewRow *)a;
    const BookViewRow *rb = (const BookViewRow *)b;
    int c = cmp_sort_text_ci(ra->album, rb->album);
    if (c) return c;
    c = cmp_sort_text_ci(ra->author, rb->author);
    if (c) return c;
    return strcasecmp(ra->root, rb->root);
}

static int cmp_book_author_view(const void *a, const void *b) {
    const BookViewRow *ra = (const BookViewRow *)a;
    const BookViewRow *rb = (const BookViewRow *)b;
    int c = cmp_sort_text_ci(ra->author, rb->author);
    if (c) return c;
    c = cmp_sort_text_ci(ra->album, rb->album);
    if (c) return c;
    return strcasecmp(ra->root, rb->root);
}

static int cmp_book_series_view(const void *a, const void *b) {
    const BookViewRow *ra = (const BookViewRow *)a;
    const BookViewRow *rb = (const BookViewRow *)b;
    int c = cmp_sort_text_ci(ra->series, rb->series);
    if (c) return c;
    c = cmp_series_part(ra->series_part, rb->series_part);
    if (c) return c;
    c = cmp_sort_text_ci(ra->album, rb->album);
    if (c) return c;
    return strcasecmp(ra->root, rb->root);
}

static void write_book_view_common_fields(FILE *fp, const BookViewRow *row) {
    write_field(fp, row->root);
    fputc('\t', fp);
    write_field(fp, row->book_key);
    fputc('\t', fp);
    write_field(fp, row->series);
    fputc('\t', fp);
    write_field(fp, row->series_part);
    fprintf(fp, "\t%d\t%d\n", row->track_count, row->first_media_id);
}

static void write_title_view(FILE *fp, BookViewVec *rows) {
    qsort(rows->items, rows->len, sizeof(BookViewRow), cmp_book_title_view);
    fprintf(fp, "character\tpinyin_charater\talbum\tauthor\troot_hiby_path\tbook_key\tseries\tseries_part\ttrack_count\tfirst_media_id\n");
    for (size_t i = 0; i < rows->len; i++) {
        char *character = character_for(rows->items[i].album);
        char *pinyin = pinyin_for(rows->items[i].album);
        write_field(fp, character);
        fputc('\t', fp);
        write_field(fp, pinyin);
        fputc('\t', fp);
        write_field(fp, rows->items[i].album);
        fputc('\t', fp);
        write_field(fp, rows->items[i].author);
        fputc('\t', fp);
        write_book_view_common_fields(fp, &rows->items[i]);
        free(character);
        free(pinyin);
    }
}

static void write_author_view(FILE *fp, BookViewVec *rows) {
    qsort(rows->items, rows->len, sizeof(BookViewRow), cmp_book_author_view);
    fprintf(fp, "character\tpinyin_charater\tauthor\talbum\troot_hiby_path\tbook_key\tseries\tseries_part\ttrack_count\tfirst_media_id\n");
    for (size_t i = 0; i < rows->len; i++) {
        char *character = character_for(rows->items[i].author);
        char *pinyin = pinyin_for(rows->items[i].author);
        write_field(fp, character);
        fputc('\t', fp);
        write_field(fp, pinyin);
        fputc('\t', fp);
        write_field(fp, rows->items[i].author);
        fputc('\t', fp);
        write_field(fp, rows->items[i].album);
        fputc('\t', fp);
        write_book_view_common_fields(fp, &rows->items[i]);
        free(character);
        free(pinyin);
    }
}

static void write_series_view(FILE *fp, BookViewVec *rows) {
    qsort(rows->items, rows->len, sizeof(BookViewRow), cmp_book_series_view);
    fprintf(fp, "character\tpinyin_charater\tseries\tseries_part\talbum\tauthor\troot_hiby_path\tbook_key\ttrack_count\tfirst_media_id\n");
    for (size_t i = 0; i < rows->len; i++) {
        if (!rows->items[i].series || !*rows->items[i].series) continue;
        char *character = character_for(rows->items[i].series);
        char *pinyin = pinyin_for(rows->items[i].series);
        write_field(fp, character);
        fputc('\t', fp);
        write_field(fp, pinyin);
        fputc('\t', fp);
        write_field(fp, rows->items[i].series);
        fputc('\t', fp);
        write_field(fp, rows->items[i].series_part);
        fputc('\t', fp);
        write_field(fp, rows->items[i].album);
        fputc('\t', fp);
        write_field(fp, rows->items[i].author);
        fputc('\t', fp);
        write_field(fp, rows->items[i].root);
        fputc('\t', fp);
        write_field(fp, rows->items[i].book_key);
        fprintf(fp, "\t%d\t%d\n", rows->items[i].track_count, rows->items[i].first_media_id);
        free(character);
        free(pinyin);
    }
}

static void write_catalog_files(const Options *opts, RowVec *rows) {
    ensure_parent_dir(opts->catalog_path);
    ensure_parent_dir(opts->album_patterns_path);
    ensure_parent_dir(opts->books_catalog_path);
    ensure_parent_dir(opts->titles_catalog_path);
    ensure_parent_dir(opts->authors_catalog_path);
    ensure_parent_dir(opts->series_catalog_path);
    char tmp_catalog[PATH_MAX];
    char tmp_albums[PATH_MAX];
    char tmp_books[PATH_MAX];
    char tmp_titles[PATH_MAX];
    char tmp_authors[PATH_MAX];
    char tmp_series[PATH_MAX];
    snprintf(tmp_catalog, sizeof(tmp_catalog), "%s.tmp.%ld", opts->catalog_path, (long)getpid());
    snprintf(tmp_albums, sizeof(tmp_albums), "%s.tmp.%ld", opts->album_patterns_path, (long)getpid());
    snprintf(tmp_books, sizeof(tmp_books), "%s.tmp.%ld", opts->books_catalog_path, (long)getpid());
    snprintf(tmp_titles, sizeof(tmp_titles), "%s.tmp.%ld", opts->titles_catalog_path, (long)getpid());
    snprintf(tmp_authors, sizeof(tmp_authors), "%s.tmp.%ld", opts->authors_catalog_path, (long)getpid());
    snprintf(tmp_series, sizeof(tmp_series), "%s.tmp.%ld", opts->series_catalog_path, (long)getpid());
    FILE *catalog = fopen(tmp_catalog, "w");
    if (!catalog) {
        fprintf(stderr, "could not write %s: %s\n", tmp_catalog, strerror(errno));
        exit(1);
    }
    FILE *albums = fopen(tmp_albums, "w");
    if (!albums) {
        fprintf(stderr, "could not write %s: %s\n", tmp_albums, strerror(errno));
        exit(1);
    }
    FILE *books = fopen(tmp_books, "w");
    if (!books) {
        fprintf(stderr, "could not write %s: %s\n", tmp_books, strerror(errno));
        exit(1);
    }
    FILE *titles = fopen(tmp_titles, "w");
    if (!titles) {
        fprintf(stderr, "could not write %s: %s\n", tmp_titles, strerror(errno));
        exit(1);
    }
    FILE *authors = fopen(tmp_authors, "w");
    if (!authors) {
        fprintf(stderr, "could not write %s: %s\n", tmp_authors, strerror(errno));
        exit(1);
    }
    FILE *series_view = fopen(tmp_series, "w");
    if (!series_view) {
        fprintf(stderr, "could not write %s: %s\n", tmp_series, strerror(errno));
        exit(1);
    }

    qsort(rows->items, rows->len, sizeof(MediaRow), cmp_catalog);
    fprintf(catalog, "root_hiby_path\ttrack_index\ttrack_count\tmedia_id\tpath\ttitle\talbum\tauthor\tbook_key\tseries\tseries_part\n");
    fprintf(books, "root_hiby_path\talbum\tauthor\tbook_key\tseries\tseries_part\ttrack_count\tfirst_media_id\n");
    char *current_root = NULL;
    char *last_album = NULL;
    BookViewVec view_rows = {0};
    size_t group_start = 0;
    while (group_start < rows->len) {
        free(current_root);
        current_root = book_root_for_hiby_path(rows->items[group_start].path);
        size_t group_end = group_start + 1;
        while (group_end < rows->len) {
            char *root = book_root_for_hiby_path(rows->items[group_end].path);
            int same = equals_ci(root, current_root);
            free(root);
            if (!same) break;
            group_end++;
        }
        int track_count = (int)(group_end - group_start);
        char *series = catalog_series_from_root(current_root);
        char *series_part = catalog_series_part_from_root(current_root, series);
        char *book_key = book_key_for_catalog(&rows->items[group_start], current_root);
        write_field(books, current_root);
        fputc('\t', books);
        write_field(books, rows->items[group_start].album);
        fputc('\t', books);
        write_field(books, rows->items[group_start].album_artist);
        fputc('\t', books);
        write_field(books, book_key);
        fputc('\t', books);
        write_field(books, series);
        fputc('\t', books);
        write_field(books, series_part);
        fprintf(books, "\t%d\t%d\n", track_count, rows->items[group_start].id);
        bookview_push(
            &view_rows,
            current_root,
            rows->items[group_start].album,
            rows->items[group_start].album_artist,
            book_key,
            series,
            series_part,
            track_count,
            rows->items[group_start].id
        );
        for (size_t i = group_start; i < group_end; i++) {
            int index = (int)(i - group_start + 1);
            fprintf(catalog, "%s\t%d\t%d\t%d\t", current_root, index, track_count, rows->items[i].id);
            write_field(catalog, rows->items[i].path);
            fputc('\t', catalog);
            write_field(catalog, rows->items[i].name);
            fputc('\t', catalog);
            write_field(catalog, rows->items[i].album);
            fputc('\t', catalog);
            write_field(catalog, rows->items[i].album_artist);
            fputc('\t', catalog);
            write_field(catalog, book_key);
            fputc('\t', catalog);
            write_field(catalog, series);
            fputc('\t', catalog);
            write_field(catalog, series_part);
            fputc('\n', catalog);
            if (rows->items[i].album && *rows->items[i].album &&
                (!last_album || !equals_ci(last_album, rows->items[i].album))) {
                fprintf(albums, "%s\n", rows->items[i].album);
                free(last_album);
                last_album = xstrdup(rows->items[i].album);
            }
        }
        free(book_key);
        free(series);
        free(series_part);
        group_start = group_end;
    }
    write_title_view(titles, &view_rows);
    write_author_view(authors, &view_rows);
    write_series_view(series_view, &view_rows);
    free(current_root);
    free(last_album);
    free_bookview(&view_rows);
    if (fclose(catalog) != 0) die("failed closing catalog");
    if (fclose(albums) != 0) die("failed closing album patterns");
    if (fclose(books) != 0) die("failed closing books catalog");
    if (fclose(titles) != 0) die("failed closing title view catalog");
    if (fclose(authors) != 0) die("failed closing author view catalog");
    if (fclose(series_view) != 0) die("failed closing series view catalog");
    if (rename(tmp_catalog, opts->catalog_path) != 0) die("failed replacing catalog");
    if (rename(tmp_albums, opts->album_patterns_path) != 0) die("failed replacing album patterns");
    if (rename(tmp_books, opts->books_catalog_path) != 0) die("failed replacing books catalog");
    if (rename(tmp_titles, opts->titles_catalog_path) != 0) die("failed replacing title view catalog");
    if (rename(tmp_authors, opts->authors_catalog_path) != 0) die("failed replacing author view catalog");
    if (rename(tmp_series, opts->series_catalog_path) != 0) die("failed replacing series view catalog");
}

static void maintain_database(const Options *opts) {
    struct stat sd_st;
    if (stat(opts->sd_root, &sd_st) != 0 || !S_ISDIR(sd_st.st_mode)) {
        if (opts->verbose) fprintf(stderr, "SD root not mounted; leaving database unchanged: %s\n", opts->sd_root);
        return;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(opts->db_path, &db) != SQLITE_OK) die_sql(db, "open database");
    sqlite3_busy_timeout(db, 30000);

    RowVec existing = load_existing_audiobook_rows(db);
    int next_id = max_media_id(db) + 1;
    RowVec music_rows = {0};
    RowVec audiobook_rows = {0};
    int existing_music_rows = count_real_music_rows(db);
    int replacing_music_rows = 0;
    struct stat music_st;
    if (existing_music_rows == 0 && stat(opts->music_dir, &music_st) == 0 && S_ISDIR(music_st.st_mode)) {
        scan_music_dir(opts->music_dir, opts->sd_root, &music_rows, &next_id);
        if (music_rows.len > 0) replacing_music_rows = 1;
    }
    struct stat ab_st;
    if (stat(opts->audiobooks_dir, &ab_st) == 0 && S_ISDIR(ab_st.st_mode)) {
        scan_audiobook_dir(opts->audiobooks_dir, opts->sd_root, &existing, &audiobook_rows, &next_id);
    }
    assign_missing_track_numbers(&music_rows);
    assign_missing_track_numbers(&audiobook_rows);

    if (exec_sql(db, "PRAGMA foreign_keys=OFF") != SQLITE_OK) die_sql(db, "set foreign_keys");
    if (exec_sql(db, "BEGIN IMMEDIATE") != SQLITE_OK) die_sql(db, "begin transaction");
    if (replacing_music_rows) {
        delete_non_audiobook_rows(db, "MEDIA_TABLE");
        delete_non_audiobook_rows(db, "MEDIA2_TABLE");
        delete_non_audiobook_rows(db, "MEDIA3_TABLE");
        delete_non_audiobook_rows(db, "SEARCH_TABLE");
    }
    delete_audiobook_rows(db, "MEDIA_TABLE");
    delete_audiobook_rows(db, "MEDIA2_TABLE");
    delete_audiobook_rows(db, "MEDIA3_TABLE");
    delete_audiobook_rows(db, "SEARCH_TABLE");
    insert_media_rows(db, "MEDIA_TABLE", &music_rows, cmp_name_ci, 0);
    insert_media_rows(db, "MEDIA2_TABLE", &music_rows, cmp_album_track, 0);
    insert_media_rows(db, "MEDIA_TABLE", &audiobook_rows, cmp_name_ci, 0);
    insert_media_rows(db, "MEDIA2_TABLE", &audiobook_rows, cmp_album_track, 1);
    rebuild_catalog_tables(db);
    if (exec_sql(db, "COMMIT") != SQLITE_OK) die_sql(db, "commit transaction");

    write_catalog_files(opts, &audiobook_rows);
    if (opts->verbose) {
        fprintf(stderr, "music tracks existing: %d\n", existing_music_rows);
        fprintf(stderr, "music tracks generated: %lu\n", (unsigned long)music_rows.len);
        fprintf(stderr, "audiobook tracks: %lu\n", (unsigned long)audiobook_rows.len);
        fprintf(stderr, "catalog: %s\n", opts->catalog_path);
    }
    free_rowvec(&existing);
    free_rowvec(&music_rows);
    free_rowvec(&audiobook_rows);
    sqlite3_close(db);
}

static void usage(void) {
    printf(
        "usage: r1_audiobook_db_maint [options]\n"
        "  --db PATH                 media DB path (default " DEFAULT_DB ")\n"
        "  --sd-root PATH            SD mount root (default " DEFAULT_SD_ROOT ")\n"
        "  --music-dir PATH          music folder (default " DEFAULT_MUSIC_DIR ")\n"
        "  --audiobooks-dir PATH     audiobook folder (default " DEFAULT_AUDIOBOOKS_DIR ")\n"
        "  --base-dir PATH           runtime state folder (default " DEFAULT_BASE_DIR ")\n"
        "  --catalog PATH            catalog output (default " DEFAULT_CATALOG ")\n"
        "  --album-patterns PATH     album pattern output (default " DEFAULT_ALBUM_PATTERNS ")\n"
        "  --books-catalog PATH      book-level catalog output (default " DEFAULT_BOOKS_CATALOG ")\n"
        "  --titles-catalog PATH     title-view catalog output (default " DEFAULT_TITLES_CATALOG ")\n"
        "  --authors-catalog PATH    author-view catalog output (default " DEFAULT_AUTHORS_CATALOG ")\n"
        "  --series-catalog PATH     series-view catalog output (default " DEFAULT_SERIES_CATALOG ")\n"
        "  --verbose                 print summary\n");
}

static Options parse_args(int argc, char **argv) {
    Options opts;
    opts.db_path = DEFAULT_DB;
    opts.sd_root = DEFAULT_SD_ROOT;
    opts.music_dir = DEFAULT_MUSIC_DIR;
    opts.audiobooks_dir = DEFAULT_AUDIOBOOKS_DIR;
    opts.base_dir = DEFAULT_BASE_DIR;
    opts.catalog_path = DEFAULT_CATALOG;
    opts.album_patterns_path = DEFAULT_ALBUM_PATTERNS;
    opts.books_catalog_path = DEFAULT_BOOKS_CATALOG;
    opts.titles_catalog_path = DEFAULT_TITLES_CATALOG;
    opts.authors_catalog_path = DEFAULT_AUTHORS_CATALOG;
    opts.series_catalog_path = DEFAULT_SERIES_CATALOG;
    opts.verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) opts.db_path = argv[++i];
        else if (strcmp(argv[i], "--sd-root") == 0 && i + 1 < argc) opts.sd_root = argv[++i];
        else if (strcmp(argv[i], "--music-dir") == 0 && i + 1 < argc) opts.music_dir = argv[++i];
        else if (strcmp(argv[i], "--audiobooks-dir") == 0 && i + 1 < argc) opts.audiobooks_dir = argv[++i];
        else if (strcmp(argv[i], "--base-dir") == 0 && i + 1 < argc) opts.base_dir = argv[++i];
        else if (strcmp(argv[i], "--catalog") == 0 && i + 1 < argc) opts.catalog_path = argv[++i];
        else if (strcmp(argv[i], "--album-patterns") == 0 && i + 1 < argc) opts.album_patterns_path = argv[++i];
        else if (strcmp(argv[i], "--books-catalog") == 0 && i + 1 < argc) opts.books_catalog_path = argv[++i];
        else if (strcmp(argv[i], "--titles-catalog") == 0 && i + 1 < argc) opts.titles_catalog_path = argv[++i];
        else if (strcmp(argv[i], "--authors-catalog") == 0 && i + 1 < argc) opts.authors_catalog_path = argv[++i];
        else if (strcmp(argv[i], "--series-catalog") == 0 && i + 1 < argc) opts.series_catalog_path = argv[++i];
        else if (strcmp(argv[i], "--verbose") == 0) opts.verbose = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            exit(0);
        } else {
            usage();
            exit(2);
        }
    }
    return opts;
}

int main(int argc, char **argv) {
    Options opts = parse_args(argc, argv);
    if (mkdir_p(opts.base_dir) != 0) {
        fprintf(stderr, "could not create base dir %s: %s\n", opts.base_dir, strerror(errno));
        return 1;
    }
    maintain_database(&opts);
    return 0;
}
