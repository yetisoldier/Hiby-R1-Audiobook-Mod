#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *ab_xcalloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

void *ab_xrealloc(void *p, size_t size) {
    void *q = realloc(p, size);
    if (!q && size != 0) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return q;
}

char *ab_xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memcpy(p, s, n);
    return p;
}

int ab_copy_str(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return -1;
    dst[0] = '\0';
    if (!src) return 0;
    snprintf(dst, dst_len, "%s", src);
    dst[dst_len - 1] = '\0';
    return 0;
}

int ab_join_path(char *dst, size_t dst_len, const char *a, const char *b) {
    if (!dst || dst_len == 0) return -1;
    if (!a) a = "";
    if (!b) b = "";
    if (!a[0]) return ab_copy_str(dst, dst_len, b);
    if (!b[0]) return ab_copy_str(dst, dst_len, a);
    size_t alen = strlen(a);
    bool slash = a[alen - 1] != '/';
    snprintf(dst, dst_len, "%s%s%s", a, slash ? "/" : "", b);
    dst[dst_len - 1] = '\0';
    return 0;
}

bool ab_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    return sl >= su && strcmp(s + sl - su, suffix) == 0;
}

bool ab_is_audio_file(const char *path) {
    return ab_ends_with(path, ".mp3") || ab_ends_with(path, ".m4b") ||
           ab_ends_with(path, ".m4a") || ab_ends_with(path, ".flac") ||
           ab_ends_with(path, ".wav") || ab_ends_with(path, ".ogg") ||
           ab_ends_with(path, ".opus");
}

uint64_t ab_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

