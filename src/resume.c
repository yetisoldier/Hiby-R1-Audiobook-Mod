/*
 * resume.c — resume record CRUD, JSON serialization, completion detection,
 *            save bucketing, restore target computation, failure tracking,
 *            deferred save logic
 *
 * Spec sections 2.2, 12, 3.2.
 */

#include "resume.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

/* ── JSON helpers ─────────────────────────────────────────────────── */

size_t json_escape(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len == 0) return 0;
    size_t di = 0;
    for (size_t si = 0; in[si] && di < out_len - 1; si++) {
        if (in[si] == '\\' || in[si] == '"') {
            if (di + 1 >= out_len - 1) break;
            out[di++] = '\\';
            out[di++] = in[si];
        } else {
            out[di++] = in[si];
        }
    }
    out[di] = '\0';
    return di;
}

/* Find a JSON field value by key.  Handles simple flat JSON objects.
   Looks for "key" : value patterns. */
static const char *find_json_value(const char *json, const char *key) {
    if (!json || !key) return NULL;

    /* Build search pattern: "key" */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        /* Move past the key */
        const char *after = p + strlen(pattern);

        /* Skip whitespace */
        while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') after++;

        /* Expect colon */
        if (*after != ':') {
            p = after;
            continue;
        }
        after++;

        /* Skip whitespace */
        while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') after++;

        return after;
    }
    return NULL;
}

int json_value(const char *json, const char *key, char *out, size_t out_len) {
    if (!out || out_len == 0) return -1;
    out[0] = '\0';

    const char *v = find_json_value(json, key);
    if (!v) return -1;

    if (*v == '"') {
        /* String value — handle escapes */
        v++;
        size_t di = 0;
        while (*v && di < out_len - 1) {
            if (*v == '\\' && *(v+1)) {
                v++;
                if (di < out_len - 1) out[di++] = *v;
                v++;
            } else if (*v == '"') {
                break;
            } else {
                out[di++] = *v++;
            }
        }
        out[di] = '\0';
        return 0;
    } else {
        /* Non-string value — copy until comma, brace, or end */
        size_t di = 0;
        while (*v && *v != ',' && *v != '}' && *v != '\n' && *v != '\r' &&
               di < out_len - 1) {
            out[di++] = *v++;
        }
        out[di] = '\0';
        /* Trim trailing spaces */
        while (di > 0 && (out[di-1] == ' ' || out[di-1] == '\t')) {
            out[--di] = '\0';
        }
        return 0;
    }
}

int json_number(const char *json, const char *key, int *out) {
    char buf[32];
    if (json_value(json, key, buf, sizeof(buf)) != 0) return -1;

    /* Handle "null" */
    if (strcmp(buf, "null") == 0) {
        *out = -1;
        return 0;
    }

    *out = atoi(buf);
    return 0;
}

int json_bool(const char *json, const char *key, bool *out) {
    char buf[16];
    if (json_value(json, key, buf, sizeof(buf)) != 0) return -1;

    *out = (buf[0] == 't' || buf[0] == 'T' || buf[0] == '1');
    return 0;
}

static void json_time_or_zero(const char *json, const char *key, time_t *out) {
    int v = -1;
    if (!out) return;
    if (json_number(json, key, &v) == 0 && v > 0) {
        *out = (time_t)v;
    } else {
        *out = 0;
    }
}

/* ── safe_id ──────────────────────────────────────────────────────── */

void safe_id(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len == 0) return;

    size_t di = 0;
    for (size_t si = 0; in[si] && di < out_len - 1; si++) {
        char c = in[si];
        if (isalnum((unsigned char)c)) {
            out[di++] = c;
        } else {
            out[di++] = '_';
        }
    }
    out[di] = '\0';
}

/* ── Record path resolution ───────────────────────────────────────── */

int record_for_path(const daemon_config *cfg, const char *path,
                    const char *book_key, const char *root,
                    char *out_path, size_t out_len) {
    if (!cfg || !out_path || out_len < 4) return -1;

    /* Determine the record filename */
    char id[256];

    if (book_key && book_key[0]) {
        /* Use book_key for the filename */
        safe_id(book_key, id, sizeof(id));
    } else if (root && root[0]) {
        /* Fall back to safe_id(root) */
        safe_id(root, id, sizeof(id));
    } else {
        /* Last resort: use path */
        safe_id(path, id, sizeof(id));
    }

    snprintf(out_path, out_len, "%s/%s.json", cfg->store_dir, id);
    return 0;
}

int existing_record_for_path(const daemon_config *cfg, const char *path,
                             const char *book_key, const char *root,
                             resume_record *rec) {
    if (!cfg || !path || !rec) return -1;

    char file_path[512];
    if (record_for_path(cfg, path, book_key, root, file_path, sizeof(file_path)) != 0) {
        return -1;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[4096];
    size_t total = 0;
    while (total < sizeof(buf) - 1) {
        ssize_t n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n > 0) { total += (size_t)n; continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fd);
    buf[total] = '\0';

    if (total == 0) return -1;

    /* Parse JSON fields */
    memset(rec, 0, sizeof(*rec));
    rec->schema_version = 3;

    char tmp[512];
    if (json_value(buf, "book_id", tmp, sizeof(tmp)) == 0)
        strncpy(rec->book_id, tmp, sizeof(rec->book_id) - 1);
    if (json_value(buf, "book_key", tmp, sizeof(tmp)) == 0)
        strncpy(rec->book_key, tmp, sizeof(rec->book_key) - 1);
    if (json_value(buf, "root_hiby_path", tmp, sizeof(tmp)) == 0)
        strncpy(rec->root_hiby_path, tmp, sizeof(rec->root_hiby_path) - 1);
    if (json_value(buf, "current_path", tmp, sizeof(tmp)) == 0)
        strncpy(rec->current_path, tmp, sizeof(rec->current_path) - 1);
    if (json_value(buf, "chapter_title", tmp, sizeof(tmp)) == 0)
        strncpy(rec->chapter_title, tmp, sizeof(rec->chapter_title) - 1);
    if (json_value(buf, "updated_at", tmp, sizeof(tmp)) == 0)
        strncpy(rec->updated_at, tmp, sizeof(rec->updated_at) - 1);

    json_number(buf, "media_id", &rec->media_id);
    json_number(buf, "track_index", &rec->track_index);
    json_number(buf, "track_count", &rec->track_count);
    json_number(buf, "schema_version", &rec->schema_version);

    char numbuf[32];
    if (json_value(buf, "position_ms", numbuf, sizeof(numbuf)) == 0)
        rec->position_ms = (uint32_t)strtoul(numbuf, NULL, 10);

    json_time_or_zero(buf, "last_played_at", &rec->last_played_at);
    json_time_or_zero(buf, "completed_at", &rec->completed_at);

    bool bval = false;
    json_bool(buf, "completed", &bval);
    rec->completed = bval;

    return 0;
}

/* ── Completion detection ─────────────────────────────────────────── */

bool completion_state_for_path_position(const daemon_config *cfg,
                                        const char *path, uint32_t position_ms) {
    if (!cfg || !path) return false;

    /* Completion is detected when position is within
     * completed_end_threshold_ms of the end of the last track. */
    /* Without duration info, we can only check against the threshold itself. */
    (void)path;
    (void)position_ms;

    /* In the shell daemon, completion is determined by:
     * 1. Being on the last track (track_index == track_count)
     * 2. Position being within completed_end_threshold_ms of duration
     * Since we don't have track info here, this is a simplified check. */
    return false;
}

/* ── Save position ────────────────────────────────────────────────── */

static void generate_timestamp_utc(char *out, size_t out_len, time_t now) {
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

int save_position(const daemon_config *cfg, const char *path,
                  uint32_t position_ms, const resume_record *template_rec) {
    if (!cfg || !path) return -1;

    /* Determine record file path */
    char file_path[512];
    const char *book_key = (template_rec && template_rec->book_key[0]) ?
                           template_rec->book_key : "";
    const char *root = (template_rec && template_rec->root_hiby_path[0]) ?
                       template_rec->root_hiby_path : "";

    if (record_for_path(cfg, path, book_key, root, file_path, sizeof(file_path)) != 0) {
        return -1;
    }

    /* Generate timestamp */
    time_t now = time(NULL);
    char timestamp[32];
    generate_timestamp_utc(timestamp, sizeof(timestamp), now);

    /* Build the JSON record */
    char json[4096];
    int pos = 0;

    pos += snprintf(json + pos, sizeof(json) - pos, "{\n");

    /* schema_version */
    int sv = template_rec ? template_rec->schema_version : 3;
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"schema_version\": %d,\n", sv);

    /* book_id */
    char escaped[1024];
    char book_id[256] = "";
    if (template_rec && template_rec->book_id[0]) {
        strncpy(book_id, template_rec->book_id, sizeof(book_id) - 1);
    } else if (root[0]) {
        safe_id(root, book_id, sizeof(book_id));
    }
    json_escape(book_id, escaped, sizeof(escaped));
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"book_id\": \"%s\",\n", escaped);

    /* book_key */
    if (book_key && book_key[0]) {
        json_escape(book_key, escaped, sizeof(escaped));
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"book_key\": \"%s\",\n", escaped);
    } else {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"book_key\": null,\n");
    }

    /* root_hiby_path */
    json_escape(root, escaped, sizeof(escaped));
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"root_hiby_path\": \"%s\",\n", escaped);

    /* current_path */
    json_escape(path, escaped, sizeof(escaped));
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"current_path\": \"%s\",\n", escaped);

    /* media_id */
    int media_id = template_rec ? template_rec->media_id : -1;
    if (media_id >= 0) {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"media_id\": %d,\n", media_id);
    } else {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"media_id\": null,\n");
    }

    /* track_index */
    int track_index = template_rec ? template_rec->track_index : -1;
    if (track_index >= 0) {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"track_index\": %d,\n", track_index);
    } else {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"track_index\": null,\n");
    }

    /* track_count */
    int track_count = template_rec ? template_rec->track_count : -1;
    if (track_count >= 0) {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"track_count\": %d,\n", track_count);
    } else {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"track_count\": null,\n");
    }

    /* chapter_title */
    const char *ch = template_rec ? template_rec->chapter_title : "";
    if (ch && ch[0]) {
        json_escape(ch, escaped, sizeof(escaped));
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"chapter_title\": \"%s\",\n", escaped);
    } else {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"chapter_title\": \"\",\n");
    }

    /* position_ms */
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"position_ms\": %u,\n", position_ms);

    /* last_played_at */
    time_t last_played_at = (template_rec && template_rec->last_played_at > 0)
                                ? template_rec->last_played_at : now;
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"last_played_at\": %ld,\n",
                    (long)last_played_at);

    /* updated_at */
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"updated_at\": \"%s\",\n", timestamp);

    /* completed_at / completed */
    if (template_rec && template_rec->completed) {
        time_t completed_at = template_rec->completed_at > 0 ? template_rec->completed_at : now;
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"completed_at\": %ld,\n",
                        (long)completed_at);
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"completed\": true\n");
    } else {
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"completed_at\": null,\n");
        pos += snprintf(json + pos, sizeof(json) - pos, "  \"completed\": false\n");
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "}\n");

    /* Write atomically: temp file + rename */
    char tmp_path[520];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);

    /* Ensure store dir exists */
    char dir[512];
    strncpy(dir, cfg->store_dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    mkdir(dir, 0755);  /* ignore error if exists */

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        log_msg("save_position: cannot create %s: %s", tmp_path, strerror(errno));
        return -1;
    }

    size_t remaining = (size_t)pos;
    const char *p = json;
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp_path);
            log_msg("save_position: write error: %s", strerror(errno));
            return -1;
        }
        p += n;
        remaining -= (size_t)n;
    }
    close(fd);

    if (rename(tmp_path, file_path) != 0) {
        log_msg("save_position: rename %s -> %s: %s", tmp_path, file_path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

/* ── Restore target computation ───────────────────────────────────── */

static uint32_t smart_rewind_ms_for_resume(const daemon_config *cfg,
                                            time_t last_played_at) {
    if (!cfg) return 0;

    if (!cfg->smart_rewind_enabled) {
        return cfg->restore_rewind_ms;
    }

    if (last_played_at <= 0) {
        return cfg->rewind_long_ms;
    }

    time_t now = time(NULL);
    if (now <= last_played_at) {
        return 0;
    }

    uint32_t paused_seconds = (uint32_t)(now - last_played_at);
    if (paused_seconds < 300) {
        return 0;
    }
    if (paused_seconds < 3600) {
        return cfg->rewind_short_ms;
    }
    if (paused_seconds < 86400) {
        return cfg->rewind_medium_ms;
    }
    return cfg->rewind_long_ms;
}

uint32_t restore_target_ms(const daemon_config *cfg, uint32_t saved_position_ms,
                           time_t last_played_at) {
    if (!cfg) return saved_position_ms;

    uint32_t rewind_ms = smart_rewind_ms_for_resume(cfg, last_played_at);
    if (saved_position_ms <= rewind_ms) {
        return 0;
    }

    return saved_position_ms - rewind_ms;
}

/* ── Restore logic ────────────────────────────────────────────────── */

int maybe_restore(const daemon_config *cfg, const char *path,
                 uint32_t current_position_ms,
                 const char *book_key, const char *root) {
    if (!cfg || !path) return -1;

    if (!cfg->restore_enabled) return -1;

    /* Check if we should attempt restore for this position */
    if (!should_attempt_restore_for_position(cfg, current_position_ms, false)) {
        return -1;
    }

    /* Load existing record */
    resume_record rec;
    if (existing_record_for_path(cfg, path, book_key, root, &rec) != 0) {
        return -1;  /* no record to restore */
    }

    /* If already completed, don't restore */
    if (rec.completed) {
        return -1;
    }

    /* Check if position is before the saved position */
    if (current_position_ms >= rec.position_ms) {
        return -1;  /* already past the saved position */
    }

    /* Compute restore target */
    uint32_t target = restore_target_ms(cfg, rec.position_ms, rec.last_played_at);

    /* In a full implementation, this would call the seek helper or UI seek.
     * For Phase 2, we just log the intent. */
    log_msg("restore: would seek to %u ms (saved=%u, current=%u)",
            target, rec.position_ms, current_position_ms);

    return 0;
}

int maybe_restore_track(const daemon_config *cfg, const char *current_path,
                        const char *saved_path, const char *book_key,
                        const char *root) {
    (void)book_key;
    (void)root;
    if (!cfg || !current_path || !saved_path) return -1;

    if (!cfg->restore_enabled || !cfg->track_restore_enabled) return -1;

    /* If paths are the same, no track restore needed */
    if (strcmp(current_path, saved_path) == 0) {
        return -1;
    }

    /* TODO: Full track restore orchestration is in state.c (Phase 3).
     * Phase 2 just logs the intent. */
    log_msg("restore_track: current=%s saved=%s", current_path, saved_path);

    return -1;
}

/* ── Failure tracking ────────────────────────────────────────────── */

/* Module-level failure state */
static char    failure_path[512] = "";
static char    failure_kind[16]  = "";   /* "seek" or "track" */
static time_t  failure_time      = 0;
static uint32_t failure_saved_pos = 0;
static char    failure_key[128]  = "";
static int     seek_failure_count = 0;

void note_seek_restore_failure(const char *path, uint32_t saved_pos,
                               const char *key) {
    if (!path) return;

    strncpy(failure_path, path, sizeof(failure_path) - 1);
    failure_path[sizeof(failure_path) - 1] = '\0';
    failure_path[sizeof(failure_path) - 1] = '\0';

    strncpy(failure_kind, "seek", sizeof(failure_kind) - 1);
    failure_kind[sizeof(failure_kind) - 1] = '\0';

    failure_time = time(NULL);
    failure_saved_pos = saved_pos;

    if (key) {
        strncpy(failure_key, key, sizeof(failure_key) - 1);
        failure_key[sizeof(failure_key) - 1] = '\0';
        failure_key[sizeof(failure_key) - 1] = '\0';
    } else {
        failure_key[0] = '\0';
    }

    /* Only increment if this is a seek failure (not a track failure masquerading) */
    seek_failure_count++;
}

void note_track_restore_failure(const char *path) {
    if (!path) return;

    strncpy(failure_path, path, sizeof(failure_path) - 1);
    failure_path[sizeof(failure_path) - 1] = '\0';
    failure_path[sizeof(failure_path) - 1] = '\0';

    strncpy(failure_kind, "track", sizeof(failure_kind) - 1);
    failure_kind[sizeof(failure_kind) - 1] = '\0';

    failure_time = time(NULL);
    failure_saved_pos = 0;

    /* Track failures reset seek failure state */
    seek_failure_count = 0;
}

uint32_t restore_retry_delay_seconds(const daemon_config *cfg) {
    if (!cfg) return 0;
    if (seek_failure_count <= 0) return 0;

    /* Exponential backoff: base * 2^(count-1), capped at max */
    uint32_t base = cfg->restore_retry_after_failure_seconds;
    uint32_t max  = cfg->restore_retry_max_after_failure_seconds;

    uint32_t delay = base;
    for (int i = 1; i < seek_failure_count; i++) {
        if (delay > max / 2) {
            delay = max;
            break;
        }
        delay *= 2;
    }

    if (delay > max) delay = max;
    return delay;
}

/* ── Save decision helpers ────────────────────────────────────────── */

bool should_defer_new_track_save(const daemon_config *cfg,
                                 const char *current_path,
                                 const char *last_saved_path,
                                 time_t last_save_time, time_t now) {
    if (!cfg || !current_path) return false;

    /* If no previous save, don't defer */
    if (!last_saved_path || !last_saved_path[0]) return false;

    /* If same path, don't defer */
    if (strcmp(current_path, last_saved_path) == 0) return false;

    /* If we haven't been on this track long enough, defer */
    if (now - last_save_time < (time_t)(cfg->new_track_commit_ms / 1000)) {
        return true;
    }

    return false;
}

bool should_skip_after_completed_restore(const char *path,
                                         const char *completed_start_over_path) {
    if (!path || !completed_start_over_path) return false;
    return strcmp(path, completed_start_over_path) == 0;
}

bool should_skip_failed_restore_save(const daemon_config *cfg,
                                     const char *path,
                                     uint32_t current_position_ms) {
    if (!cfg || !path) return false;

    /* If no failure, don't skip */
    if (!failure_path[0] || strcmp(path, failure_path) != 0) {
        return false;
    }

    /* If this is a seek failure and current position is less than saved,
     * skip the save to avoid overwriting a deeper bookmark */
    if (strcmp(failure_kind, "seek") == 0 && current_position_ms < failure_saved_pos) {
        /* Check if we're still in the backoff window */
        uint32_t delay = restore_retry_delay_seconds(cfg);
        time_t now = time(NULL);
        if (now - failure_time < (time_t)delay) {
            return true;
        }
    }

    return false;
}

bool should_attempt_restore_for_position(const daemon_config *cfg,
                                         uint32_t position_ms,
                                         bool autostart_active) {
    if (!cfg) return false;
    if (!cfg->restore_enabled) return false;

    /* Only restore if position is before the threshold */
    if (position_ms > cfg->restore_only_before_ms) {
        if (!autostart_active) return false;
    }

    /* Don't restore if position is too small */
    if (position_ms < cfg->restore_min_ms) return false;

    return true;
}

/* ── Reset failure state (for testing) ────────────────────────────── */

void resume_reset_failures(void) {
    failure_path[0] = '\0';
    failure_kind[0] = '\0';
    failure_time = 0;
    failure_saved_pos = 0;
    failure_key[0] = '\0';
    seek_failure_count = 0;
}

/* ── Accessors for testing ────────────────────────────────────────── */

int resume_get_seek_failure_count(void) {
    return seek_failure_count;
}

const char *resume_get_failure_path(void) {
    return failure_path;
}

const char *resume_get_failure_kind(void) {
    return failure_kind;
}