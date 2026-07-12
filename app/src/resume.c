#include "resume.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_atomic_text(const char *path, const char *text) {
    size_t path_len = strlen(path);
    size_t tmp_len = path_len + 32;
    char *tmp = malloc(tmp_len);
    if (!tmp) return -1;
    snprintf(tmp, tmp_len, "%s.tmp.%ld", path, (long)getpid());
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        free(tmp);
        return -1;
    }
    size_t len = strlen(text);
    ssize_t n = write(fd, text, len);
    if (n < 0 || (size_t)n != len || fsync(fd) != 0 || close(fd) != 0) {
        close(fd);
        unlink(tmp);
        free(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

void resume_init(resume_state *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

int resume_bind_book(resume_state *r, const book_row *book, const progress_row *progress) {
    if (!r || !book) return -1;
    memset(r, 0, sizeof(*r));
    r->book = *book;
    if (progress) r->progress = *progress;
    r->bound = true;
    return 0;
}

static bool smart_rewind_enabled = true;

uint32_t resume_smart_rewind_ms(uint64_t paused_seconds, bool rebooted, uint32_t saved_position_ms) {
    /* Only apply a rewind when smart rewind is enabled and we know how long
     * playback has been paused. saved_position_ms is intentionally not used for
     * tier decisions; it is just the return base. */
    (void)saved_position_ms;
    if (!smart_rewind_enabled) return 0;
    if (rebooted || paused_seconds <= 0) return 20000;
    if (paused_seconds < 300) return 0;
    if (paused_seconds < 3600) return 5000;
    if (paused_seconds < 86400) return 10000;
    return 20000;
}

int resume_on_event(resume_state *r, const audiobook_event *ev, progress_row *out) {
    if (!r || !ev || !out || !r->bound) return -1;
    if (ev->type == AB_EVT_BOOK_OPENED) {
        r->started = false;
        r->completed = false;
        r->started_at_ms = ab_now_ms();
        if (r->progress.protected_until_ms < (int64_t)r->started_at_ms) {
            r->progress.protected_until_ms = (int64_t)r->started_at_ms + 10000;
        }
    } else if (ev->type == AB_EVT_BOOK_COMPLETED || ev->type == AB_EVT_EOF_REACHED) {
        r->completed = true;
        r->progress.completed = 1;
        r->progress.completed_at = (int64_t)ab_now_ms();
        r->progress.protected_until_ms = 0;
    }
    if (ev->type == AB_EVT_PLAYBACK_STARTED || ev->type == AB_EVT_RESUMED) {
        r->started = true;
    }
    if (ev->type == AB_EVT_POSITION_TICK || ev->type == AB_EVT_SEEK_COMPLETE ||
        ev->type == AB_EVT_PAUSED || ev->type == AB_EVT_PLAYBACK_STARTED ||
        ev->type == AB_EVT_TRACK_CHANGED || ev->type == AB_EVT_RESUMED) {
        r->progress.track_ordinal = (int)ev->track_ordinal;
        r->progress.position_ms = (int64_t)ev->position_ms;
        r->progress.total_book_elapsed_ms = (int64_t)ev->position_ms;
        r->progress.playback_speed = (float)ev->playback_speed_x100 / 100.0f;
        r->progress.last_played_at = (int64_t)ab_now_ms();
        r->progress.last_saved_at = r->progress.last_played_at;
        r->progress.track_id = ev->track_id;
    }
    *out = r->progress;
    out->book_id = r->book.book_id;
    return 0;
}

int resume_write_record_atomic(const char *dir, const progress_row *progress, const book_row *book) {
    if (!dir || !progress || !book) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", dir, book->book_key);
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\n"
             "  \"book_id\": %lld,\n"
             "  \"book_key\": \"%s\",\n"
             "  \"track_ordinal\": %d,\n"
             "  \"position_ms\": %lld,\n"
             "  \"total_book_elapsed_ms\": %lld,\n"
             "  \"playback_speed\": %.2f,\n"
             "  \"completed\": %d,\n"
             "  \"completed_at\": %lld,\n"
             "  \"protected_until_ms\": %lld,\n"
             "  \"last_saved_at\": %lld\n"
             "}\n",
             (long long)progress->book_id,
             book->book_key,
             progress->track_ordinal,
             (long long)progress->position_ms,
             (long long)progress->total_book_elapsed_ms,
             progress->playback_speed,
             progress->completed,
             (long long)progress->completed_at,
             (long long)progress->protected_until_ms,
             (long long)progress->last_saved_at);
    return write_atomic_text(path, buf);
}

static const char *skip_ws(const char *p) {
    while (p && *p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':' || *p == ',')) {
        p++;
    }
    return p;
}

static long long parse_ll(const char *p) {
    return p ? strtoll(p, NULL, 10) : 0;
}

int resume_read_record(const char *path, progress_row *progress) {
    if (!path || !progress) return -1;
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    const char *colon;
    const char *p;
    p = strstr(buf, "\"book_id\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->book_id = parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"track_ordinal\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->track_ordinal = (int)parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"position_ms\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->position_ms = parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"total_book_elapsed_ms\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->total_book_elapsed_ms = parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"playback_speed\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->playback_speed = (float)strtod(skip_ws(colon + 1), NULL);
    p = strstr(buf, "\"completed\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->completed = (int)parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"completed_at\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->completed_at = parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"protected_until_ms\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->protected_until_ms = parse_ll(skip_ws(colon + 1));
    p = strstr(buf, "\"last_saved_at\"");
    colon = p ? strchr(p, ':') : NULL;
    if (colon) progress->last_saved_at = parse_ll(skip_ws(colon + 1));
    return 0;
}
