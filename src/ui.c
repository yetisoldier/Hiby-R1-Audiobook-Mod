/*
 * ui.c — framebuffer pixel checks, touch/key event injection,
 *         seek-bar restore, play-mode enforcement, back guard
 *
 * Spec section 5.  All framebuffer and input device operations
 * fail gracefully (log warning, return error) if devices
 * can't be opened.
 *
 * Framebuffer format: RGB565, 16 bits per pixel, little-endian.
 *   v = pixel[0] | (pixel[1] << 8)
 *   r = v >> 11        (5 bits, bits 11-15)
 *   g = (v >> 5) & 0x3f (6 bits, bits 5-10)
 *   b = v & 0x1f        (5 bits, bits 0-4)
 *   white = r >= 24 && g >= 48 && b >= 24
 *   blue  = r <= 10 && g >= 24 && b >= 18
 *
 * Input event format (MIPS LE): 24 bytes per event
 *   struct input_event { struct timeval time; uint16_t type; uint16_t code; int32_t value; }
 *   timeval on mipsel LE = 8 bytes (two 32-bit LE ints)
 */

#include "ui.h"
#include "log.h"
#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

/* ── Pixel classification ─────────────────────────────────────────── */

bool pixel_is_white(uint16_t v) {
    uint16_t r = v >> 11;
    uint16_t g = (v >> 5) & 0x3f;
    uint16_t b = v & 0x1f;
    return r >= 24 && g >= 48 && b >= 24;
}

bool pixel_is_blue(uint16_t v) {
    uint16_t r = v >> 11;
    uint16_t g = (v >> 5) & 0x3f;
    uint16_t b = v & 0x1f;
    return r <= 10 && g >= 24 && b >= 18;
}

/* ── Framebuffer reads ────────────────────────────────────────────── */

/*
 * Read a chunk of framebuffer data starting at (0, y0) for
 * (y1 - y0) rows, each row being stride bytes.
 * Returns a malloc'd buffer (caller frees), or NULL on error.
 */
static unsigned char *fb_read_rows(int fb_fd, uint16_t y0, uint16_t y1,
                                   uint16_t stride) {
    if (fb_fd < 0 || y1 <= y0 || stride == 0) return NULL;

    size_t total = (size_t)(y1 - y0) * stride;
    unsigned char *buf = malloc(total);
    if (!buf) return NULL;

    /* Seek to row y0 */
    off_t offset = (off_t)y0 * stride;
    if (lseek(fb_fd, offset, SEEK_SET) == (off_t)-1) {
        free(buf);
        return NULL;
    }

    /* Read all rows */
    size_t done = 0;
    while (done < total) {
        ssize_t n = read(fb_fd, buf + done, total - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return NULL;
        }
        if (n == 0) break; /* short read — fb doesn't have that many rows */
        done += (size_t)n;
    }

    return buf;
}

int fb_white_pixels_region(int fb_fd, uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1,
                           uint16_t stride) {
    if (fb_fd < 0 || x1 <= x0 || y1 <= y0 || stride == 0)
        return -1;

    /* Each pixel is 2 bytes (RGB565), so 4 bytes per pixel in the hex
       representation but 2 actual bytes. The stride is in bytes.
       We read 2 bytes per pixel. */
    unsigned char *buf = fb_read_rows(fb_fd, y0, y1, stride);
    if (!buf) return -1;

    int count = 0;
    size_t rows = (size_t)(y1 - y0);

    for (size_t row = 0; row < rows; row++) {
        unsigned char *rowptr = buf + (row * stride);
        for (uint16_t x = x0; x < x1; x++) {
            size_t byte_off = (size_t)x * 2;
            if (byte_off + 1 >= stride) break;
            /* Little-endian 16-bit pixel value */
            uint16_t v = rowptr[byte_off] | ((uint16_t)rowptr[byte_off + 1] << 8);
            if (pixel_is_white(v)) count++;
        }
    }

    free(buf);
    return count;
}

/* ── Screen classification ────────────────────────────────────────── */

bool ui_seek_screen_ready(int fb_fd, const daemon_config *cfg) {
    if (!cfg->ui_seek_screen_guard_enabled) return true;
    if (fb_fd < 0) return false;

    if (cfg->ui_seek_bar_x_min == 0 && cfg->ui_seek_bar_x_max == 0 &&
        cfg->ui_seek_fb_stride == 0)
        return false;

    /* Read the seek bar row */
    unsigned char *buf = fb_read_rows(fb_fd, cfg->ui_seek_bar_y,
                                      cfg->ui_seek_bar_y + 1,
                                      cfg->ui_seek_fb_stride);
    if (!buf) {
        log_msg("ui-seek screen guard unavailable fb0");
        return false;
    }

    int pixels = 0;
    for (uint16_t x = cfg->ui_seek_bar_x_min; x <= cfg->ui_seek_bar_x_max; x++) {
        size_t byte_off = (size_t)x * 2;
        if (byte_off + 1 >= cfg->ui_seek_fb_stride) break;
        uint16_t v = buf[byte_off] | ((uint16_t)buf[byte_off + 1] << 8);
        if (pixel_is_white(v) || pixel_is_blue(v)) pixels++;
    }

    free(buf);

    if (pixels < (int)cfg->ui_seek_screen_min_bar_pixels) {
        log_msg("ui-seek screen guard blocked pixels=%d min=%u y=%u",
                pixels, cfg->ui_seek_screen_min_bar_pixels,
                cfg->ui_seek_bar_y);
        return false;
    }
    return true;
}

bool audiobook_subheader_visible(int fb_fd, const daemon_config *cfg) {
    if (fb_fd < 0) return false;
    int pixels = fb_white_pixels_region(fb_fd, 60, 118, 220, 155,
                                        cfg->ui_seek_fb_stride);
    if (pixels < 0) return false;
    return pixels >= (int)cfg->back_guard_subheader_min_white;
}

bool audiobook_title_list_visible(int fb_fd, const daemon_config *cfg) {
    if (fb_fd < 0) return false;

    int subheader_pixels = fb_white_pixels_region(fb_fd, 60, 118, 220, 155,
                                                   cfg->ui_seek_fb_stride);
    int header_mid_pixels = fb_white_pixels_region(fb_fd, 170, 70, 260, 110,
                                                     cfg->ui_seek_fb_stride);
    int header_icon_pixels = fb_white_pixels_region(fb_fd, 400, 75, 440, 110,
                                                      cfg->ui_seek_fb_stride);

    if (subheader_pixels < 0) subheader_pixels = 0;
    if (header_mid_pixels < 0) header_mid_pixels = 0;
    if (header_icon_pixels < 0) header_icon_pixels = 0;

    if (subheader_pixels < (int)cfg->back_guard_subheader_min_white)
        return false;
    if (header_mid_pixels > 120)
        return false;
    if (header_icon_pixels < 300)
        return false;
    return true;
}

bool audiobook_track_list_visible(int fb_fd, const daemon_config *cfg) {
    if (fb_fd < 0) return false;

    int subheader_pixels = fb_white_pixels_region(fb_fd, 60, 118, 220, 155,
                                                   cfg->ui_seek_fb_stride);
    int header_pixels = fb_white_pixels_region(fb_fd, 60, 70, 380, 110,
                                                cfg->ui_seek_fb_stride);

    if (subheader_pixels < 0) subheader_pixels = 0;
    if (header_pixels < 0) header_pixels = 0;

    if (subheader_pixels < (int)cfg->back_guard_subheader_min_white)
        return false;
    if (audiobook_title_list_visible(fb_fd, cfg))
        return false;
    if (header_pixels < 150)
        return false;
    return true;
}

bool audiobook_global_back_target_visible(int fb_fd, const daemon_config *cfg) {
    if (fb_fd < 0) return false;

    int subheader_pixels = fb_white_pixels_region(fb_fd, 60, 118, 220, 155,
                                                   cfg->ui_seek_fb_stride);
    int header_pixels = fb_white_pixels_region(fb_fd, 20, 70, 230, 110,
                                                cfg->ui_seek_fb_stride);
    int back_pixels = fb_white_pixels_region(fb_fd, 15, 75, 55, 105,
                                              cfg->ui_seek_fb_stride);

    if (subheader_pixels < 0) subheader_pixels = 0;
    if (header_pixels < 0) header_pixels = 0;
    if (back_pixels < 0) back_pixels = 0;

    if (subheader_pixels > (int)cfg->back_guard_subheader_max_white)
        return false;
    if (header_pixels < (int)cfg->back_guard_header_min_white)
        return false;
    if (back_pixels < (int)cfg->back_guard_back_arrow_min_white)
        return false;
    return true;
}

/* ── Input event injection ───────────────────────────────────────── */

/*
 * input_event struct on mipsel LE: 24 bytes
 *   struct timeval time: 8 bytes (two 32-bit LE: tv_sec, tv_usec)
 *   uint16_t type:  2 bytes
 *   uint16_t code:  2 bytes
 *   int32_t value:  4 bytes
 * Total: 16 bytes on 32-bit MIPS LE (timeval is 8 bytes: two int32).
 *
 * Wait — the spec says 24 bytes. On MIPS32 LE, struct timeval has
 * two 32-bit fields = 8 bytes. So input_event = 8 + 2 + 2 + 4 = 16 bytes.
 * But the shell packs 24 bytes (it writes le32 + le32 + le16 + le16 + le32
 * = 4+4+2+2+4 = 16 bytes). Actually the shell writes:
 *   emit_input_le32 0  (4 bytes: tv_sec)
 *   emit_input_le32 0  (4 bytes: tv_usec)
 *   emit_input_le16 type (2 bytes)
 *   emit_input_le16 code (2 bytes)
 *   emit_input_le32 value (4 bytes)
 * Total = 4+4+2+2+4 = 16 bytes.
 *
 * On x86_64 Linux, struct input_event has 64-bit timeval, so it's 24 bytes.
 * On mipsel 32-bit, it's 16 bytes.
 *
 * For cross-platform compatibility, we pack the event manually to match
 * the target device (16 bytes on MIPS LE).
 */
#define INPUT_EVENT_SIZE_MIPS 16

int send_input_event(int fd, uint16_t type, uint16_t code, int32_t value) {
    unsigned char buf[INPUT_EVENT_SIZE_MIPS];
    /* tv_sec = 0 (4 bytes LE) */
    buf[0] = 0; buf[1] = 0; buf[2] = 0; buf[3] = 0;
    /* tv_usec = 0 (4 bytes LE) */
    buf[4] = 0; buf[5] = 0; buf[6] = 0; buf[7] = 0;
    /* type (2 bytes LE) */
    buf[8] = (unsigned char)(type & 0xFF);
    buf[9] = (unsigned char)((type >> 8) & 0xFF);
    /* code (2 bytes LE) */
    buf[10] = (unsigned char)(code & 0xFF);
    buf[11] = (unsigned char)((code >> 8) & 0xFF);
    /* value (4 bytes LE) */
    buf[12] = (unsigned char)(value & 0xFF);
    buf[13] = (unsigned char)((value >> 8) & 0xFF);
    buf[14] = (unsigned char)((value >> 16) & 0xFF);
    buf[15] = (unsigned char)((value >> 24) & 0xFF);

    size_t total = INPUT_EVENT_SIZE_MIPS;
    size_t done = 0;
    while (done < total) {
        ssize_t n = write(fd, buf + done, total - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

int emit_touch_abs_frame(int fd, uint16_t x, uint16_t y, int include_press) {
    /* ABS_MT_TRACKING_ID = 0 */
    if (send_input_event(fd, 3, 57, 0) < 0) return -1;
    /* ABS_MT_PRESSURE = 63 */
    if (send_input_event(fd, 3, 58, 63) < 0) return -1;
    /* ABS_MT_TOUCH_MAJOR = 9 */
    if (send_input_event(fd, 3, 48, 9) < 0) return -1;
    /* ABS_MT_POSITION_X = x */
    if (send_input_event(fd, 3, 53, (int32_t)x) < 0) return -1;
    /* ABS_MT_POSITION_Y = y */
    if (send_input_event(fd, 3, 54, (int32_t)y) < 0) return -1;
    /* SYN_REPORT */
    if (send_input_event(fd, 0, 2, 0) < 0) return -1;
    /* BTN_TOUCH = 1 (if press) */
    if (include_press) {
        if (send_input_event(fd, 1, 330, 1) < 0) return -1;
    }
    /* SYN_REPORT */
    if (send_input_event(fd, 0, 0, 0) < 0) return -1;
    return 0;
}

int write_touch_tap_stream(const char *output_file, uint16_t x, uint16_t y,
                           uint8_t frames) {
    if (!output_file || frames == 0) return -1;

    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    int rc = 0;

    /* Press frame */
    if (emit_touch_abs_frame(fd, x, y, 1) < 0) { rc = -1; goto done; }

    /* Additional frames (no press) */
    for (uint8_t i = 1; i < frames; i++) {
        if (emit_touch_abs_frame(fd, x, y, 0) < 0) { rc = -1; goto done; }
    }

    /* Release: BTN_TOUCH = 0 */
    if (send_input_event(fd, 1, 330, 0) < 0) { rc = -1; goto done; }
    /* SYN_REPORT */
    if (send_input_event(fd, 0, 2, 0) < 0) { rc = -1; goto done; }
    /* SYN_REPORT */
    if (send_input_event(fd, 0, 0, 0) < 0) { rc = -1; goto done; }

done:
    close(fd);
    return rc;
}

int ui_send_event_file(const char *label, const char *event_file,
                       const char *event_node) {
    if (!event_file || !event_node || !label)
        return -1;

    /* Check event file exists and is readable */
    if (access(event_file, R_OK) != 0) {
        log_msg("%s unavailable missing=%s", label, event_file);
        return -1;
    }

    /* Check event node exists and is writable */
    if (access(event_node, W_OK) != 0) {
        log_msg("%s unavailable missing=%s", label, event_node);
        return -1;
    }

    /* Open event file */
    int in_fd = open(event_file, O_RDONLY);
    if (in_fd < 0) {
        log_msg("%s cannot open event file %s: %s", label, event_file,
                 strerror(errno));
        return -1;
    }

    /* Open event node */
    int out_fd = open(event_node, O_WRONLY);
    if (out_fd < 0) {
        log_msg("%s cannot open event node %s: %s", label, event_node,
                 strerror(errno));
        close(in_fd);
        return -1;
    }

    /* Copy file → device node */
    char buf[4096];
    int rc = 0;
    while (1) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            log_msg("%s read error: %s", label, strerror(errno));
            rc = -1;
            break;
        }
        if (n == 0) break;

        size_t done = 0;
        while (done < (size_t)n) {
            ssize_t w = write(out_fd, buf + done, (size_t)n - done);
            if (w < 0) {
                if (errno == EINTR) continue;
                log_msg("%s write error: %s", label, strerror(errno));
                rc = -1;
                break;
            }
            done += (size_t)w;
        }
        if (rc < 0) break;
    }

    close(in_fd);
    close(out_fd);
    return rc;
}

int touch_generated_tap(const char *label, uint16_t x, uint16_t y,
                        uint8_t frames, const daemon_config *cfg) {
    if (!label || !cfg) return -1;

    /* Build temp file path: base_dir/input/label.pid.bin */
    char tmp[384];
    snprintf(tmp, sizeof(tmp), "%s/input/%s.%d.bin",
             cfg->base_dir, label, (int)getpid());

    /* Ensure input dir exists */
    {
        char dir[384];
        snprintf(dir, sizeof(dir), "%s/input", cfg->base_dir);
        mkdir(dir, 0755);
    }

    if (write_touch_tap_stream(tmp, x, y, frames) < 0) {
        log_msg("%s failed writing tap x=%u y=%u", label, x, y);
        unlink(tmp);
        return -1;
    }

    int rc = ui_send_event_file(label, tmp, cfg->touch_event_node);
    unlink(tmp);
    return rc;
}

/* ── High-level touch helpers ─────────────────────────────────────── */

int touch_first_track(const daemon_config *cfg) {
    /* Try the single-file approach first */
    if (access(cfg->touch_first_track_event_file, R_OK) == 0) {
        return ui_send_event_file("touch-first-track",
                                   cfg->touch_first_track_event_file,
                                   cfg->touch_event_node);
    }

    /* Try the down/move/up sequence */
    if (access(cfg->touch_first_track_down_event_file, R_OK) == 0 &&
        access(cfg->touch_first_track_move_event_file, R_OK) == 0 &&
        access(cfg->touch_first_track_up_event_file, R_OK) == 0) {
        if (ui_send_event_file("touch-first-track-down",
                                cfg->touch_first_track_down_event_file,
                                cfg->touch_event_node) < 0)
            return -1;
        if (cfg->touch_first_track_hold_us > 0)
            usleep(cfg->touch_first_track_hold_us);
        if (ui_send_event_file("touch-first-track-move",
                                cfg->touch_first_track_move_event_file,
                                cfg->touch_event_node) < 0)
            return -1;
        if (cfg->touch_first_track_hold_us > 0)
            usleep(cfg->touch_first_track_hold_us);
        if (ui_send_event_file("touch-first-track-up",
                                cfg->touch_first_track_up_event_file,
                                cfg->touch_event_node) < 0)
            return -1;
        return 0;
    }

    /* Final fallback: try the single file even if access failed (will log error) */
    return ui_send_event_file("touch-first-track",
                               cfg->touch_first_track_event_file,
                               cfg->touch_event_node);
}

int touch_back_to_track_list(const daemon_config *cfg) {
    return ui_send_event_file("touch-back",
                               cfg->touch_back_event_file,
                               cfg->touch_event_node);
}

int touch_track_row(int row, const daemon_config *cfg) {
    if (row < 1 || row > 5) return -1;
    char label[32];
    snprintf(label, sizeof(label), "touch-track-row%d", row);
    return ui_send_event_file(label,
                               cfg->touch_track_row_event_files[row - 1],
                               cfg->touch_event_node);
}

int touch_track_swipe_up(const daemon_config *cfg) {
    /* Swipe down first */
    if (ui_send_event_file("touch-track-swipe-down",
                           cfg->touch_track_swipe_down_event_file,
                           cfg->touch_event_node) < 0)
        return -1;
    if (cfg->touch_track_swipe_phase_us > 0)
        usleep(cfg->touch_track_swipe_phase_us);

    /* 6 move events */
    for (int i = 0; i < 6; i++) {
        char label[32];
        snprintf(label, sizeof(label), "touch-track-swipe-move%d", i + 1);
        if (ui_send_event_file(label,
                                cfg->touch_track_swipe_move_event_files[i],
                                cfg->touch_event_node) < 0)
            return -1;
        if (cfg->touch_track_swipe_phase_us > 0)
            usleep(cfg->touch_track_swipe_phase_us);
    }

    /* Swipe up */
    if (ui_send_event_file("touch-track-swipe-up",
                           cfg->touch_track_swipe_up_event_file,
                           cfg->touch_event_node) < 0)
        return -1;

    return 0;
}

int track_next(const daemon_config *cfg) {
    /* Try key-next first */
    if (access(cfg->key_next_event_file, R_OK) == 0) {
        return ui_send_event_file("key-next",
                                   cfg->key_next_event_file,
                                   cfg->key_next_event_node);
    }
    /* Fallback: touch-next */
    return ui_send_event_file("touch-next",
                               cfg->touch_next_event_file,
                               cfg->touch_event_node);
}

int track_prev(const daemon_config *cfg) {
    return ui_send_event_file("key-prev",
                               cfg->key_prev_event_file,
                               cfg->key_prev_event_node);
}

/* ── Seek-bar restore ─────────────────────────────────────────────── */

uint16_t ui_seek_compute_x(uint32_t saved_pos, uint32_t duration,
                           uint16_t x_min, uint16_t x_max) {
    if (duration == 0 || x_max <= x_min + 2) return 0;

    uint32_t range = (uint32_t)(x_max - x_min);
    /* x = x_min + (saved_pos * range + duration/2) / duration */
    uint32_t x = x_min + (saved_pos * range + duration / 2) / duration;

    /* Clamp to [x_min+1, x_max-1] */
    if (x <= x_min) x = x_min + 1;
    if (x >= x_max) x = x_max - 1;

    return (uint16_t)x;
}

int ui_seek_restore(const char *path, uint32_t saved_pos,
                    const daemon_config *cfg) {
    if (!cfg->ui_seek_fallback_enabled) return -1;
    if (!path) return -1;

    /* Read duration from player memory */
    uint32_t duration = duration_ms_memory(cfg);
    if (duration == 0) {
        log_msg("ui-seek unavailable duration path=%s saved_ms=%u",
                path, saved_pos);
        return -1;
    }

    if (duration < cfg->ui_seek_min_duration_ms) {
        log_msg("ui-seek skipped short-duration path=%s saved_ms=%u duration_ms=%u",
                path, saved_pos, duration);
        return -1;
    }

    if (saved_pos == 0 || saved_pos >= duration) {
        log_msg("ui-seek skipped out-of-range path=%s saved_ms=%u duration_ms=%u",
                path, saved_pos, duration);
        return -1;
    }

    /* Open framebuffer for screen guard check */
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd >= 0) {
        if (!ui_seek_screen_ready(fb_fd, cfg)) {
            close(fb_fd);
            return -1;
        }
        close(fb_fd);
    }

    /* Compute X position on seek bar */
    uint16_t x = ui_seek_compute_x(saved_pos, duration,
                                    cfg->ui_seek_bar_x_min,
                                    cfg->ui_seek_bar_x_max);
    if (x == 0) return -1;

    log_msg("ui-seek attempt path=%s saved_ms=%u duration_ms=%u x=%u y=%u",
            path, saved_pos, duration, x, cfg->ui_seek_bar_y);

    /* Tap the seek bar */
    if (touch_generated_tap("ui-seek", x, cfg->ui_seek_bar_y,
                             cfg->ui_seek_touch_frames, cfg) < 0)
        return -1;

    /* Wait for seek to settle */
    if (cfg->ui_seek_verify_delay_seconds > 0)
        sleep(cfg->ui_seek_verify_delay_seconds);

    /* Verify: check path hasn't changed */
    char path_after[512];
    if (current_path_from_hex(cfg, path_after, sizeof(path_after)) == 0) {
        if (strcmp(path_after, path) != 0) {
            log_msg("ui-seek path changed path=%s after=%s saved_ms=%u",
                    path, path_after, saved_pos);
            return -1;
        }
    }

    /* Verify: check position is within tolerance */
    uint32_t pos_after = position_ms_memory(cfg);
    if (pos_after == 0) {
        log_msg("ui-seek verify unavailable path=%s saved_ms=%u", path, saved_pos);
        return -1;
    }

    uint32_t range = (uint32_t)(cfg->ui_seek_bar_x_max - cfg->ui_seek_bar_x_min);
    uint32_t tolerance = cfg->ui_seek_verify_tolerance_ms;
    if (range > 0)
        tolerance += duration / range;

    int32_t diff = (int32_t)(pos_after > saved_pos ?
                              pos_after - saved_pos : saved_pos - pos_after);

    if (diff <= (int32_t)tolerance) {
        log_msg("ui-seek restored path=%s saved_ms=%u pos_ms=%u duration_ms=%u diff_ms=%d tolerance_ms=%u",
                path, saved_pos, pos_after, duration, diff, tolerance);
        return 0;
    }

    log_msg("ui-seek verify failed path=%s saved_ms=%u pos_ms=%u duration_ms=%u diff_ms=%d tolerance_ms=%u",
            path, saved_pos, pos_after, duration, diff, tolerance);
    return -1;
}

/* ── Play-mode enforcement ────────────────────────────────────────── */

int play_mode_value(const daemon_config *cfg) {
    if (cfg->play_mode_user_ini_offset == 0) return -1;

    int fd = open(cfg->user_ini_path, O_RDONLY);
    if (fd < 0) return -1;

    if (lseek(fd, (off_t)cfg->play_mode_user_ini_offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return -1;
    }

    unsigned char byte;
    ssize_t n = read(fd, &byte, 1);
    close(fd);

    if (n != 1) return -1;
    return (int)byte;
}

bool play_mode_screen_ready(int fb_fd, const daemon_config *cfg) {
    if (!cfg->play_mode_screen_guard_enabled) return true;
    return ui_seek_screen_ready(fb_fd, cfg);
}

int ensure_audiobook_play_mode(const daemon_config *cfg) {
    if (!cfg->play_mode_enforce_enabled) return 0;
    if (cfg->play_mode_max_taps == 0) return -1;
    if (cfg->play_mode_touch_x == 0 && cfg->play_mode_touch_y == 0) return -1;

    int mode = play_mode_value(cfg);
    if (mode < 0) {
        log_msg("play-mode unavailable target=%u", cfg->play_mode_target);
        return -1;
    }

    if (mode == (int)cfg->play_mode_target) return 0;

    /* Check screen if guard enabled */
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd >= 0) {
        bool ready = play_mode_screen_ready(fb_fd, cfg);
        close(fb_fd);
        if (!ready) {
            log_msg("play-mode skipped screen-not-ready mode=%d target=%u",
                    mode, cfg->play_mode_target);
            return -1;
        }
    }

    int taps = 0;
    while (taps < (int)cfg->play_mode_max_taps) {
        int old_mode = mode;
        if (touch_generated_tap("play-mode", cfg->play_mode_touch_x,
                                 cfg->play_mode_touch_y,
                                 cfg->ui_seek_touch_frames, cfg) < 0)
            return -1;
        taps++;

        if (cfg->play_mode_settle_seconds > 0)
            sleep(cfg->play_mode_settle_seconds);

        mode = play_mode_value(cfg);
        if (mode < 0) mode = -1;

        log_msg("play-mode tap=%d mode=%d->%d target=%u",
                taps, old_mode, mode, cfg->play_mode_target);

        if (mode == (int)cfg->play_mode_target) return 0;
    }

    log_msg("play-mode failed mode=%d target=%u taps=%d",
            mode, cfg->play_mode_target, taps);
    return -1;
}

/* ── Back guard ───────────────────────────────────────────────────── */

void enable_audiobook_back_guard_window(time_t now,
                                        back_guard_state *bg,
                                        const daemon_config *cfg) {
    if (!cfg->back_guard_enabled) return;
    if (cfg->back_guard_window_seconds == 0) return;
    if (!bg) return;
    bg->audiobook_back_guard_until = now + (time_t)cfg->back_guard_window_seconds;
}

void maybe_audiobook_back_guard(time_t now, const char *path_preview,
                                back_guard_state *bg,
                                const daemon_config *cfg) {
    if (!cfg->back_guard_enabled) return;
    if (!bg) return;

    /* Determine if back guard is active */
    int active = 0;
    if (bg->audiobook_back_guard_until > now) {
        active = 1;
    } else if (bg->audiobook_back_guard_seen_at > 0 &&
               (now - bg->audiobook_back_guard_seen_at) <=
               (time_t)cfg->back_guard_after_screen_seconds) {
        active = 1;
    } else if (path_preview &&
               !path_preview_is_music(path_preview) &&
               !path_preview_is_audiobook(path_preview)) {
        active = 1;
    }

    if (!active) return;

    /* Open framebuffer */
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) return;

    /* Check if audiobook subheader is visible */
    if (audiobook_subheader_visible(fb_fd, cfg)) {
        bg->audiobook_back_guard_seen_at = now;
        enable_audiobook_back_guard_window(now, bg, cfg);

        /* If title list visible and context window configured, extend context */
        if (audiobook_title_list_visible(fb_fd, cfg) &&
            cfg->book_title_context_seconds > 0) {
            /* book_title_context_until would be set here in full state machine */
        }
        close(fb_fd);
        return;
    }

    close(fb_fd);

    /* Subheader not visible — check if we've seen it recently */
    if (bg->audiobook_back_guard_seen_at == 0) return;
    if ((now - bg->audiobook_back_guard_seen_at) >
        (time_t)cfg->back_guard_after_screen_seconds)
        return;

    /* Check for global back target */
    fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) return;

    if (audiobook_global_back_target_visible(fb_fd, cfg)) {
        close(fb_fd);

        /* Rate-limit: don't fire too frequently */
        if (bg->audiobook_back_guard_last_fire_at > 0 &&
            (now - bg->audiobook_back_guard_last_fire_at) <
            (time_t)cfg->back_guard_after_screen_seconds) {
            return;
        }

        log_msg("back-guard extra-back after audiobook list count=%u",
                cfg->back_guard_extra_backs);

        int backs = (int)cfg->back_guard_extra_backs;
        while (backs > 0) {
            if (touch_back_to_track_list(cfg) < 0)
                log_msg("back-guard extra-back failed remaining=%d", backs);
            backs--;
            if (backs <= 0) break;
            if (cfg->back_guard_settle_seconds > 0)
                sleep(cfg->back_guard_settle_seconds);
        }

        bg->audiobook_back_guard_last_fire_at = now;
        bg->audiobook_back_guard_seen_at = 0;
        bg->audiobook_back_guard_until = 0;

        if (cfg->back_guard_settle_seconds > 0)
            sleep(cfg->back_guard_settle_seconds);
    } else {
        close(fb_fd);
    }
}

/* ── Book title wait ──────────────────────────────────────────────── */

int book_title_wait_for_launcher_track_list(const daemon_config *cfg) {
    if (cfg->book_title_launcher_tracklist_wait_seconds == 0) return -1;

    /* Sleep first (matching shell behavior) */
    sleep(cfg->book_title_launcher_tracklist_wait_seconds);

    /* Check if track list is visible */
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) return -1;

    bool visible = audiobook_track_list_visible(fb_fd, cfg);
    close(fb_fd);

    if (visible) {
        log_msg("book-title launcher track-list visible");
        return 0;
    }
    return -1;
}