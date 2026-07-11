#include "touch.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

int touch_open(touch_context *touch, const char *path) {
    if (!touch || !path) return -1;
    memset(touch, 0, sizeof(*touch));
    touch->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    return touch->fd >= 0 ? 0 : -1;
}

void touch_close(touch_context *touch) {
    if (!touch) return;
    if (touch->fd >= 0) close(touch->fd);
    memset(touch, 0, sizeof(*touch));
    touch->fd = -1;
}

int touch_poll(touch_context *touch, touch_event *ev, int timeout_ms) {
    if (!touch || !ev || touch->fd < 0) return -1;
    struct pollfd pfd = {.fd = touch->fd, .events = POLLIN};
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return -1;
    struct input_event ie;
    ssize_t n;
    memset(ev, 0, sizeof(*ev));
    while ((n = read(touch->fd, &ie, sizeof(ie))) == (ssize_t)sizeof(ie)) {
        if (ie.type == EV_ABS) {
            if (ie.code == ABS_MT_POSITION_X || ie.code == ABS_X) touch->last_x = ie.value;
            if (ie.code == ABS_MT_POSITION_Y || ie.code == ABS_Y) touch->last_y = ie.value;
        } else if (ie.type == EV_KEY && ie.code == BTN_TOUCH) {
            if (ie.value) {
                touch->down = true;
                touch->start_x = touch->last_x;
                touch->start_y = touch->last_y;
            } else if (touch->down) {
                int dx = touch->last_x - touch->start_x;
                int dy = touch->last_y - touch->start_y;
                touch->down = false;
                ev->x = touch->last_x;
                ev->y = touch->last_y;
                ev->dx = dx;
                ev->dy = dy;
                if (abs(dx) < 40 && abs(dy) < 40) ev->action = TOUCH_TAP;
                else if (dy < -80 && abs(dx) < 150) ev->action = TOUCH_SWIPE_UP;
                else if (dy > 80 && abs(dx) < 150) ev->action = TOUCH_SWIPE_DOWN;
                else if (touch->start_x < 40 && dx > 120) ev->action = TOUCH_BACK_EDGE;
                else ev->action = TOUCH_NONE;
                return 0;
            }
        }
    }
    return -1;
}
