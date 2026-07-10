/*
 * log.c — structured logging with timestamps and size-based rotation
 *
 * Spec section 7.
 *   - Log lines: "YYYY-MM-DDTHH:MM:SS+ZZZZ message\n"
 *   - Rotation: rename log → log.1 when size >= max_bytes
 *   - Uses write() + vsnprintf() (no stdio buffering to log file)
 *   - Falls back to stderr if log file can't be opened
 */

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

static int  log_fd      = -1;
static char log_path[512] = "";
static uint32_t log_max  = 524288;  /* 512 KB default */

void log_init(const char *path, uint32_t max_bytes) {
    if (path && path[0]) {
        strncpy(log_path, path, sizeof(log_path) - 1);
        log_path[sizeof(log_path) - 1] = '\0';
    }
    if (max_bytes >= 1024) {
        log_max = max_bytes;
    }

    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        /* Fall back to stderr */
        log_fd = -1;
        dprintf(STDERR_FILENO, "log_init: cannot open %s: %s — falling back to stderr\n",
                log_path, strerror(errno));
    }
}

void log_close(void) {
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}

void log_rotate_if_needed(void) {
    if (log_fd < 0 || log_path[0] == '\0') return;

    struct stat st;
    if (fstat(log_fd, &st) != 0) return;
    if ((uint32_t)st.st_size < log_max) return;

    /* Close current log */
    close(log_fd);
    log_fd = -1;

    /* Rename log → log.1 (overwriting previous log.1) */
    char old_path[520];
    snprintf(old_path, sizeof(old_path), "%s.1", log_path);
    rename(log_path, old_path);

    /* Reopen */
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        log_fd = -1;
        dprintf(STDERR_FILENO, "log_rotate: cannot reopen %s: %s\n",
                log_path, strerror(errno));
        return;
    }

    /* Write rotation marker */
    char marker[256];
    int n = snprintf(marker, sizeof(marker),
                     "%s rotated log previous_size=%lld max=%u previous=%s\n",
                     "",  /* timestamp filled below */
                     (long long)st.st_size, log_max, old_path);

    /* Prepend timestamp to marker */
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime_r(&now, &tm));

    char full[300];
    int m = snprintf(full, sizeof(full), "%s %s", ts, marker + 0);
    if (m > 0 && (size_t)m < sizeof(full)) {
        write(log_fd, full, m);
    } else if (n > 0) {
        write(log_fd, marker, n);
    }
}

void log_msg(const char *fmt, ...) {
    char buf[1024];
    va_list ap;

    /* Timestamp */
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime_r(&now, &tm));

    int prefix_len = snprintf(buf, sizeof(buf), "%s ", ts);
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(buf)) {
        prefix_len = 0;
    }

    va_start(ap, fmt);
    int msg_len = vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len - 1, fmt, ap);
    va_end(ap);

    if (msg_len < 0) msg_len = 0;
    if ((size_t)(prefix_len + msg_len) >= sizeof(buf)) msg_len = sizeof(buf) - prefix_len - 1;

    buf[prefix_len + msg_len] = '\n';
    int total = prefix_len + msg_len + 1;

    /* Rotate before writing if needed */
    log_rotate_if_needed();

    if (log_fd >= 0) {
        write(log_fd, buf, total);
    } else {
        /* Fallback to stderr */
        write(STDERR_FILENO, buf, total);
    }
}

void log_stderr(const char *fmt, ...) {
    char buf[1024];
    va_list ap;

    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime_r(&now, &tm));

    int prefix_len = snprintf(buf, sizeof(buf), "%s ", ts);
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(buf)) prefix_len = 0;

    va_start(ap, fmt);
    int msg_len = vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len - 1, fmt, ap);
    va_end(ap);

    if (msg_len < 0) msg_len = 0;
    if ((size_t)(prefix_len + msg_len) >= sizeof(buf)) msg_len = sizeof(buf) - prefix_len - 1;

    buf[prefix_len + msg_len] = '\n';
    write(STDERR_FILENO, buf, prefix_len + msg_len + 1);
}