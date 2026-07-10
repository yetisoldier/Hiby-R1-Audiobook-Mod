/*
 * shadow.h — shadow-mode wrappers for migration validation
 *
 * Spec section 11 (Migration Plan), section 11.5 (Shadow Mode Implementation).
 *
 * Shadow mode allows the C daemon to run alongside the shell daemon,
 * logging what it *would* do without performing any UI actions or writes.
 * Read-only operations (memory reads, path detection, catalog lookups,
 * position reads) execute normally; all side-effect-producing functions
 * are intercepted and logged instead.
 *
 * When shadow_mode is OFF (0), every wrapper calls the real function
 * with zero overhead beyond a single flag check.
 */

#ifndef SHADOW_H
#define SHADOW_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "resume.h"

/* ── Shadow mode control ─────────────────────────────────────────── */

/*
 * Enable shadow mode (sets config.shadow_mode = 1).
 * After calling, all shadow_wrap_* functions will log instead of acting.
 */
void shadow_init(daemon_config *cfg);

/*
 * Returns true if shadow mode is active (cfg->shadow_mode != 0).
 * This is the gate check used by all wrapper functions.
 */
bool shadow_is_active(const daemon_config *cfg);

/* ── Action logging ──────────────────────────────────────────────── */

/*
 * Log a "WOULD DO: <action> <details>" message.
 * Uses the standard log system.
 */
void shadow_log_action(const char *action, const char *fmt, ...);

/* ── Save wrapper ────────────────────────────────────────────────── */

/*
 * If shadow mode is active, logs what would be saved instead of writing
 * a resume record.  Returns 0 (simulated success).
 * If shadow mode is inactive, calls save_position() for real.
 */
int shadow_wrap_save(const daemon_config *cfg, const char *path,
                     uint32_t position_ms, const resume_record *template_rec);

/* ── Restore wrapper ────────────────────────────────────────────── */

/*
 * If shadow mode is active, logs what the restore target would be
 * instead of seeking.  Returns 0 (simulated success).
 * If shadow mode is inactive, calls maybe_restore() for real.
 */
int shadow_wrap_restore(const daemon_config *cfg, const char *path,
                        uint32_t current_position_ms,
                        const char *book_key, const char *root);

/* ── UI action wrapper ───────────────────────────────────────────── */

/*
 * If shadow mode is active, logs what UI action would be taken
 * (label + details) instead of executing it.  Returns 0.
 * If shadow mode is inactive, calls the provided real_action function
 * and returns its result.
 *
 * This is a generic wrapper for any UI operation (touch taps,
 * event file sends, play-mode enforcement, etc.)
 */
typedef int (*ui_action_fn)(const daemon_config *cfg);
int shadow_wrap_ui(const daemon_config *cfg, const char *label,
                   ui_action_fn real_action);

/*
 * Convenience wrapper for UI actions with an extra detail string.
 * Logs "WOULD DO: <label> <detail>" in shadow mode.
 */
typedef int (*ui_action_detail_fn)(const daemon_config *cfg, const char *detail);
int shadow_wrap_ui_detail(const daemon_config *cfg, const char *label,
                          const char *detail, ui_action_detail_fn real_action);

/* ── Play mode wrapper ───────────────────────────────────────────── */

/*
 * If shadow mode is active, logs what play mode change would be made
 * instead of enforcing it.  Returns 0.
 * If shadow mode is inactive, calls ensure_audiobook_play_mode() for real.
 */
int shadow_wrap_play_mode(const daemon_config *cfg);

#endif /* SHADOW_H */