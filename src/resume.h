/*
 * resume.h — resume record CRUD, JSON serialization, completion detection,
 *            save bucketing, restore target computation, failure tracking
 *
 * Spec section 2.2 (resume_record), section 12 (JSON format),
 * section 3.2 (save/restore logic).
 */

#ifndef RESUME_H
#define RESUME_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"

/* ── Resume record (spec 2.2) ────────────────────────────────────── */

typedef struct {
    int      schema_version;    /* always 3 */
    char     book_id[256];      /* safe_id(root) */
    char     book_key[128];
    char     root_hiby_path[512];
    char     current_path[512];
    int      media_id;          /* -1 = null */
    int      track_index;       /* -1 = null */
    int      track_count;       /* -1 = null */
    char     chapter_title[256];
    uint32_t position_ms;
    char     updated_at[32];    /* ISO 8601 UTC */
    bool     completed;
} resume_record;

/* ── JSON helpers ─────────────────────────────────────────────────── */

/* Escape backslashes and double quotes in a string for JSON output.
 * out must have enough space (worst case 2*len + 1).
 * Returns the length of the escaped string. */
size_t json_escape(const char *in, char *out, size_t out_len);

/* Parse a JSON string value for a given key from a JSON body.
 * Returns 0 on success, -1 if key not found. */
int json_value(const char *json, const char *key, char *out, size_t out_len);

/* Parse a JSON numeric value for a given key.
 * Returns 0 on success, -1 if key not found. */
int json_number(const char *json, const char *key, int *out);

/* Parse a JSON boolean value for a given key.
 * Returns 0 on success, -1 if key not found. */
int json_bool(const char *json, const char *key, bool *out);

/* ── safe_id ──────────────────────────────────────────────────────── */

/* Generate a safe file ID from a path by replacing non-alphanumeric
 * characters with underscores.  out must be at least as long as in. */
void safe_id(const char *in, char *out, size_t out_len);

/* ── Record path resolution ─────────────────────────────────────── */

/* Resolve the file path for a resume record given the book path.
 * Uses book_key if available, otherwise falls back to safe_id(root).
 * Writes the full file path to out_path.
 * Returns 0 on success. */
int record_for_path(const daemon_config *cfg, const char *path,
                    const char *book_key, const char *root,
                    char *out_path, size_t out_len);

/* Read an existing resume record for a path.
 * Returns 0 on success (record filled), -1 if no record exists. */
int existing_record_for_path(const daemon_config *cfg, const char *path,
                             const char *book_key, const char *root,
                             resume_record *rec);

/* ── Completion detection ────────────────────────────────────────── */

/* Check if a position near the end of a track/book indicates completion.
 * Returns true if the position is within completed_end_threshold_ms of the end. */
bool completion_state_for_path_position(const daemon_config *cfg,
                                        const char *path, uint32_t position_ms);

/* ── Save / restore ──────────────────────────────────────────────── */

/* Save a position to a resume record file.
 * Creates or updates the record atomically (write to temp, rename).
 * Returns 0 on success, -1 on error. */
int save_position(const daemon_config *cfg, const char *path,
                  uint32_t position_ms, const resume_record *template_rec);

/* Compute the restore target position (position - rewind, clamped).
 * Returns the target position in ms. */
uint32_t restore_target_ms(const daemon_config *cfg, uint32_t saved_position_ms);

/* Attempt to restore position for a path.
 * Returns 0 on success, -1 if no restore needed or failed. */
int maybe_restore(const daemon_config *cfg, const char *path,
                 uint32_t current_position_ms,
                 const char *book_key, const char *root);

/* Attempt track restore for a path.
 * Returns 0 on success, -1 if not needed or failed. */
int maybe_restore_track(const daemon_config *cfg, const char *current_path,
                        const char *saved_path, const char *book_key,
                        const char *root);

/* ── Failure tracking ────────────────────────────────────────────── */

/* Note a seek restore failure.  Updates failure tracking state. */
void note_seek_restore_failure(const char *path, uint32_t saved_pos,
                               const char *key);

/* Note a track restore failure.  Updates failure tracking state. */
void note_track_restore_failure(const char *path);

/* Compute the retry delay (in seconds) for the current failure state.
 * Uses exponential backoff: base * 2^(count-1), capped at max. */
uint32_t restore_retry_delay_seconds(const daemon_config *cfg);

/* ── Save decision helpers ────────────────────────────────────────── */

/* Check if we should defer saving because the user just switched tracks.
 * Returns true if the new track hasn't been committed yet. */
bool should_defer_new_track_save(const daemon_config *cfg,
                                 const char *current_path,
                                 const char *last_saved_path,
                                 time_t last_save_time, time_t now);

/* Check if we should skip saving because we just restored a completed book. */
bool should_skip_after_completed_restore(const char *path,
                                         const char *completed_start_over_path);

/* Check if we should skip saving because the last restore failed. */
bool should_skip_failed_restore_save(const daemon_config *cfg,
                                     const char *path,
                                     uint32_t current_position_ms);

/* Check if a restore should be attempted for the current position. */
bool should_attempt_restore_for_position(const daemon_config *cfg,
                                         uint32_t position_ms,
                                         bool autostart_active);

/* ── Accessors for testing ────────────────────────────────────────── */

/* Reset failure tracking state (for testing). */
void resume_reset_failures(void);

/* Get current seek failure count (for testing). */
int resume_get_seek_failure_count(void);

/* Get failure path (for testing). */
const char *resume_get_failure_path(void);

/* Get failure kind (for testing). */
const char *resume_get_failure_kind(void);

#endif /* RESUME_H */