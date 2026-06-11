#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_CATALOG_BOOKS "/usr/data/audiobooks/catalog-books.tsv"
#define DEFAULT_ADDR 0x008b1000UL
#define DEFAULT_BYTES 0x00034000UL

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --pid PID [--catalog-books PATH] [--addr ADDR] [--bytes N]\n"
            "Scans a running hiby_player memory range for UTF-16LE audiobook book roots.\n",
            argv0);
}

static unsigned long parse_ulong(const char *text) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || !end || *end) {
        fprintf(stderr, "invalid number: %s\n", text);
        exit(2);
    }
    return value;
}

static int read_all_pread(int fd, unsigned char *buf, size_t len, unsigned long addr) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, buf + done, len - done, (off_t)(addr + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static void *find_bytes(const unsigned char *haystack, size_t haystack_len,
                        const unsigned char *needle, size_t needle_len) {
    if (!needle_len) return (void *)haystack;
    if (needle_len > haystack_len) return NULL;
    size_t last = haystack_len - needle_len;
    for (size_t i = 0; i <= last; i++) {
        if (haystack[i] == needle[0] && memcmp(haystack + i, needle, needle_len) == 0) {
            return (void *)(haystack + i);
        }
    }
    return NULL;
}

static int utf8_next(const unsigned char **p, uint32_t *out) {
    const unsigned char *s = *p;
    if (*s < 0x80) {
        *out = *s;
        *p = s + 1;
        return 0;
    }
    if ((*s & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        *out = ((uint32_t)(s[0] & 0x1f) << 6) | (uint32_t)(s[1] & 0x3f);
        *p = s + 2;
        return 0;
    }
    if ((*s & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        *out = ((uint32_t)(s[0] & 0x0f) << 12) | ((uint32_t)(s[1] & 0x3f) << 6) |
               (uint32_t)(s[2] & 0x3f);
        *p = s + 3;
        return 0;
    }
    if ((*s & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 &&
        (s[3] & 0xc0) == 0x80) {
        *out = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3f) << 12) |
               ((uint32_t)(s[2] & 0x3f) << 6) | (uint32_t)(s[3] & 0x3f);
        *p = s + 4;
        return 0;
    }
    *out = 0xfffd;
    *p = s + 1;
    return 0;
}

static unsigned char *utf8_to_utf16le(const char *text, size_t *out_len) {
    size_t cap = strlen(text) * 4 + 2;
    unsigned char *out = (unsigned char *)malloc(cap);
    if (!out) return NULL;
    size_t len = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t cp = 0;
        utf8_next(&p, &cp);
        if (cp > 0x10ffff) cp = 0xfffd;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            uint16_t hi = (uint16_t)(0xd800u + (cp >> 10));
            uint16_t lo = (uint16_t)(0xdc00u + (cp & 0x3ffu));
            out[len++] = (unsigned char)(hi & 0xff);
            out[len++] = (unsigned char)(hi >> 8);
            out[len++] = (unsigned char)(lo & 0xff);
            out[len++] = (unsigned char)(lo >> 8);
        } else {
            uint16_t w = (uint16_t)cp;
            out[len++] = (unsigned char)(w & 0xff);
            out[len++] = (unsigned char)(w >> 8);
        }
    }
    *out_len = len;
    return out;
}

static void trim_line(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

int main(int argc, char **argv) {
    int pid = 0;
    const char *catalog_books = DEFAULT_CATALOG_BOOKS;
    unsigned long addr = DEFAULT_ADDR;
    unsigned long bytes = DEFAULT_BYTES;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            pid = (int)parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--catalog-books") == 0 && i + 1 < argc) {
            catalog_books = argv[++i];
        } else if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
            addr = parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--bytes") == 0 && i + 1 < argc) {
            bytes = parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (pid <= 0 || bytes == 0 || bytes > (16UL * 1024UL * 1024UL)) {
        usage(argv[0]);
        return 2;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)bytes);
    if (!buf) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }

    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);
    int mem_fd = open(mem_path, O_RDONLY);
    if (mem_fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", mem_path, strerror(errno));
        free(buf);
        return 2;
    }
    if (read_all_pread(mem_fd, buf, (size_t)bytes, addr) != 0) {
        fprintf(stderr, "read %s at 0x%lx failed: %s\n", mem_path, addr, strerror(errno));
        close(mem_fd);
        free(buf);
        return 2;
    }
    close(mem_fd);

    FILE *fp = fopen(catalog_books, "r");
    if (!fp) {
        fprintf(stderr, "open %s failed: %s\n", catalog_books, strerror(errno));
        free(buf);
        return 2;
    }

    char line[PATH_MAX * 2];
    int row = 0;
    while (fgets(line, sizeof(line), fp)) {
        row++;
        if (row == 1) continue;
        trim_line(line);
        char *tab = strchr(line, '\t');
        if (tab) *tab = '\0';
        if (!line[0]) continue;

        size_t needle_len = 0;
        unsigned char *needle = utf8_to_utf16le(line, &needle_len);
        if (!needle) {
            fclose(fp);
            free(buf);
            return 2;
        }
        int found = find_bytes(buf, (size_t)bytes, needle, needle_len) != NULL;
        free(needle);
        if (found) {
            printf("%s\n", line);
            fclose(fp);
            free(buf);
            return 0;
        }
    }

    fclose(fp);
    free(buf);
    return 1;
}
