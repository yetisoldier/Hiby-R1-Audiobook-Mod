/*
 * ui.c - compatibility stubs retained for the legacy state machine.
 *
 * The daemon no longer performs framebuffer reads or touch injection,
 * but the older state.c implementation still calls these helpers.
 */

#include "ui.h"

int ensure_audiobook_play_mode(const daemon_config *cfg) {
    (void)cfg;
    return 0;
}

int play_mode_value(const daemon_config *cfg) {
    return cfg ? cfg->play_mode_target : 0;
}

bool audiobook_title_list_visible(int fb, const daemon_config *cfg) {
    (void)fb;
    (void)cfg;
    return false;
}

bool audiobook_track_list_visible(int fb, const daemon_config *cfg) {
    (void)fb;
    (void)cfg;
    return false;
}

int touch_first_track(const daemon_config *cfg) {
    (void)cfg;
    return 0;
}

int touch_back_to_track_list(const daemon_config *cfg) {
    (void)cfg;
    return 0;
}

int touch_track_row(int row, const daemon_config *cfg) {
    (void)row;
    (void)cfg;
    return 0;
}

int touch_track_swipe_up(const daemon_config *cfg) {
    (void)cfg;
    return 0;
}

int track_next(const daemon_config *cfg) {
    (void)cfg;
    return 0;
}

int track_prev(const daemon_config *cfg) {
    (void)cfg;
    return 0;
}

void maybe_audiobook_back_guard(time_t now, const char *path_preview,
                                back_guard_state *bg, const daemon_config *cfg) {
    (void)now;
    (void)path_preview;
    (void)bg;
    (void)cfg;
}
