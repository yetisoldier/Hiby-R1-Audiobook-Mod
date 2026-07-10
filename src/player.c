/*
 * player.c — hiby_player PID discovery, position/duration reads,
 *             path slot hex read + decode + cache, book-title marker
 *             sequence polling, memscan root lookup
 *
 * Spec sections 2.3, 4.3, 13.
 */

#include "player.h"
#include "catalog.h"
#include "config.h"
#include "proc_mem.h"
#include "log.h"
#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>

/* ── PID cache ────────────────────────────────────────────────────── */

static pid_t cached_pid = -1;

/* ── PID discovery ───────────────────────────────────────────────── */

pid_t player_pid(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        /* Only numeric directory names */
        /* On some systems (e.g. R1 procfs) d_type is DT_UNKNOWN; the
         * isdigit check below already filters non-PID entries. */
        if (ent->d_type != DT_DIR && ent->d_type != DT_UNKNOWN) continue;
        int is_num = 1;
        for (const char *p = ent->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) { is_num = 0; break; }
        }
        if (!is_num) continue;

        /* Read /proc/PID/cmdline */
        char path[64];
        int pname_len = strlen(ent->d_name);
        if (pname_len > 16) continue;  /* PID directory names are short */
        snprintf(path, sizeof(path), "/proc/%s/cmdline", ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        /* cmdline is null-separated; check if it contains "hiby_player" */
        for (ssize_t i = 0; i < n; i++) {
            if (strncmp(buf + i, "hiby_player", 11) == 0) {
                pid_t pid = (pid_t)atoi(ent->d_name);
                closedir(dir);
                return pid;
            }
        }
    }

    closedir(dir);
    return -1;
}

pid_t player_pid_cached(void) {
    if (cached_pid > 0) {
        /* Check if the process is still alive */
        char path[32];
        snprintf(path, sizeof(path), "/proc/%d", (int)cached_pid);
        if (access(path, F_OK) == 0) {
            return cached_pid;
        }
        /* Process gone — invalidate cache */
        cached_pid = -1;
    }

    cached_pid = player_pid();
    return cached_pid;
}

void player_invalidate_pid(void) {
    cached_pid = -1;
}

/* ── Position / duration reads ────────────────────────────────────── */

static int cached_mem_fd = -1;
static pid_t cached_mem_pid = -1;

static int get_mem_fd(const daemon_config *cfg) {
    (void)cfg;  /* config used by callers for address selection */
    pid_t pid = player_pid_cached();
    if (pid < 0) return -1;

    if (cached_mem_fd >= 0 && cached_mem_pid == pid) {
        /* Verify the fd is still valid */
        char path[32];
        snprintf(path, sizeof(path), "/proc/%d", (int)pid);
        if (access(path, F_OK) == 0) {
            return cached_mem_fd;
        }
        /* Process gone */
        proc_mem_close(cached_mem_fd);
        cached_mem_fd = -1;
        cached_mem_pid = -1;
        player_invalidate_pid();
        return -1;
    }

    /* PID changed or first open */
    if (cached_mem_fd >= 0) {
        proc_mem_close(cached_mem_fd);
        cached_mem_fd = -1;
    }

    cached_mem_fd = proc_mem_open(pid);
    if (cached_mem_fd < 0) {
        log_msg("player: cannot open /proc/%d/mem: %s", (int)pid, strerror(errno));
        return -1;
    }
    cached_mem_pid = pid;
    return cached_mem_fd;
}

uint32_t position_ms_memory(const daemon_config *cfg) {
    if (!cfg) return 0;
    int fd = get_mem_fd(cfg);
    if (fd < 0) return 0;

    uint32_t pos = 0;
    if (proc_mem_read_u32le(fd, cfg->player_position_addr, &pos) != 0) {
        /* Read failed — process may have died */
        proc_mem_close(cached_mem_fd);
        cached_mem_fd = -1;
        cached_mem_pid = -1;
        player_invalidate_pid();
        return 0;
    }
    return pos;
}

uint32_t duration_ms_memory(const daemon_config *cfg) {
    if (!cfg) return 0;
    int fd = get_mem_fd(cfg);
    if (fd < 0) return 0;

    uint32_t dur = 0;
    if (proc_mem_read_u32le(fd, cfg->player_duration_addr, &dur) != 0) {
        proc_mem_close(cached_mem_fd);
        cached_mem_fd = -1;
        cached_mem_pid = -1;
        player_invalidate_pid();
        return 0;
    }
    return dur;
}

/* ── Path slot reading ───────────────────────────────────────────── */

int current_path_slot_hex(const daemon_config *cfg, char *out_hex, size_t out_len) {
    if (!cfg || !out_hex || out_len < 1025) return -1;

    int fd = open(cfg->user_ini_path, O_RDONLY);
    if (fd < 0) return -1;

    /* Seek to offset 40, read 512 bytes */
    if (lseek(fd, 40, SEEK_SET) != 40) {
        close(fd);
        return -1;
    }

    unsigned char buf[512];
    size_t done = 0;
    while (done < 512) {
        ssize_t n = read(fd, buf + done, 512 - done);
        if (n > 0) { done += (size_t)n; continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        close(fd);
        return -1;
    }
    close(fd);

    if (done < 512) return -1;

    /* Hex-encode: each byte → 2 hex chars */
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < 512; i++) {
        out_hex[i * 2]     = hex_chars[(buf[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex_chars[buf[i] & 0xF];
    }
    out_hex[1024] = '\0';
    return 0;
}

int current_path_slot_preview(const daemon_config *cfg, char *out_preview, size_t out_len) {
    if (!cfg || !out_preview || out_len < 129) return -1;

    char hex[1025];
    if (current_path_slot_hex(cfg, hex, sizeof(hex)) != 0) {
        out_preview[0] = '\0';
        return -1;
    }

    /* Take first 256 hex chars (= 128 bytes = 64 UTF-16LE chars) for preview */
    size_t preview_hex_len = 256;
    if (preview_hex_len > strlen(hex)) preview_hex_len = strlen(hex);

    /* Decode the preview portion */
    char decoded[128];
    size_t di = 0;
    for (size_t i = 0; i + 3 < preview_hex_len && di < 127; i += 4) {
        /* Read two bytes (4 hex chars) as UTF-16LE */
        unsigned int lo, hi;
        char b0[3] = { hex[i], hex[i+1], '\0' };
        char b1[3] = { hex[i+2], hex[i+3], '\0' };
        lo = (unsigned int)strtoul(b0, NULL, 16);
        hi = (unsigned int)strtoul(b1, NULL, 16);

        if (lo == 0 && hi == 0) {
            if (di > 0) break;
            continue;
        }
        if (hi == 0) {
            decoded[di++] = (char)lo;
        } else {
            decoded[di++] = '?';
        }
    }
    decoded[di] = '\0';

    /* Apply path fix-up: if starts with ":\\" prepend "a" */
    if (di >= 2 && decoded[0] == ':' && decoded[1] == '\\') {
        if (out_len < di + 2) {
            strncpy(out_preview, decoded, out_len - 1);
            out_preview[out_len - 1] = '\0';
        } else {
            out_preview[0] = 'a';
            memcpy(out_preview + 1, decoded, di + 1);
        }
    } else if (di >= 11 && strncmp(decoded, "\\Audiobooks\\", 11) == 0) {
        /* Prepend "a:" */
        if (out_len < di + 3) {
            strncpy(out_preview, decoded, out_len - 1);
            out_preview[out_len - 1] = '\0';
        } else {
            out_preview[0] = 'a';
            out_preview[1] = ':';
            memcpy(out_preview + 2, decoded, di + 1);
        }
    } else {
        strncpy(out_preview, decoded, out_len - 1);
        out_preview[out_len - 1] = '\0';
    }

    return 0;
}

int decode_path_slot_hex(const char *hex, char *out_path, size_t out_len) {
    if (!hex || !out_path || out_len < 2) return -1;

    size_t hex_len = strlen(hex);
    if (hex_len < 4) {
        out_path[0] = '\0';
        return -1;
    }

    size_t di = 0;
    for (size_t i = 0; i + 3 < hex_len && di < out_len - 1; i += 4) {
        unsigned int lo, hi;
        char b0[3] = { hex[i], hex[i+1], '\0' };
        char b1[3] = { hex[i+2], hex[i+3], '\0' };
        lo = (unsigned int)strtoul(b0, NULL, 16);
        hi = (unsigned int)strtoul(b1, NULL, 16);

        if (lo == 0 && hi == 0) {
            if (di > 0) break;
            continue;
        }
        if (hi == 0) {
            out_path[di++] = (char)lo;
        } else {
            out_path[di++] = '?';
        }
    }
    out_path[di] = '\0';

    /* Apply path fix-up */
    if (di >= 2 && out_path[0] == ':' && out_path[1] == '\\') {
        /* Prepend "a" */
        if (di + 1 < out_len) {
            memmove(out_path + 1, out_path, di + 1);
            out_path[0] = 'a';
        }
    } else if (di >= 11 && strncmp(out_path, "\\Audiobooks\\", 11) == 0) {
        /* Prepend "a:" */
        if (di + 2 < out_len) {
            memmove(out_path + 2, out_path, di + 1);
            out_path[0] = 'a';
            out_path[1] = ':';
        }
    } else if (di >= 6 && strncmp(out_path, "\\Music\\", 6) == 0) {
        if (di + 2 < out_len) {
            memmove(out_path + 2, out_path, di + 1);
            out_path[0] = 'a';
            out_path[1] = ':';
        }
    }

    return 0;
}

int current_path_from_hex(const daemon_config *cfg, char *out_path, size_t out_len) {
    if (!cfg || !out_path || out_len < 512) return -1;

    char hex[1025];
    if (current_path_slot_hex(cfg, hex, sizeof(hex)) != 0) {
        out_path[0] = '\0';
        return -1;
    }

    return decode_path_slot_hex(hex, out_path, out_len);
}

/* ── Path classification ───────────────────────────────────────────── */

bool path_preview_is_audiobook(const char *preview) {
    if (!preview || !preview[0]) return false;

    /* Match: a:\Audiobooks\*, :\Audiobooks\*, \Audiobooks\* (case-insensitive drive) */
    if (strncasecmp(preview, "a:\\Audiobooks\\", 14) == 0) return true;
    if (strncasecmp(preview, ":\\Audiobooks\\", 13) == 0) return true;
    if (strncasecmp(preview, "\\Audiobooks\\", 12) == 0) return true;

    return false;
}

bool path_preview_is_music(const char *preview) {
    if (!preview || !preview[0]) return false;

    if (strncasecmp(preview, "a:\\Music\\", 8) == 0) return true;
    if (strncasecmp(preview, ":\\Music\\", 7) == 0) return true;
    if (strncasecmp(preview, "\\Music\\", 6) == 0) return true;

    return false;
}

bool path_slot_hex_is_audiobook(const char *hex) {
    if (!hex) return false;

    /* Decode the hex and check the decoded string */
    char decoded[512];
    if (decode_path_slot_hex(hex, decoded, sizeof(decoded)) != 0) return false;
    return path_preview_is_audiobook(decoded);
}

/* ── Book-title marker polling ────────────────────────────────────── */

uint32_t book_title_marker_seq(const daemon_config *cfg) {
    if (!cfg || cfg->book_title_marker_addr < 4096) return 0;

    int fd = get_mem_fd(cfg);
    if (fd < 0) return 0;

    /* Read the magic value at marker_addr */
    uint32_t magic = 0;
    if (proc_mem_read_u32le(fd, cfg->book_title_marker_addr, &magic) != 0) {
        proc_mem_close(cached_mem_fd);
        cached_mem_fd = -1;
        cached_mem_pid = -1;
        player_invalidate_pid();
        return 0;
    }

    /* Check the source magic */
    if (magic != cfg->book_title_source_magic) {
        return 0;
    }

    /* Read the sequence number at marker_addr + 40 */
    uint32_t seq = 0;
    if (proc_mem_read_u32le(fd, cfg->book_title_marker_addr + 40, &seq) != 0) {
        return 0;
    }

    return seq;
}

/* ── Memscan root lookup ──────────────────────────────────────────── */

int book_title_memscan_root(const daemon_config *cfg, pid_t pid,
                            const struct catalog_db *cat,
                            char *out_root, size_t out_len) {
    if (!cfg || !out_root || out_len < 4 || pid < 0) return -1;

    /* Try the memscan helper first */
    if (cfg->memscan_helper_path[0] && cfg->catalog_books_path[0]) {
        int rc = helpers_memscan_root(pid, cfg->memscan_helper_path,
                                      cfg->catalog_books_path,
                                      out_root, out_len,
                                      cfg->helper_timeout_seconds);
        if (rc == 0 && out_root[0]) {
            return 0;
        }
    }

    /* Fallback: scan player memory for catalog paths */
    if (cat && cfg->book_title_memscan_addr >= 4096 && cfg->book_title_memscan_bytes > 0) {
        int fd = proc_mem_open(pid);
        if (fd < 0) return -1;

        int rc = proc_mem_first_catalog_path(fd, cfg->book_title_memscan_addr,
                                             cfg->book_title_memscan_bytes,
                                             cat, out_root, out_len);
        proc_mem_close(fd);

        if (rc == 0) return 0;
    }

    return -1;
}