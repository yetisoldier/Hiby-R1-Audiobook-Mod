#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_EVENT_NODE "/dev/input/event1"
#define DEFAULT_REQUEST_PATH "/usr/data/audiobooks/bookmark.request"
#define DEFAULT_USER_INI "/usr/data/user.ini"

typedef struct {
    const char *event_node;
    const char *request_path;
    const char *user_ini_path;
    int hold_ms;
    int slop_px;
    int x_max;
    int y_max;
    int cooldown_ms;
} Options;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int path_is_audiobook(const char *user_ini_path) {
    unsigned char buf[128];
    int fd = open(user_ini_path, O_RDONLY);
    if (fd < 0) return 0;
    if (lseek(fd, 40, SEEK_SET) < 0) {
        close(fd);
        return 0;
    }
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) return 0;

    char ascii[128];
    size_t j = 0;
    for (ssize_t i = 0; i + 1 < n && j + 1 < sizeof(ascii); i += 2) {
        if (buf[i] == 0 && buf[i + 1] == 0) break;
        ascii[j++] = (char)buf[i];
    }
    ascii[j] = '\0';

    return strncmp(ascii, "a:\\Audiobooks\\", 14) == 0 ||
           strncmp(ascii, "A:\\Audiobooks\\", 14) == 0 ||
           strncmp(ascii, "\\Audiobooks\\", 12) == 0;
}

static int write_request_file(const char *request_path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", request_path, (long)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    const char *payload = "bookmark\n";
    ssize_t wrote = write(fd, payload, (size_t)strlen(payload));
    close(fd);
    if (wrote < 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, request_path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int abs_i(int value) {
    return value < 0 ? -value : value;
}

static Options parse_args(int argc, char **argv) {
    Options opts;
    opts.event_node = DEFAULT_EVENT_NODE;
    opts.request_path = DEFAULT_REQUEST_PATH;
    opts.user_ini_path = DEFAULT_USER_INI;
    opts.hold_ms = 900;
    opts.slop_px = 24;
    opts.x_max = 80;
    opts.y_max = 90;
    opts.cooldown_ms = 1500;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--event") == 0 && i + 1 < argc) opts.event_node = argv[++i];
        else if (strcmp(argv[i], "--request") == 0 && i + 1 < argc) opts.request_path = argv[++i];
        else if (strcmp(argv[i], "--user-ini") == 0 && i + 1 < argc) opts.user_ini_path = argv[++i];
        else if (strcmp(argv[i], "--hold-ms") == 0 && i + 1 < argc) opts.hold_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--slop-px") == 0 && i + 1 < argc) opts.slop_px = atoi(argv[++i]);
        else if (strcmp(argv[i], "--x-max") == 0 && i + 1 < argc) opts.x_max = atoi(argv[++i]);
        else if (strcmp(argv[i], "--y-max") == 0 && i + 1 < argc) opts.y_max = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cooldown-ms") == 0 && i + 1 < argc) opts.cooldown_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fprintf(stderr,
                    "usage: r1_audiobook_bookmark_monitor [options]\n"
                    "  --event PATH\n"
                    "  --request PATH\n"
                    "  --user-ini PATH\n"
                    "  --hold-ms N\n"
                    "  --slop-px N\n"
                    "  --x-max N\n"
                    "  --y-max N\n"
                    "  --cooldown-ms N\n");
            exit(0);
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            exit(2);
        }
    }
    return opts;
}

int main(int argc, char **argv) {
    Options opts = parse_args(argc, argv);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    int fd = open(opts.event_node, O_RDONLY);
    if (fd < 0) {
        perror("open event node");
        return 1;
    }

    int active = 0;
    int inside_region = 0;
    int moved_too_far = 0;
    int x = -1, y = -1, start_x = -1, start_y = -1;
    long long down_ms = 0;
    long long last_fire_ms = 0;

    while (!g_stop) {
        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == 0) continue;
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read event");
            break;
        }
        if ((size_t)n != sizeof(ev)) continue;

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X) x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) y = ev.value;

            if (active && start_x >= 0 && start_y >= 0 && x >= 0 && y >= 0) {
                if (abs_i(x - start_x) > opts.slop_px || abs_i(y - start_y) > opts.slop_px) {
                    moved_too_far = 1;
                }
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                active = 1;
                down_ms = now_ms();
                start_x = x;
                start_y = y;
                moved_too_far = 0;
                inside_region = (x >= 0 && y >= 0 && x <= opts.x_max && y <= opts.y_max);
            } else if (ev.value == 0) {
                long long released_ms = now_ms();
                if (active && inside_region && !moved_too_far && start_x >= 0 && start_y >= 0) {
                    long long held_ms = released_ms - down_ms;
                    if (held_ms >= opts.hold_ms &&
                        (released_ms - last_fire_ms) >= opts.cooldown_ms &&
                        path_is_audiobook(opts.user_ini_path)) {
                        if (write_request_file(opts.request_path) == 0) {
                            last_fire_ms = released_ms;
                        }
                    }
                }
                active = 0;
                inside_region = 0;
                moved_too_far = 0;
                start_x = start_y = -1;
            }
        }
    }

    close(fd);
    return 0;
}
