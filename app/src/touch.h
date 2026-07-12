#ifndef R1_AB_TOUCH_H
#define R1_AB_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TOUCH_NONE = 0,
    TOUCH_TAP,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
    TOUCH_BACK_EDGE,
    TOUCH_SWIPE_RIGHT,
} touch_action;

typedef struct touch_event {
    touch_action action;
    int x;
    int y;
    int dx;
    int dy;
} touch_event;

typedef struct touch_context {
    int fd;
    bool down;
    int start_x;
    int start_y;
    int last_x;
    int last_y;
} touch_context;

int touch_open(touch_context *touch, const char *path);
void touch_close(touch_context *touch);
int touch_poll(touch_context *touch, touch_event *ev, int timeout_ms);

#endif

