/*
 * proc_mem.c — process memory reading via pread on /proc/PID/mem
 *
 * Spec section 4.  Direct pread() calls, no shell subprocesses.
 * Handles ESRCH (process gone), EPERM (no access), EIO gracefully.
 */

#include "proc_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ── memmem fallback for musl ─────────────────────────────────────── */

#if !defined(__GLIBC__) && !defined(__UCLIBC__)
/* musl does not provide memmem by default in all configurations.
   Provide a simple O(n*m) implementation. */
static void *memmem_fallback(const void *hay, size_t hay_len,
                             const void *needle, size_t needle_len) {
    if (needle_len == 0) return (void *)hay;
    if (hay_len < needle_len) return NULL;
    const char *h = hay;
    const char *n = needle;
    size_t lim = hay_len - needle_len;
    for (size_t i = 0; i <= lim; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, needle_len) == 0)
            return (void *)(h + i);
    }
    return NULL;
}
#define MEMMEM(h, hl, n, nl) memmem_fallback(h, hl, n, nl)
#else
#define MEMMEM(h, hl, n, nl) memmem(h, hl, n, nl)
#endif

/* ── Public API ───────────────────────────────────────────────────── */

int proc_mem_open(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", (int)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0 && errno == EACCES) {
        /* Try with O_RDONLY | O_CLOEXEC */
        fd = open(path, O_RDONLY | O_CLOEXEC);
    }
    return fd;
}

int proc_mem_read(int fd, void *buf, size_t len, uint32_t addr) {
    if (addr < 4096) return -1;
    if (fd < 0) return -1;

    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, (char *)buf + done, len - done, (off_t)(addr + done));
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        if (n == 0) {
            /* EOF — shouldn't happen on /proc/PID/mem, treat as partial */
            break;
        }
        if (errno == EINTR) continue;

        /* ESRCH: process gone.  EPERM: no access.  EIO: I/O error. */
        return -1;
    }
    return (int)done;
}

int proc_mem_read_u32le(int fd, uint32_t addr, uint32_t *out) {
    if (addr < 4096) return -1;
    unsigned char b[4];
    int n = proc_mem_read(fd, b, 4, addr);
    if (n != 4) return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

int proc_mem_u32le_from_hex(const char *hex8, uint32_t *out) {
    if (!hex8 || strlen(hex8) < 8) return -1;

    /* hex8 is 8 hex chars representing 4 bytes in little-endian order.
       The first 2 hex chars are the low byte, next 2 are the next byte, etc. */
    unsigned int b[4];
    for (int i = 0; i < 4; i++) {
        char byte_str[3] = { hex8[i*2], hex8[i*2+1], '\0' };
        char *end = NULL;
        b[i] = (unsigned int)strtoul(byte_str, &end, 16);
        if (end == byte_str || *end != '\0') return -1;
    }
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

bool proc_mem_contains(int fd, uint32_t addr, size_t count,
                       const void *pattern, size_t pattern_len) {
    if (pattern_len == 0 || count == 0 || count < pattern_len) return false;
    if (addr < 4096) return false;

    char *buf = malloc(count);
    if (!buf) return false;

    int n = proc_mem_read(fd, buf, count, addr);
    if (n < (int)pattern_len) {
        free(buf);
        return false;
    }

    bool found = MEMMEM(buf, (size_t)n, pattern, pattern_len) != NULL;
    free(buf);
    return found;
}

bool proc_mem_contains_catalog_album(int fd, uint32_t addr, size_t count,
                                     const struct catalog_db *cat) {
    if (!cat || count == 0 || addr < 4096) return false;

    /* We need access to catalog_db's album_patterns.  Since we can't
       include catalog.h (circular dependency), we cast through a
       minimal structure.  The catalog_db type is:
       { catalog_entry *entries; size_t count; char **album_patterns; size_t album_pattern_count; }
       We access album_patterns via the known layout. */
    char **patterns = *(char ***)((char *)cat + sizeof(void *) + sizeof(size_t));
    size_t pattern_count = *(size_t *)((char *)cat + sizeof(void *) + sizeof(size_t) + sizeof(char **));
    (void)pattern_count;  /* used below */

    if (!patterns || pattern_count == 0) return false;

    char *buf = malloc(count);
    if (!buf) return false;

    int n = proc_mem_read(fd, buf, count, addr);
    if (n <= 0) {
        free(buf);
        return false;
    }

    bool found = false;
    for (size_t i = 0; i < pattern_count; i++) {
        if (patterns[i] && MEMMEM(buf, (size_t)n, patterns[i], strlen(patterns[i]))) {
            found = true;
            break;
        }
    }

    free(buf);
    return found;
}

int proc_mem_first_catalog_path(int fd, uint32_t addr, size_t count,
                                const struct catalog_db *cat,
                                char *out_path, size_t out_len) {
    if (!cat || count == 0 || addr < 4096 || !out_path || out_len == 0) return -1;

    /* Access catalog entries through the known layout:
       { catalog_entry *entries; size_t count; ... }
       catalog_entry has: root[512], index, count, media_id, path[512], title[256], album[256], book_key[128] */
    char **entries_ptr = (char **)cat;
    size_t entry_count = *(size_t *)((char *)cat + sizeof(char **));
    char *entries_base = *entries_ptr;

    if (!entries_ptr || entry_count == 0) return -1;

    char *buf = malloc(count);
    if (!buf) return -1;

    int n = proc_mem_read(fd, buf, count, addr);
    if (n <= 0) {
        free(buf);
        return -1;
    }

    /* catalog_entry layout: root[512] (offset 0), index (512), count (516),
       media_id (520), path[512] (524), title[256] (1036), album[256] (1292),
       book_key[128] (1548).  Total: 1676 bytes. */
    const size_t ENTRY_SIZE = 512 + 4 + 4 + 4 + 512 + 256 + 256 + 128; /* 1676 */
    const size_t PATH_OFFSET = 512 + 4 + 4 + 4; /* 524 */

    for (size_t i = 0; i < entry_count; i++) {
        char *entry = entries_base + (i * ENTRY_SIZE);
        char *path = entry + PATH_OFFSET;
        size_t plen = strnlen(path, 512);
        if (plen > 0 && MEMMEM(buf, (size_t)n, path, plen)) {
            strncpy(out_path, path, out_len - 1);
            out_path[out_len - 1] = '\0';
            free(buf);
            return 0;
        }
    }

    free(buf);
    return -1;
}

void proc_mem_close(int fd) {
    if (fd >= 0) close(fd);
}