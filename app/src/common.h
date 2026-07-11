#ifndef R1_AB_COMMON_H
#define R1_AB_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    AB_SCREEN_W = 480,
    AB_SCREEN_H = 800,
};

static inline size_t ab_min_size(size_t a, size_t b) { return a < b ? a : b; }
static inline uint32_t ab_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t ab_max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

void *ab_xcalloc(size_t n, size_t size);
void *ab_xrealloc(void *p, size_t size);
char *ab_xstrdup(const char *s);
int ab_copy_str(char *dst, size_t dst_len, const char *src);
int ab_join_path(char *dst, size_t dst_len, const char *a, const char *b);
bool ab_ends_with(const char *s, const char *suffix);
bool ab_is_audio_file(const char *path);
uint64_t ab_now_ms(void);

#endif

