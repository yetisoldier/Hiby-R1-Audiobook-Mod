/*
 * shadow.c — shadow-mode wrapper implementation
 *
 * Spec section 11 (Migration Plan), section 11.5 (Shadow Mode Implementation).
 *
 * When shadow_mode is OFF, every wrapper delegates to the real function
 * with no overhead beyond a single flag check.
 * When shadow_mode is ON, wrappers log "WOULD DO: ..." messages and
 * return simulated success without performing any side effects.
 */

#include "shadow.h"
#include "log.h"
#include "resume.h"
#include "ui.h"

#include <stdarg.h>
#include <stdio.h>

/* ── Shadow mode control ─────────────────────────────────────────── */

void shadow_init(daemon_config *cfg) {
    if (cfg) {
        cfg->shadow_mode = 1;
        log_msg("shadow mode enabled — all actions will be logged only");
    }
}

bool shadow_is_active(const daemon_config *cfg) {
    return cfg && cfg->shadow_mode;
}

/* ── Action logging ──────────────────────────────────────────────── */

void shadow_log_action(const char *action, const char *fmt, ...) {
    char detail[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    log_msg("WOULD DO: %s %s", action, detail);
}

/* ── Save wrapper ────────────────────────────────────────────────── */

int shadow_wrap_save(const daemon_config *cfg, const char *path,
                     uint32_t position_ms, const resume_record *template_rec) {
    if (!shadow_is_active(cfg)) {
        return save_position(cfg, path, position_ms, template_rec);
    }

    /* Log what would be saved */
    if (template_rec) {
        shadow_log_action("SAVE",
            "path=%s position_ms=%u book_key=%s track_index=%d completed=%d",
            path ? path : "(null)",
            position_ms,
            template_rec->book_key[0] ? template_rec->book_key : "(none)",
            template_rec->track_index,
            (int)template_rec->completed);
    } else {
        shadow_log_action("SAVE", "path=%s position_ms=%u (no template)",
            path ? path : "(null)", position_ms);
    }
    return 0;
}

/* ── Restore wrapper ────────────────────────────────────────────── */

int shadow_wrap_restore(const daemon_config *cfg, const char *path,
                        uint32_t current_position_ms,
                        const char *book_key, const char *root) {
    if (!shadow_is_active(cfg)) {
        return maybe_restore(cfg, path, current_position_ms, book_key, root);
    }

    /* Log what the restore target would be */
    uint32_t target = restore_target_ms(cfg, current_position_ms);
    shadow_log_action("RESTORE",
        "path=%s current_pos=%u target_pos=%u book_key=%s root=%s",
        path ? path : "(null)",
        current_position_ms,
        target,
        book_key ? book_key : "(null)",
        root ? root : "(null)");
    return 0;
}

/* ── UI action wrapper ───────────────────────────────────────────── */

int shadow_wrap_ui(const daemon_config *cfg, const char *label,
                   ui_action_fn real_action) {
    if (!shadow_is_active(cfg)) {
        if (real_action) return real_action(cfg);
        return -1;
    }

    shadow_log_action("UI", "%s", label ? label : "(unnamed)");
    return 0;
}

int shadow_wrap_ui_detail(const daemon_config *cfg, const char *label,
                          const char *detail, ui_action_detail_fn real_action) {
    if (!shadow_is_active(cfg)) {
        if (real_action) return real_action(cfg, detail);
        return -1;
    }

    shadow_log_action("UI", "%s %s", label ? label : "(unnamed)",
                      detail ? detail : "");
    return 0;
}

/* ── Play mode wrapper ───────────────────────────────────────────── */

int shadow_wrap_play_mode(const daemon_config *cfg) {
    if (!shadow_is_active(cfg)) {
        return ensure_audiobook_play_mode(cfg);
    }

    shadow_log_action("PLAY_MODE",
        "target=%d touch=(%u,%u) max_taps=%d",
        (int)cfg->play_mode_target,
        (unsigned)cfg->play_mode_touch_x,
        (unsigned)cfg->play_mode_touch_y,
        (int)cfg->play_mode_max_taps);
    return 0;
}