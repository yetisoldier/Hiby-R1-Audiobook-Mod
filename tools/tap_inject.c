/* tap_inject.c — inject a touch tap on the R1 touchscreen
   Usage: tap_inject <x> <y> */
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void write_ev(int fd, struct timespec *ts, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.input_event_sec = ts->tv_sec;
    ev.input_event_usec = ts->tv_nsec / 1000;
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(fd, &ev, sizeof(ev));
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <x> <y>\n", argv[0]); return 1; }
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    int fd = open("/dev/input/event1", O_WRONLY);
    if (fd < 0) { perror("open"); return 1; }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    /* Touch down */
    write_ev(fd, &ts, EV_ABS, ABS_MT_TRACKING_ID, 0);
    write_ev(fd, &ts, EV_ABS, ABS_MT_POSITION_X, x);
    write_ev(fd, &ts, EV_ABS, ABS_MT_POSITION_Y, y);
    write_ev(fd, &ts, EV_KEY, BTN_TOUCH, 1);
    write_ev(fd, &ts, EV_SYN, SYN_REPORT, 0);
    usleep(50000);
    /* Touch up */
    write_ev(fd, &ts, EV_ABS, ABS_MT_TRACKING_ID, -1);
    write_ev(fd, &ts, EV_KEY, BTN_TOUCH, 0);
    write_ev(fd, &ts, EV_SYN, SYN_REPORT, 0);
    close(fd);
    printf("Tap at (%d,%d)\n", x, y);
    return 0;
}