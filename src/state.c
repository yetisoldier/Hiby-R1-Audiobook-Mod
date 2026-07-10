/*
 * state.c — state machine, daemon runtime state, diagnostics
 *
 * Spec section 2.4, 3, 16.  Wires all modules together.
 */

#include "state.h"
#include "config.h"
#include "log.h"
#include "player.h"
#include "catalog.h"
#include "resume.h"
#include "ui.h"
#include "proc_mem.h"
#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>

/* ── Init ──────────────────────────────────────────────────────────── */

void state_init(daemon_runtime *rt) {
    if (!rt) return;
    memset(rt, 0, sizeof(*rt));
    rt->state = STATE_IDLE;
    rt->last_saved_bucket = -1;
}

/* ── Book root helper ─────────────────────────────────────────────── */

void state_book_root(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len == 0) {
        if (out && out_len > 0) out[0] = '\0';
        return;
    }
    const char *last = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '\\') last = p;
    if (last) {
        size_t n = (size_t)(last - path);
        if (n >= out_len) n = out_len - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    } else {
        strncpy(out, path, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

bool state_same_book_root(const char *p1, const char *root2) {
    if (!p1 || !root2) return false;
    size_t rl = strlen(root2);
    return rl > 0 && strncmp(p1, root2, rl) == 0 && p1[rl] == '\\';
}

/* ── Settle ticks ────────────────────────────────────────────────── */

int state_settle_ticks(const daemon_config *cfg) {
    if (!cfg || cfg->track_switch_settle_seconds == 0 ||
        cfg->track_switch_poll_us == 0) return 15;
    int t = (int)(cfg->track_switch_settle_seconds * 1000000 / cfg->track_switch_poll_us);
    return t > 0 ? t : 1;
}

/* ── Autostart active / context ──────────────────────────────────── */

bool state_autostart_active(const daemon_runtime *rt) {
    return rt && rt->book_title_autostart_until > time(NULL);
}

bool state_context_active(const daemon_runtime *rt, time_t now) {
    return rt && rt->book_title_context_until > now;
}

/* ── Marker poll decision ────────────────────────────────────────── */

bool state_should_poll_marker(const daemon_runtime *rt,
                              const daemon_config *cfg,
                              const char *pp, time_t now) {
    if (!cfg->book_title_autostart_enabled || !pp) return false;
    if (path_preview_is_audiobook(pp)) return true;
    if (path_preview_is_music(pp)) {
        if (rt->last_book_title_marker_poll_at == 0) return true;
        return (now - rt->last_book_title_marker_poll_at) >=
               (time_t)cfg->book_title_marker_music_poll_seconds;
    }
    if (state_context_active(rt, now)) return true;
    if (rt->last_book_title_marker_poll_at == 0) return true;
    return (now - rt->last_book_title_marker_poll_at) >=
           (time_t)cfg->book_title_marker_idle_poll_seconds;
}

/* ── Clear autostart ──────────────────────────────────────────────── */

void state_clear_autostart(daemon_runtime *rt) {
    if (!rt) return;
    rt->book_title_autostart_until = 0;
    rt->book_title_autostart_seq = 0;
    rt->book_title_autostart_reset_key[0] = '\0';
    rt->book_title_restore_wait_log_key[0] = '\0';
    rt->book_title_pre_restore_log_key[0] = '\0';
}

/* ── Diagnostics ──────────────────────────────────────────────────── */

void state_diag_inc(daemon_runtime *rt, int *c) {
    (void)rt; if (c) (*c)++;
}

void state_diag_log(daemon_runtime *rt, const daemon_config *cfg, time_t now) {
    if (!rt || !cfg || cfg->diagnostics_interval_seconds == 0) return;
    if (rt->diag_last_log_at == 0) { rt->diag_last_log_at = now; return; }
    if ((now - rt->diag_last_log_at) < (time_t)cfg->diagnostics_interval_seconds) return;
    log_msg("stats loops=%d ab=%d nab=%d pp=%d mp=%d ms=%d pr=%d sv=%d",
            rt->diag_loops, rt->diag_audiobook_loops,
            rt->diag_non_audiobook_loops, rt->diag_path_previews,
            rt->diag_marker_polls, rt->diag_marker_skips,
            rt->diag_position_reads, rt->diag_saves);
    rt->diag_last_log_at = now;
    rt->diag_loops = rt->diag_audiobook_loops = rt->diag_non_audiobook_loops = 0;
    rt->diag_path_previews = rt->diag_marker_polls = rt->diag_marker_skips = 0;
    rt->diag_position_reads = rt->diag_saves = 0;
}

/* ── Logging helpers ──────────────────────────────────────────────── */

int state_log_bucket(const daemon_config *cfg, uint32_t pos) {
    if (!cfg || cfg->book_title_restore_log_bucket_ms == 0 || pos == 0) return 0;
    return (int)(pos / cfg->book_title_restore_log_bucket_ms);
}

void state_log_restore_wait(daemon_runtime *rt, const daemon_config *cfg,
                           const char *path, uint32_t pos) {
    int b = state_log_bucket(cfg, pos);
    char key[128];
    snprintf(key, sizeof(key), "w:%u:%s:%d", rt->book_title_autostart_seq, path, b);
    if (strcmp(rt->book_title_restore_wait_log_key, key) != 0) {
        log_msg("book-title restore wait seq=%u path=%s pos=%u",
                rt->book_title_autostart_seq, path, pos);
        strncpy(rt->book_title_restore_wait_log_key, key,
                sizeof(rt->book_title_restore_wait_log_key) - 1);
    }
}

void state_log_pre_restore_skip(daemon_runtime *rt, const daemon_config *cfg,
                                 const char *path, uint32_t pos) {
    int b = state_log_bucket(cfg, pos);
    char key[128];
    snprintf(key, sizeof(key), "s:%u:%s:%d", rt->book_title_autostart_seq, path, b);
    if (strcmp(rt->book_title_pre_restore_log_key, key) != 0) {
        log_msg("book-title skip pre-restore save seq=%u path=%s pos=%u",
                rt->book_title_autostart_seq, path, pos);
        strncpy(rt->book_title_pre_restore_log_key, key,
                sizeof(rt->book_title_pre_restore_log_key) - 1);
    }
}

/* ── Direct track geometry ────────────────────────────────────────── */

int state_direct_geometry(const daemon_config *cfg, int si,
                          int *out_sw, int *out_row) {
    if (si <= 0) return -1;
    int vr = cfg->book_title_direct_track_visible_rows;
    if (vr <= 0) vr = 5; if (vr > 5) vr = 5;
    int rps = cfg->book_title_direct_track_rows_per_swipe;
    if (rps <= 0) rps = 4;
    int ms = cfg->book_title_direct_track_max_swipes;
    if (ms == 0) ms = 20;

    int sw = 0;
    if (si > vr) sw = (si - vr + rps - 1) / rps;
    if (sw > ms) return -1;

    int row = si - (sw * rps);
    while (row < 1 && sw > 0) { sw--; row = si - (sw * rps); }
    while (row > vr) { sw++; row = si - (sw * rps); }
    if (row < 1 || row > 5) return -1;
    *out_sw = sw; *out_row = row;
    return 0;
}

/* ── Tap track list index ─────────────────────────────────────────── */

int state_tap_track_index(daemon_runtime *rt, const daemon_config *cfg,
                          int si, const char *lbl) {
    int sw, row;
    if (state_direct_geometry(cfg, si, &sw, &row) != 0) {
        log_msg("%s too many swipes si=%d", lbl, si);
        return -1;
    }
    for (int i = 0; i < sw; i++) {
        if (touch_track_swipe_up(cfg) != 0) return -1;
        sleep(cfg->book_title_direct_track_swipe_settle_seconds > 0 ?
              cfg->book_title_direct_track_swipe_settle_seconds : 1);
        log_msg("%s swipe=%d/%d", lbl, i + 1, sw);
    }
    if (touch_track_row(row, cfg) != 0) return -1;
    log_msg("%s tapped idx=%d sw=%d row=%d", lbl, si, sw, row);
    (void)rt;
    return 0;
}

/* ── Poll path helper ─────────────────────────────────────────────── */

static void poll_path(char *buf, size_t len, const daemon_config *cfg) {
    if (current_path_from_hex(cfg, buf, len) != 0) buf[0] = '\0';
}

/* ── Verify selected track ────────────────────────────────────────── */

int state_verify_selected_track(daemon_runtime *rt, const daemon_config *cfg,
                                catalog_db *cat,
                                const char *saved_path, int saved_idx,
                                int sel_row, const char *lbl,
                                const char *path_before) {
    char sr[512]; state_book_root(saved_path, sr, sizeof(sr));
    int ticks = state_settle_ticks(cfg);
    int min_ticks = (path_before && strcmp(path_before, saved_path) == 0) ? ticks : 0;
    int seen = 0;

    while (ticks > 0) {
        usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
        seen++;
        char pn[512]; poll_path(pn, sizeof(pn), cfg);
        if (strcmp(pn, saved_path) == 0 && seen >= min_ticks) {
            log_msg("%s reached path=%s saved=%d row=%d", lbl, pn, saved_idx, sel_row);
            return 0;
        }
        if (pn[0] && path_before && strcmp(pn, path_before) != 0) {
            char sroot[512]; state_book_root(pn, sroot, sizeof(sroot));
            if (strcmp(sroot, sr) == 0) {
                const catalog_entry *e = catalog_field_for_path(cat, pn);
                int si = e ? e->index : -1;
                log_msg("%s sel path=%s idx=%d saved=%d row=%d", lbl, pn, si, saved_idx, sel_row);
                if (si < 0 || saved_idx < 0) return 2;

                int delta = saved_idx - si;
                int rr = sel_row + delta;
                if (rr >= 1 && rr <= 5) {
                    log_msg("%s retry si=%d saved=%d row=%d", lbl, si, saved_idx, rr);
                    if (touch_back_to_track_list(cfg) != 0) return 2;
                    sleep(cfg->book_title_direct_track_return_delay_seconds > 0 ?
                          cfg->book_title_direct_track_return_delay_seconds : 1);
                    char pbr[512]; poll_path(pbr, sizeof(pbr), cfg);
                    if (touch_track_row(rr, cfg) != 0) return 2;
                    int rt2 = state_settle_ticks(cfg);
                    while (rt2 > 0) {
                        usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
                        char pr[512]; poll_path(pr, sizeof(pr), cfg);
                        if (strcmp(pr, saved_path) == 0) {
                            log_msg("%s retry reached saved=%d row=%d", lbl, saved_idx, rr);
                            return 0;
                        }
                        if (pr[0] && strcmp(pr, pbr) != 0) {
                            char rroot[512]; state_book_root(pr, rroot, sizeof(rroot));
                            if (strcmp(rroot, sr) == 0) {
                                const catalog_entry *re = catalog_field_for_path(cat, pr);
                                log_msg("%s retry sel path=%s idx=%d saved=%d",
                                        lbl, pr, re ? re->index : -1, saved_idx);
                                break;
                            }
                        }
                        rt2--;
                    }
                }
                if (cfg->book_title_direct_track_recovery_transport_enabled)
                    if (state_near_miss_transport(rt, cfg, cat, saved_path,
                                                  saved_idx, "bt-direct-miss") == 0)
                        return 0;
                return 2;
            }
        }
        ticks--;
    }
    char pn[512]; poll_path(pn, sizeof(pn), cfg);
    log_msg("%s not reached final=%s saved=%s", lbl, pn, saved_path);
    return strcmp(pn, saved_path) == 0 ? 0 : -1;
}

/* ── Direct open trigger ───────────────────────────────────────────── */

int state_direct_open_trigger(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat, pid_t pid, int saved_idx,
                              const char *saved_path, const char *path_before,
                              const char *lbl) {
    (void)rt;
    if (!cfg->book_title_direct_open_enabled || pid <= 0 || saved_idx <= 0)
        return -1;
    if (cfg->direct_open_helper_path[0] == '\0') return -1;

    int zi = saved_idx - 1;
    log_msg("%s start saved=%d zi=%d helper=%s", lbl, saved_idx, zi,
            cfg->direct_open_helper_path);

    int rc = helpers_direct_open(pid, (uint32_t)zi, cfg->direct_open_probe_addr,
                                 cfg->direct_open_scratch_addr,
                                 cfg->direct_open_timeout_ms,
                                 cfg->direct_open_arm_delay_us,
                                 cfg->direct_open_helper_path,
                                 (int)cfg->helper_timeout_seconds);
    if (rc != 0) { log_msg("%s helper fail status=%d", lbl, rc); return -1; }
    if (touch_track_row(1, cfg) != 0) { log_msg("%s tap trigger fail", lbl); return -1; }

    return state_verify_selected_track(rt, cfg, cat, saved_path, saved_idx, 1, lbl, path_before);
}

/* ── Direct track select ──────────────────────────────────────────── */

int state_direct_track_select(daemon_runtime *rt, const daemon_config *cfg,
                               catalog_db *cat, int ci, int si,
                               const char *saved_path) {
    if (!cfg->book_title_direct_track_select_enabled) return -1;
    if (!state_autostart_active(rt)) return -1;
    if (ci != 1 || si <= 1) return -1;

    /* Try direct-open first */
    if (cfg->book_title_direct_open_enabled) {
        pid_t pid = player_pid_cached();
        if (pid > 0) {
            if (touch_back_to_track_list(cfg) != 0) return -1;
            sleep(cfg->book_title_direct_track_return_delay_seconds > 0 ?
                  cfg->book_title_direct_track_return_delay_seconds : 1);
            char pb[512]; poll_path(pb, sizeof(pb), cfg);
            if (state_direct_open_trigger(rt, cfg, cat, pid, si,
                                           saved_path, pb, "direct-open") == 0)
                return 0;
        }
    }

    int sw, row;
    if (state_direct_geometry(cfg, si, &sw, &row) != 0) {
        log_msg("dts too many swipes si=%d", si);
        return -1;
    }
    log_msg("dts start si=%d sw=%d row=%d", si, sw, row);
    if (touch_back_to_track_list(cfg) != 0) return -1;
    sleep(cfg->book_title_direct_track_return_delay_seconds > 0 ?
          cfg->book_title_direct_track_return_delay_seconds : 1);
    char pb[512]; poll_path(pb, sizeof(pb), cfg);
    if (state_tap_track_index(rt, cfg, si, "dts") != 0) return -1;

    int ticks = state_settle_ticks(cfg);
    while (ticks > 0) {
        usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
        char pn[512]; poll_path(pn, sizeof(pn), cfg);
        if (strcmp(pn, saved_path) == 0) { log_msg("dts reached si=%d", si); return 0; }
        if (pn[0] && strcmp(pn, pb) != 0) {
            char sroot[512], saved_root[512];
            state_book_root(pn, sroot, sizeof(sroot));
            state_book_root(saved_path, saved_root, sizeof(saved_root));
            if (strcmp(sroot, saved_root) == 0) {
                const catalog_entry *e = catalog_field_for_path(cat, pn);
                int seli = e ? e->index : -1;
                log_msg("dts nearby path=%s idx=%d saved=%d", pn, seli, si);
                if (seli < 0) return 2;
                int delta = si - seli;
                int rr = row + delta;
                if (rr >= 1 && rr <= 5) {
                    log_msg("dts retry sel=%d saved=%d row=%d", seli, si, rr);
                    if (touch_back_to_track_list(cfg) != 0) return 2;
                    sleep(cfg->book_title_direct_track_return_delay_seconds > 0 ?
                          cfg->book_title_direct_track_return_delay_seconds : 1);
                    char pbr[512]; poll_path(pbr, sizeof(pbr), cfg);
                    if (touch_track_row(rr, cfg) != 0) return 2;
                    int rt2 = state_settle_ticks(cfg);
                    while (rt2 > 0) {
                        usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
                        char pr[512]; poll_path(pr, sizeof(pr), cfg);
                        if (strcmp(pr, saved_path) == 0) {
                            log_msg("dts retry reached si=%d row=%d", si, rr);
                            return 0;
                        }
                        if (pr[0] && strcmp(pr, pbr) != 0) {
                            char rroot[512]; state_book_root(pr, rroot, sizeof(rroot));
                            if (strcmp(rroot, saved_root) == 0) return 2;
                        }
                        rt2--;
                    }
                }
                return 2;
            }
        }
        ticks--;
    }
    char pn[512]; poll_path(pn, sizeof(pn), cfg);
    log_msg("dts not reached final=%s", pn);
    return strcmp(pn, saved_path) == 0 ? 0 : -1;
}

/* ── Visible track select ─────────────────────────────────────────── */

int state_visible_track_select(daemon_runtime *rt, const daemon_config *cfg,
                               catalog_db *cat, int ci, int si,
                               const char *saved_path) {
    (void)cat;
    if (!cfg->book_title_direct_track_select_enabled) return -1;
    if (!state_autostart_active(rt)) return -1;
    if (ci == si) return 0;

    int vr = cfg->book_title_direct_track_visible_rows;
    if (vr <= 0) vr = 5; if (vr > 5) vr = 5;
    int delta = si - ci, row;
    if (delta >= 0 && delta < vr) row = delta + 1;
    else if (delta < 0 && (-delta) < vr) row = vr + delta;
    else return -1;
    if (row < 1 || row > 5) return -1;

    log_msg("vts start ci=%d si=%d row=%d", ci, si, row);
    if (touch_back_to_track_list(cfg) != 0) return 2;
    sleep(cfg->book_title_direct_track_return_delay_seconds > 0 ?
          cfg->book_title_direct_track_return_delay_seconds : 1);
    char pb[512]; poll_path(pb, sizeof(pb), cfg);
    if (touch_track_row(row, cfg) != 0) return 2;

    int ticks = state_settle_ticks(cfg);
    while (ticks > 0) {
        usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
        char pn[512]; poll_path(pn, sizeof(pn), cfg);
        if (strcmp(pn, saved_path) == 0) { log_msg("vts reached si=%d", si); return 0; }
        if (pn[0] && strcmp(pn, pb) != 0) {
            char sroot[512], saved_root[512];
            state_book_root(pn, sroot, sizeof(sroot));
            state_book_root(saved_path, saved_root, sizeof(saved_root));
            if (strcmp(sroot, saved_root) == 0) {
                const catalog_entry *e = catalog_field_for_path(cat, pn);
                log_msg("vts nearby path=%s idx=%d saved=%d",
                        pn, e ? e->index : -1, si);
                return 2;
            }
        }
        ticks--;
    }
    log_msg("vts not reached saved=%s", saved_path);
    return 2;
}

/* ── Near-miss transport ──────────────────────────────────────────── */

int state_near_miss_transport(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat, const char *saved_path,
                              int saved_idx, const char *reason) {
    (void)rt;
    if (!cfg->track_restore_near_miss_transport_enabled) return -1;

    int mode = play_mode_value(cfg);
    if (mode != cfg->play_mode_target) {
        log_msg("nmt skipped mode=%d target=%d reason=%s", mode, cfg->play_mode_target, reason);
        return -1;
    }

    char path[512]; poll_path(path, sizeof(path), cfg);
    if (strcmp(path, saved_path) == 0) return 0;

    const catalog_entry *e = catalog_field_for_path(cat, path);
    int ci = e ? e->index : -1;
    if (ci < 0 || saved_idx < 0) return -1;

    int max_steps = cfg->track_restore_near_miss_max_steps;
    if (strcmp(reason, "bt-direct-miss") == 0) {
        if (!cfg->book_title_direct_track_recovery_transport_enabled) return -1;
        max_steps = cfg->book_title_direct_track_recovery_max_steps;
    }
    if (max_steps <= 0) return -1;

    int dir, steps;
    if (saved_idx > ci) { dir = 1; steps = saved_idx - ci; }
    else if (saved_idx < ci) { dir = -1; steps = ci - saved_idx; }
    else return 0;

    if (steps > max_steps) {
        log_msg("nmt too many ci=%d si=%d max=%d", ci, saved_idx, max_steps);
        return -1;
    }

    log_msg("nmt start reason=%s ci=%d si=%d dir=%s steps=%d", reason, ci,
            saved_idx, dir == 1 ? "next" : "prev", steps);

    for (int c = 0; c < steps; c++) {
        char pb[512]; poll_path(pb, sizeof(pb), cfg);
        int ei = (dir == 1) ? ci + c + 1 : ci - c - 1;
        if (dir == 1) { if (track_next(cfg) != 0) return -1; }
        else { if (track_prev(cfg) != 0) return -1; }

        int ticks = state_settle_ticks(cfg);
        while (ticks > 0) {
            usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
            char pn[512]; poll_path(pn, sizeof(pn), cfg);
            if (strcmp(pn, saved_path) == 0) break;
            const catalog_entry *en = catalog_field_for_path(cat, pn);
            int in = en ? en->index : -1;
            if (in == ei) break;
            if (pn[0] && strcmp(pn, pb) != 0 && in > 0) break;
            ticks--;
        }
        char pn[512]; poll_path(pn, sizeof(pn), cfg);
        const catalog_entry *en = catalog_field_for_path(cat, pn);
        log_msg("nmt %s step=%d/%d idx=%d", dir == 1 ? "next" : "prev",
                c + 1, steps, en ? en->index : -1);
        if (strcmp(pn, saved_path) == 0) return 0;
    }

    char pn[512]; poll_path(pn, sizeof(pn), cfg);
    if (strcmp(pn, saved_path) == 0) return 0;
    log_msg("nmt failed final=%s", pn);
    return -1;
}

/* ── Direct start saved track ─────────────────────────────────────── */

int state_direct_start_saved(daemon_runtime *rt, const daemon_config *cfg,
                             catalog_db *cat, pid_t pid,
                             uint32_t tl_ptr, uint32_t cs_ptr,
                             int allow_memscan) {
    if (!cfg->book_title_direct_track_select_enabled) return -1;
    if (!cfg->book_title_direct_track_preplay_enabled) return -1;
    if (!cfg->restore_enabled || pid <= 0) return -1;

    char track_path[512] = "";
    if (allow_memscan) {
        char root[512];
        if (book_title_memscan_root(cfg, pid, (const struct catalog_db *)cat,
                                    root, sizeof(root)) == 0 && root[0]) {
            if (catalog_first_path_for_root(cat, root, track_path, sizeof(track_path)) == 0)
                log_msg("bt ds memscan root=%s", root);
        }
    }
    if (track_path[0] == '\0' && tl_ptr > 4096 && cfg->book_title_track_list_scan_bytes > 0) {
        int fd = proc_mem_open(pid);
        if (fd >= 0) {
            proc_mem_first_catalog_path(fd, tl_ptr, cfg->book_title_track_list_scan_bytes,
                                        (const struct catalog_db *)cat,
                                        track_path, sizeof(track_path));
            proc_mem_close(fd);
        }
    }
    if (track_path[0] == '\0' && cs_ptr > 4096 && cfg->book_title_catalog_scan_bytes > 0) {
        int fd = proc_mem_open(pid);
        if (fd >= 0) {
            proc_mem_first_catalog_path(fd, cs_ptr, cfg->book_title_catalog_scan_bytes,
                                        (const struct catalog_db *)cat,
                                        track_path, sizeof(track_path));
            proc_mem_close(fd);
        }
    }
    if (track_path[0] == '\0') {
        log_msg("bt ds unavailable no catalog path");
        return -1;
    }

    char root[512]; state_book_root(track_path, root, sizeof(root));
    char bk[128] = "";
    const char *bkp = book_key_for_path(cat, track_path);
    if (bkp) strncpy(bk, bkp, sizeof(bk) - 1);

    resume_record rec;
    if (existing_record_for_path(cfg, track_path, bk, root, &rec) != 0) {
        log_msg("bt ds no record root=%s", root);
        return -1;
    }
    if (rec.completed) { log_msg("bt ds completed root=%s", root); return -1; }

    char saved_path[512];
    strncpy(saved_path, rec.current_path, sizeof(saved_path) - 1);
    saved_path[sizeof(saved_path) - 1] = '\0';
    uint32_t saved_pos = rec.position_ms;
    if (saved_pos < cfg->restore_min_ms) return -1;

    const catalog_entry *se = catalog_field_for_path(cat, saved_path);
    int si = se ? se->index : -1;
    int tc = se ? se->count : -1;
    if (si < 0) return -1;

    char pbd[512]; poll_path(pbd, sizeof(pbd), cfg);

    if (cfg->book_title_direct_track_calibrate_enabled &&
        strcmp(pbd, saved_path) == 0) {
        log_msg("bt ds probe si=%d/%d pos=%u", si, tc, saved_pos);
        if (touch_track_row(1, cfg) != 0) return -1;
        return state_verify_selected_track(rt, cfg, cat, saved_path, si, 1,
                                           "bt ds probe", pbd);
    }

    if (si > 1 && cfg->book_title_direct_open_enabled) {
        int rc = state_direct_open_trigger(rt, cfg, cat, pid, si,
                                           saved_path, pbd, "bt ds open");
        if (rc == 0) return 0;
        log_msg("bt ds open fallback status=%d", rc);
        if (touch_back_to_track_list(cfg) != 0) return -1;
        sleep(cfg->book_title_direct_track_return_delay_seconds > 0 ?
              cfg->book_title_direct_track_return_delay_seconds : 1);
        poll_path(pbd, sizeof(pbd), cfg);
    }

    int sw, row;
    if (state_direct_geometry(cfg, si, &sw, &row) != 0) {
        log_msg("bt ds too many swipes si=%d", si);
        return -1;
    }
    log_msg("bt ds root=%s si=%d/%d pos=%u sw=%d row=%d", root, si, tc, saved_pos, sw, row);
    if (state_tap_track_index(rt, cfg, si, "bt ds") != 0) return -1;
    return state_verify_selected_track(rt, cfg, cat, saved_path, si, row,
                                       "bt ds", pbd);
}

/* ── Maybe autostart ──────────────────────────────────────────────── */

int state_maybe_autostart(daemon_runtime *rt, const daemon_config *cfg,
                         catalog_db *cat, uint32_t seq) {
    if (!cfg->book_title_autostart_enabled || seq == 0) return 0;
    time_t now = time(NULL);
    bool ctx = state_context_active(rt, now);
    pid_t pid = player_pid();
    if (pid <= 0) return 0;

    int fd = proc_mem_open(pid);
    if (fd < 0) return 0;

    uint32_t album_ptr = 0;
    if (proc_mem_read_u32le(fd, cfg->book_title_marker_addr + 24, &album_ptr) != 0 ||
        album_ptr <= 4096) { proc_mem_close(fd); return 0; }

    uint32_t tl_ptr = 0;
    if (proc_mem_read_u32le(fd, album_ptr + cfg->book_title_track_list_offset,
                            &tl_ptr) != 0 || tl_ptr <= 4096) { proc_mem_close(fd); return 0; }

    const char *reason = NULL;
    uint32_t cs_ptr = 0;

    uint32_t sm = 0, ss = 0;
    proc_mem_read_u32le(fd, cfg->book_title_marker_addr + 32, &sm);
    proc_mem_read_u32le(fd, cfg->book_title_marker_addr + 44, &ss);
    if (sm == cfg->book_title_source_magic && ss == seq) reason = "launcher";

    if (!reason && proc_mem_contains(fd, tl_ptr, cfg->book_title_track_list_scan_bytes,
                                     "a:\\Audiobooks", 13)) reason = "path";

    if (!reason) {
        uint32_t csa = album_ptr + cfg->book_title_catalog_scan_ptr_offset;
        proc_mem_read_u32le(fd, csa, &cs_ptr);
        if (cs_ptr > 4096 && cfg->book_title_catalog_scan_bytes > 0 &&
            proc_mem_contains_catalog_album(fd, cs_ptr,
                                            cfg->book_title_catalog_scan_bytes,
                                            (const struct catalog_db *)cat))
            reason = "catalog";
    }

    if (!reason && ctx) reason = "context";
    if (!reason) {
        if (cfg->book_title_autostart_require_path) {
            log_msg("bt autostart ignored seq=%u ap=%u tl=%u cs=%u sm=%u ss=%u ctx=%ld",
                    seq, album_ptr, tl_ptr, cs_ptr, sm, ss, (long)rt->book_title_context_until);
            proc_mem_close(fd);
            return 0;
        }
        reason = "relaxed";
        log_msg("bt autostart relaxed seq=%u ap=%u tl=%u", seq, album_ptr, tl_ptr);
    }
    proc_mem_close(fd);

    if (strcmp(reason, "launcher") == 0 || strcmp(reason, "path") == 0 ||
        strcmp(reason, "catalog") == 0) {
        if (cfg->book_title_context_seconds > 0)
            rt->book_title_context_until = now + (time_t)cfg->book_title_context_seconds;
        if (cfg->back_guard_enabled)
            rt->audiobook_back_guard_until = now + (time_t)cfg->back_guard_window_seconds;
    }

    log_msg("bt autostart seq=%u reason=%s ap=%u tl=%u cs=%u sm=%u ss=%u ctx=%d ctx_until=%ld",
            seq, reason, album_ptr, tl_ptr, cs_ptr, sm, ss, ctx ? 1 : 0,
            (long)rt->book_title_context_until);

    rt->book_title_autostart_until = now + (time_t)cfg->restore_retry_after_failure_seconds;
    rt->book_title_autostart_seq = seq;
    rt->book_title_autostart_reset_key[0] = '\0';
    rt->book_title_restore_wait_log_key[0] = '\0';
    rt->book_title_pre_restore_log_key[0] = '\0';
    rt->restored_path[0] = '\0';
    rt->completed_saved_path[0] = '\0';
    resume_reset_failures();
    rt->last_saved_bucket = -1;
    rt->deferred_overwrite_path[0] = '\0';

    if (cfg->book_title_autostart_delay_seconds > 0)
        sleep(cfg->book_title_autostart_delay_seconds);

    bool preplay = (strcmp(reason, "launcher") == 0 ||
                    strcmp(reason, "context") == 0 ||
                    strcmp(reason, "relaxed") == 0);
    if (preplay) {
        int allow_ms = (strcmp(reason, "path") == 0 || strcmp(reason, "catalog") == 0) ? 0 : 1;
        if (state_direct_start_saved(rt, cfg, cat, pid, tl_ptr, cs_ptr, allow_ms) == 0)
            return 0;
    } else {
        log_msg("bt ds skipped reason=%s tl=%u cs=%u", reason, tl_ptr, cs_ptr);
    }

    if (strcmp(reason, "launcher") == 0) {
        int fb = open("/dev/fb0", O_RDONLY);
        if (fb >= 0) {
            bool tl = audiobook_title_list_visible(fb, cfg);
            bool tr = audiobook_track_list_visible(fb, cfg);
            close(fb);
            const char *scr = tl ? "title-list" : (tr ? "track-list" : "unknown");
            log_msg("bt touch-first skipped reason=launcher screen=%s", scr);
            if (tr) {
                log_msg("bt launcher back-to-title-list");
                touch_back_to_track_list(cfg);
            }
            return 0;
        }
        return 0;
    }

    return touch_first_track(cfg) == 0 ? 0 : -1;
}

/* ── Maybe restore track ──────────────────────────────────────────── */

int state_maybe_restore_track(daemon_runtime *rt, const daemon_config *cfg,
                              catalog_db *cat, const char *path, uint32_t pos) {
    if (!cfg->restore_enabled || !cfg->track_restore_enabled) return -1;

    char root[512]; state_book_root(path, root, sizeof(root));
    char bk[128] = "";
    const char *bkp = book_key_for_path(cat, path);
    if (bkp) strncpy(bk, bkp, sizeof(bk) - 1);

    resume_record rec;
    if (existing_record_for_path(cfg, path, bk, root, &rec) != 0) return -1;
    if (rec.completed) return -1;

    char sp[512]; strncpy(sp, rec.current_path, sizeof(sp) - 1);
    sp[sizeof(sp) - 1] = '\0';
    if (strcmp(sp, path) == 0) return -1;
    if (!state_same_book_root(sp, root)) return -1;
    if (rec.position_ms < cfg->restore_min_ms) return -1;
    if (pos > cfg->restore_only_before_ms) return -1;

    const catalog_entry *ce = catalog_field_for_path(cat, path);
    const catalog_entry *se = catalog_field_for_path(cat, sp);
    int ci = ce ? ce->index : -1;
    int si = se ? se->index : -1;
    if (ci < 0 || si < 0) {
        log_msg("tr unavailable ci=%d si=%d", ci, si);
        return -1;
    }

    bool auto_restore = state_autostart_active(rt);
    if (!auto_restore) {
        bool fok = false;
        if (cfg->track_restore_first_track_entry_enabled &&
            ci == 1 && si > 1 && pos <= cfg->track_restore_first_track_entry_max_ms)
            fok = true;
        if (!fok) { log_msg("skip tr manual path=%s", path); return -1; }
        log_msg("tr first-entry ci=%d si=%d pos=%u", ci, si, pos);
    }

    int dir, steps;
    if (si > ci) { dir = 1; steps = si - ci; }
    else if (si < ci) { dir = -1; steps = ci - si; }
    else return -1;

    if (steps > cfg->track_restore_max_steps) {
        log_msg("tr too many ci=%d si=%d max=%d", ci, si, cfg->track_restore_max_steps);
        return -1;
    }

    int ds = state_direct_track_select(rt, cfg, cat, ci, si, sp);
    if (ds == 0) return 0;
    if (ds == 2) {
        char pa[512]; poll_path(pa, sizeof(pa), cfg);
        if (strcmp(pa, sp) == 0) return 0;
        if (state_same_book_root(pa, root)) {
            const catalog_entry *ae = catalog_field_for_path(cat, pa);
            int ai = ae ? ae->index : -1;
            if (ai > 0) {
                log_msg("tr near-miss visible ci=%d si=%d", ai, si);
                int vs = state_visible_track_select(rt, cfg, cat, ai, si, sp);
                if (vs == 0) return 0;
                if (vs == 2) {
                    if (state_near_miss_transport(rt, cfg, cat, sp, si, "direct-near-miss") == 0)
                        return 0;
                    return -1;
                }
            }
        }
        return -1;
    }

    int vs = state_visible_track_select(rt, cfg, cat, ci, si, sp);
    if (vs == 0) return 0;
    if (vs == 2) {
        if (state_near_miss_transport(rt, cfg, cat, sp, si, "visible-near-miss") == 0)
            return 0;
        return -1;
    }

    /* Key fallback */
    if (!cfg->track_restore_key_fallback_enabled) {
        log_msg("tr key fallback disabled ci=%d si=%d", ci, si);
        return -1;
    }

    char pa[512]; poll_path(pa, sizeof(pa), cfg);
    if (pa[0] && strcmp(pa, path) != 0) {
        if (!state_same_book_root(pa, root)) return -1;
        path = pa;
        const catalog_entry *ne = catalog_field_for_path(cat, path);
        ci = ne ? ne->index : -1;
        if (ci < 0) return -1;
        if (si > ci) { dir = 1; steps = si - ci; }
        else if (si < ci) { dir = -1; steps = ci - si; }
        else return 0;
        if (steps > cfg->track_restore_max_steps) return -1;
        log_msg("tr fallback ci=%d si=%d steps=%d", ci, si, steps);
    }

    log_msg("tr key start ci=%d si=%d dir=%s steps=%d", ci, si,
            dir == 1 ? "next" : "prev", steps);

    for (int c = 0; c < steps; c++) {
        char pb[512]; poll_path(pb, sizeof(pb), cfg);
        int ei = (dir == 1) ? ci + c + 1 : ci - c - 1;
        if (dir == 1) { if (track_next(cfg) != 0) return -1; }
        else { if (track_prev(cfg) != 0) return -1; }

        int ticks = state_settle_ticks(cfg);
        while (ticks > 0) {
            usleep(cfg->track_switch_poll_us > 0 ? cfg->track_switch_poll_us : 100000);
            char pn[512]; poll_path(pn, sizeof(pn), cfg);
            if (strcmp(pn, sp) == 0) break;
            const catalog_entry *en = catalog_field_for_path(cat, pn);
            int in = en ? en->index : -1;
            if (in == ei) break;
            if (pn[0] && strcmp(pn, pb) != 0 && in > 0) break;
            ticks--;
        }
        char pn[512]; poll_path(pn, sizeof(pn), cfg);
        const catalog_entry *en = catalog_field_for_path(cat, pn);
        log_msg("tr %s step=%d/%d idx=%d", dir == 1 ? "next" : "prev",
                c + 1, steps, en ? en->index : -1);
        if (strcmp(pn, sp) == 0) return 0;
    }

    char pn[512]; poll_path(pn, sizeof(pn), cfg);
    if (strcmp(pn, sp) == 0) return 0;
    log_msg("tr failed final=%s", pn);
    return -1;
}

/* ── Main poll cycle ──────────────────────────────────────────────── */

uint32_t state_poll_cycle(daemon_runtime *rt, const daemon_config *cfg,
                          catalog_db *cat) {
    time_t now_loop = time(NULL);
    state_diag_inc(rt, &rt->diag_loops);
    uint32_t loop_sleep = cfg->idle_interval_seconds > 0 ? cfg->idle_interval_seconds : 3;

    state_diag_log(rt, cfg, now_loop);

    /* Read path preview */
    char pp[129];
    if (current_path_slot_preview(cfg, pp, sizeof(pp)) != 0) pp[0] = '\0';
    state_diag_inc(rt, &rt->diag_path_previews);

    /* Back guard */
    if (cfg->back_guard_enabled) {
        back_guard_state bg = {
            rt->audiobook_back_guard_until,
            rt->audiobook_back_guard_seen_at,
            rt->audiobook_back_guard_last_fire_at
        };
        maybe_audiobook_back_guard(now_loop, pp, &bg, cfg);
        rt->audiobook_back_guard_until = bg.audiobook_back_guard_until;
        rt->audiobook_back_guard_seen_at = bg.audiobook_back_guard_seen_at;
        rt->audiobook_back_guard_last_fire_at = bg.audiobook_back_guard_last_fire_at;
    }

    /* Marker poll */
    if (state_should_poll_marker(rt, cfg, pp, now_loop)) {
        state_diag_inc(rt, &rt->diag_marker_polls);
        rt->last_book_title_marker_poll_at = now_loop;
        uint32_t seq = book_title_marker_seq(cfg);
        if (seq > 0 && seq != rt->last_book_title_seq) {
            loop_sleep = cfg->interval_seconds > 0 ? cfg->interval_seconds : 1;
            if (state_maybe_autostart(rt, cfg, cat, seq) != 0)
                log_msg("bt autostart failed seq=%u", seq);
            rt->last_book_title_seq = seq;
            if (current_path_slot_preview(cfg, pp, sizeof(pp)) != 0) pp[0] = '\0';
            state_diag_inc(rt, &rt->diag_path_previews);
        }
    } else {
        state_diag_inc(rt, &rt->diag_marker_skips);
    }

    /* Back guard idle interval */
    if (cfg->back_guard_enabled && !path_preview_is_music(pp) &&
        !path_preview_is_audiobook(pp) &&
        cfg->back_guard_idle_interval_seconds < loop_sleep)
        loop_sleep = cfg->back_guard_idle_interval_seconds;

    /* Path classification */
    char path[512] = "";
    if (path_preview_is_audiobook(pp)) {
        if (current_path_from_hex(cfg, path, sizeof(path)) != 0) path[0] = '\0';
    }

    if (path[0] && path_preview_is_audiobook(path)) {
        /* ── Audiobook tracking ──────────────────────────── */
        rt->state = STATE_AUDIOBOOK_TRACKING;
        state_diag_inc(rt, &rt->diag_audiobook_loops);
        loop_sleep = cfg->interval_seconds > 0 ? cfg->interval_seconds : 1;

        time_t now = time(NULL);
        if (cfg->book_title_context_seconds > 0)
            rt->book_title_context_until = now + (time_t)cfg->book_title_context_seconds;

        state_diag_inc(rt, &rt->diag_position_reads);
        uint32_t pos = position_ms_memory(cfg);
        bool auto_restore = state_autostart_active(rt);

        /* Autostart reset */
        if (auto_restore) {
            char rk[32]; snprintf(rk, sizeof(rk), "%u", rt->book_title_autostart_seq);
            if (strcmp(rt->book_title_autostart_reset_key, rk) != 0) {
                log_msg("bt restore reset seq=%u path=%s pos=%u",
                        rt->book_title_autostart_seq, path, pos);
                rt->restored_path[0] = '\0';
                rt->completed_saved_path[0] = '\0';
                resume_reset_failures();
                rt->last_saved_bucket = -1;
                rt->deferred_overwrite_path[0] = '\0';
                strncpy(rt->book_title_autostart_reset_key, rk,
                        sizeof(rt->book_title_autostart_reset_key) - 1);
            }
        }

        /* Path change */
        if (strcmp(path, rt->last_path) != 0) {
            char root[512]; state_book_root(path, root, sizeof(root));
            const catalog_entry *e = catalog_field_for_path(cat, path);
            log_msg("audiobook path=%s pos=%u track=%d/%d", path, pos,
                    e ? e->index : -1, e ? e->count : -1);
            ensure_audiobook_play_mode(cfg);
            rt->restored_path[0] = '\0';
            resume_reset_failures();
            rt->last_saved_bucket = -1;
            rt->deferred_overwrite_path[0] = '\0';
        }

        char root[512]; state_book_root(path, root, sizeof(root));
        char bk[128] = "";
        const char *bkp = book_key_for_path(cat, path);
        if (bkp) strncpy(bk, bkp, sizeof(bk) - 1);

        /* Completed check */
        resume_record rec;
        if (existing_record_for_path(cfg, path, bk, root, &rec) == 0 && rec.completed) {
            if (strcmp(rt->restored_path, path) != 0)
                log_msg("completed start-over root=%s path=%s pos=%u", root, path, pos);
            strncpy(rt->restored_path, path, sizeof(rt->restored_path) - 1);
            rt->restored_path[sizeof(rt->restored_path) - 1] = '\0';
            rt->completed_saved_path[0] = '\0';
            resume_reset_failures();
            state_clear_autostart(rt);
            rt->last_saved_bucket = -1;
            rt->deferred_overwrite_path[0] = '\0';
            strncpy(rt->completed_start_over_path, path,
                    sizeof(rt->completed_start_over_path) - 1);
            rt->completed_start_over_path[sizeof(rt->completed_start_over_path) - 1] = '\0';
        }

        /* Restore phase */
        if (strcmp(rt->restored_path, path) != 0) {
            if (should_attempt_restore_for_position(cfg, pos, auto_restore)) {
                if (auto_restore && pos > cfg->restore_only_before_ms) {
                    state_log_restore_wait(rt, cfg, path, pos);
                    strncpy(rt->last_path, path, sizeof(rt->last_path) - 1);
                    rt->last_path[sizeof(rt->last_path) - 1] = '\0';
                    return loop_sleep;
                }

                bool try_restore = true;
                if (rt->restore_failed_path[0] &&
                    strcmp(rt->restore_failed_path, path) == 0) {
                    uint32_t rdelay = cfg->restore_retry_after_failure_seconds;
                    if (strcmp(rt->restore_failed_kind, "seek") == 0)
                        rdelay = restore_retry_delay_seconds(cfg);
                    if ((time(NULL) - rt->restore_failed_at) < (time_t)rdelay)
                        try_restore = false;
                }

                if (try_restore) {
                    bool track_changed = false;

                    if (should_skip_after_completed_restore(path,
                                                            rt->completed_saved_path)) {
                        strncpy(rt->restored_path, path,
                                sizeof(rt->restored_path) - 1);
                        rt->restored_path[sizeof(rt->restored_path) - 1] = '\0';
                        resume_reset_failures();
                        state_clear_autostart(rt);
                    } else if (state_maybe_restore_track(rt, cfg, cat, path, pos) == 0) {
                        char pa[512]; poll_path(pa, sizeof(pa), cfg);
                        if (pa[0] && strcmp(pa, path) != 0) {
                            strncpy(path, pa, sizeof(path) - 1);
                            path[sizeof(path) - 1] = '\0';
                            track_changed = true;
                            state_diag_inc(rt, &rt->diag_position_reads);
                            pos = position_ms_memory(cfg);
                            const catalog_entry *e2 = catalog_field_for_path(cat, path);
                            log_msg("audiobook path=%s pos=%u track=%d/%d", path, pos,
                                    e2 ? e2->index : -1, e2 ? e2->count : -1);
                        }
                    } else {
                        rt->restored_path[0] = '\0';
                        note_track_restore_failure(path);
                        strncpy(rt->last_path, path, sizeof(rt->last_path) - 1);
                        rt->last_path[sizeof(rt->last_path) - 1] = '\0';
                        return loop_sleep;
                    }

                    if (track_changed) {
                        log_msg("restore settle path=%s pos=%u", path, pos);
                        strncpy(rt->last_path, path, sizeof(rt->last_path) - 1);
                        rt->last_path[sizeof(rt->last_path) - 1] = '\0';
                        return loop_sleep;
                    }

                    if (rt->restored_path[0] && strcmp(rt->restored_path, path) == 0 &&
                        rt->completed_saved_path[0] &&
                        strcmp(rt->completed_saved_path, path) == 0) {
                        /* already restored */
                    } else if (maybe_restore(cfg, path, pos, bk, root) == 0) {
                        strncpy(rt->restored_path, path, sizeof(rt->restored_path) - 1);
                        rt->restored_path[sizeof(rt->restored_path) - 1] = '\0';
                        strncpy(rt->completed_saved_path, path,
                                sizeof(rt->completed_saved_path) - 1);
                        rt->completed_saved_path[sizeof(rt->completed_saved_path) - 1] = '\0';
                        resume_reset_failures();
                        state_clear_autostart(rt);
                    } else {
                        rt->restored_path[0] = '\0';
                    }
                }

                state_diag_inc(rt, &rt->diag_position_reads);
                pos = position_ms_memory(cfg);
            }
        }

        /* Save phase */
        if (pos >= cfg->min_save_ms) {
            int bucket = (int)(pos / cfg->save_bucket_ms);

            if (auto_restore && rt->restored_path[0] &&
                strcmp(rt->restored_path, path) != 0 &&
                pos <= cfg->restore_only_before_ms) {
                state_log_pre_restore_skip(rt, cfg, path, pos);
            } else if (should_skip_failed_restore_save(cfg, path, pos)) {
                /* skip */
            } else if (rt->completed_start_over_path[0] &&
                       strcmp(rt->completed_start_over_path, path) == 0) {
                save_position(cfg, path, pos, NULL);
                state_diag_inc(rt, &rt->diag_saves);
                rt->last_saved_bucket = bucket;
                rt->completed_start_over_path[0] = '\0';
                rt->deferred_overwrite_path[0] = '\0';
            } else if (should_defer_new_track_save(cfg, path, rt->last_path,
                                                    0, now)) {
                if (strcmp(rt->deferred_overwrite_path, path) != 0) {
                    log_msg("defer save path=%s pos=%u", path, pos);
                    strncpy(rt->deferred_overwrite_path, path,
                            sizeof(rt->deferred_overwrite_path) - 1);
                    rt->deferred_overwrite_path[sizeof(rt->deferred_overwrite_path) - 1] = '\0';
                }
            } else if (bucket != rt->last_saved_bucket ||
                       strcmp(path, rt->last_path) != 0) {
                resume_record tmpl;
                memset(&tmpl, 0, sizeof(tmpl));
                tmpl.schema_version = 3;
                state_book_root(path, tmpl.root_hiby_path, sizeof(tmpl.root_hiby_path));
                safe_id(tmpl.root_hiby_path, tmpl.book_id, sizeof(tmpl.book_id));
                if (bkp) strncpy(tmpl.book_key, bkp, sizeof(tmpl.book_key) - 1);
                const catalog_entry *ce = catalog_field_for_path(cat, path);
                if (ce) {
                    tmpl.media_id = ce->media_id;
                    tmpl.track_index = ce->index;
                    tmpl.track_count = ce->count;
                    strncpy(tmpl.chapter_title, ce->title, sizeof(tmpl.chapter_title) - 1);
                }
                save_position(cfg, path, pos, &tmpl);
                state_diag_inc(rt, &rt->diag_saves);
                rt->last_saved_bucket = bucket;
                rt->deferred_overwrite_path[0] = '\0';
            }
        }

        strncpy(rt->last_path, path, sizeof(rt->last_path) - 1);
        rt->last_path[sizeof(rt->last_path) - 1] = '\0';
    } else {
        /* ── Non-audiobook ───────────────────────────────── */
        rt->state = STATE_IDLE;
        state_diag_inc(rt, &rt->diag_non_audiobook_loops);
        if (rt->last_path[0]) log_msg("leave audiobook");
        rt->restored_path[0] = '\0';
        rt->completed_saved_path[0] = '\0';
        resume_reset_failures();
        rt->helper_failures = 0;
        rt->last_saved_bucket = -1;
        rt->deferred_overwrite_path[0] = '\0';
        rt->completed_start_over_path[0] = '\0';
        rt->last_path[0] = '\0';
    }

    return loop_sleep;
}