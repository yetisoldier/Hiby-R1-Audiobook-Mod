/*
 * player.h — hiby_player PID discovery, position/duration reads,
 *             path slot decoding, book-title marker polling
 *
 * Spec section 2.3 (book_context), section 13 (path slot decoding),
 * section 4.3 (memscan helper integration).
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include "config.h"
#include "proc_mem.h"

/* Forward declaration — catalog_db defined in catalog.h */
struct catalog_db;

/* ── Book context (spec 2.3) ──────────────────────────────────────── */

typedef struct {
    char     path[512];         /* full hiby path e.g. a:\Audiobooks\Book\03.mp3 */
    char     root[512];         /* book root e.g. a:\Audiobooks\Book */
    char     path_preview[128]; /* first 128 chars of user.ini slot */
    uint32_t position_ms;
    uint32_t duration_ms;
    int      track_index;       /* from catalog, -1 if unknown */
    int      track_count;       /* from catalog, -1 if unknown */
    int      media_id;          /* from catalog, -1 if unknown */
    char     chapter_title[256];
    char     book_key[128];
} book_context;

/* ── PID discovery ───────────────────────────────────────────────── */

/* Scan /proc for a process whose cmdline contains "hiby_player".
 * Returns the PID, or -1 if not found. */
pid_t player_pid(void);

/* Return cached PID, re-scanning if the cache is stale or invalid.
 * Returns -1 if hiby_player is not running. */
pid_t player_pid_cached(void);

/* Invalidate the PID cache (call when a read fails with ESRCH). */
void player_invalidate_pid(void);

/* ── Position / duration reads ────────────────────────────────────── */

/* Read the current position in milliseconds from player memory.
 * Returns the position, or 0 on error. */
uint32_t position_ms_memory(const daemon_config *cfg);

/* Read the total duration in milliseconds from player memory.
 * Returns the duration, or 0 on error. */
uint32_t duration_ms_memory(const daemon_config *cfg);

/* ── Path slot reading ───────────────────────────────────────────── */

/* Read 512 bytes at offset 40 from user.ini, hex-encode them.
 * out_hex must be at least 1025 bytes.
 * Returns 0 on success, -1 on error. */
int current_path_slot_hex(const daemon_config *cfg, char *out_hex, size_t out_len);

/* Read the first 128 chars of the user.ini path slot (hex preview).
 * out_preview must be at least 129 bytes.
 * Returns 0 on success, -1 on error. */
int current_path_slot_preview(const daemon_config *cfg, char *out_preview, size_t out_len);

/* Decode a hex-encoded path slot (1024 hex chars = 512 bytes UTF-16LE)
 * into a UTF-8/ASCII string.  out_path must be at least 512 bytes.
 * Returns 0 on success, -1 on error. */
int decode_path_slot_hex(const char *hex, char *out_path, size_t out_len);

/* Read and decode the full current path from user.ini.
 * out_path must be at least 512 bytes.
 * Returns 0 on success, -1 on error. */
int current_path_from_hex(const daemon_config *cfg, char *out_path, size_t out_len);

/* ── Path classification ─────────────────────────────────────────── */

/* Check if a decoded path preview is an audiobook path. */
bool path_preview_is_audiobook(const char *preview);

/* Check if a decoded path preview is a music path. */
bool path_preview_is_music(const char *preview);

/* Check if a hex-encoded path slot contains an audiobook path. */
bool path_slot_hex_is_audiobook(const char *hex);

/* ── Book-title marker polling ───────────────────────────────────── */

/* Read the book-title marker sequence number.
 * Returns the sequence, or 0 if the marker is not valid. */
uint32_t book_title_marker_seq(const daemon_config *cfg);

/* ── Memscan root lookup ─────────────────────────────────────────── */

/* Call the external memscan helper to find the book root in player memory.
 * Returns 0 on success (out_root filled), -1 on failure. */
int book_title_memscan_root(const daemon_config *cfg, pid_t pid,
                            const struct catalog_db *cat,
                            char *out_root, size_t out_len);

#endif /* PLAYER_H */