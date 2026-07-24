/* ui.c — audiobook UI state machine + event loop.
 *
 * Runs inside hook_b when the user taps the Audiobooks tile. Renders to
 * hiby_player's mmap'd framebuffer, reads touch from /dev/input/event1.
 *
 * The home screen shows: Continue Listening, Titles, Authors, Series,
 * Folders, Finished, Refresh, Back. Touch items navigate to list/detail
 * screens. Back returns to the launcher. (ADB is always on at boot via the
 * firmware's S90adb init script when System -> USB working mode = Device.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <linux/input.h>
#include <linux/fb.h>
#include "ui.h"
#include "scan.h"
#include "player.h"
#include "cover.h"
#include "storage_guard.h"

/* EVIOCGRAB: exclusive grab on an input device so hiby_player's own fd
 * doesn't receive touch events while we're in audiobook mode. */
#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

/* ---- Layout constants --------------------------------------------------- */
/* Tuned for the larger truetype sizes (FONT_PX_1=28/2=44/3=64/4=36). Title
 * bar fits a 44 px header; home rows are single-line (78 px), list rows hold
 * a 44 px title + 28 px metadata line + progress bar (108 px, ~6 visible). */
#define TITLE_BAR_H     76
#define HOME_ITEM_H     78
#define LIST_ITEM_H     108
#define ITEM_PAD        4
#define FOOTER_H        44
#define SCROLL_SPEED    3   /* pixels per touch move event */
/* Bottom-anchored detail controls. Shared by drawing and hit-testing so the
 * visible buttons and their touch targets cannot drift apart. */
#define DETAIL_BTN_H            64
#define DETAIL_BTN_GAP          12
#define DETAIL_BOTTOM_MARGIN    16
#define DETAIL_BTN_ROW2_Y       (RENDER_FB_H - DETAIL_BOTTOM_MARGIN - DETAIL_BTN_H)
#define DETAIL_BTN_ROW1_Y       (DETAIL_BTN_ROW2_Y - DETAIL_BTN_GAP - DETAIL_BTN_H)
#define DETAIL_PROGRESS_BAR_Y   (DETAIL_BTN_ROW1_Y - 34)
#define DETAIL_PROGRESS_TIME_Y  (DETAIL_PROGRESS_BAR_Y - 52)
#define DETAIL_PROGRESS_LABEL_Y (DETAIL_PROGRESS_TIME_Y - 32)
/* Cap on chapter rows rendered/seekable at once. Bounds the chapter-list
 * render allocation (each row ~264B) and the tap-seek start_ms array so a
 * pathological chapter count (huge multi-file book, bogus embedded_chapters)
 * can't OOM this 56MB device. Real audiobooks have well under this. */
#define MAX_CHAPTER_ROWS 2048
/* Scrollable list viewport (between the title bar and the footer). */
#define LIST_VIEWPORT_H (RENDER_FB_H - FOOTER_H - TITLE_BAR_H)

/* ---- Home screen items -------------------------------------------------- */

typedef enum {
    HOME_CONTINUE = 0,
    HOME_TITLES,
    HOME_AUTHORS,
    HOME_SERIES,
    HOME_FOLDERS,
    HOME_FINISHED,
    HOME_REFRESH,
    HOME_BACK,
    HOME_ITEM_COUNT
} home_item_t;

static const char *home_labels[HOME_ITEM_COUNT] = {
    "Continue",
    "Titles",
    "Authors",
    "Series",
    "Folders",
    "Finished",
    "Refresh Library",
    "Back to Menu",
};

/* ---- Forward declarations ----------------------------------------------- */

static void draw_home(ui_state_t *ui);
static void draw_list(ui_state_t *ui);
static void draw_detail(ui_state_t *ui);
static void draw_now_playing(ui_state_t *ui);
static void draw_bookmarks(ui_state_t *ui);
static void draw_chapters(ui_state_t *ui);
static void draw_volume_overlay(ui_state_t *ui);

typedef struct {
    pthread_t thread;
    pthread_mutex_t mu;
    int running;
    int done;
    int result;
} scan_worker_t;

static scan_worker_t g_scan = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
};

static void *scan_worker_main(void *arg) {
    (void)arg;
    sqlite3 *db = NULL;
    int rc = -1;
    audiobook_db_write_lock();
    int open_rc = audiobook_db_open(AUDIOBOOK_DB_PATH, &db);
    audiobook_db_write_unlock();
    if (open_rc == 0) {
        rc = audiobook_scan_library(db, AUDIOBOOK_LIBRARY_ROOT, NULL, NULL);
        audiobook_db_close(db);
    }
    pthread_mutex_lock(&g_scan.mu);
    g_scan.result = rc;
    g_scan.done = 1;
    pthread_mutex_unlock(&g_scan.mu);
    return NULL;
}

static int scan_worker_start(void) {
    pthread_mutex_lock(&g_scan.mu);
    if (g_scan.running) {
        pthread_mutex_unlock(&g_scan.mu);
        return 0;
    }
    g_scan.done = 0;
    g_scan.result = -1;
    g_scan.running = 1;
    pthread_mutex_unlock(&g_scan.mu);
    if (pthread_create(&g_scan.thread, NULL, scan_worker_main, NULL) != 0) {
        pthread_mutex_lock(&g_scan.mu);
        g_scan.running = 0;
        pthread_mutex_unlock(&g_scan.mu);
        return -1;
    }
    return 1;
}

/* Returns 1 once for a completed scan and joins its worker. */
static int scan_worker_poll(int *result) {
    int done;
    pthread_mutex_lock(&g_scan.mu);
    done = g_scan.running && g_scan.done;
    if (done && result) *result = g_scan.result;
    pthread_mutex_unlock(&g_scan.mu);
    if (!done) return 0;
    pthread_join(g_scan.thread, NULL);
    pthread_mutex_lock(&g_scan.mu);
    g_scan.running = 0;
    g_scan.done = 0;
    pthread_mutex_unlock(&g_scan.mu);
    return 1;
}

static void scan_worker_join(void) {
    pthread_mutex_lock(&g_scan.mu);
    int running = g_scan.running;
    pthread_mutex_unlock(&g_scan.mu);
    if (!running) return;
    pthread_join(g_scan.thread, NULL);
    pthread_mutex_lock(&g_scan.mu);
    g_scan.running = 0;
    g_scan.done = 0;
    pthread_mutex_unlock(&g_scan.mu);
}

static int handle_home_touch(ui_state_t *ui, int x, int y);
static int handle_list_touch(ui_state_t *ui, int x, int y);
static int handle_detail_touch(ui_state_t *ui, int x, int y);
static int handle_now_playing_touch(ui_state_t *ui, int x, int y);
static int handle_bookmarks_touch(ui_state_t *ui, int x, int y);
static int handle_chapters_touch(ui_state_t *ui, int x, int y);
static int handle_bookmarks_longpress(ui_state_t *ui, int x, int y);

/* Now Playing scrub-drag handlers (finger-down on the handle / finger-move
 * while dragging). Other screens don't use them. */
static int handle_now_playing_down(ui_state_t *ui, int x, int y);
static int handle_now_playing_move(ui_state_t *ui, int x, int y);

/* Long-press dispatcher (defined with the other event handlers below). */
static int ui_handle_longpress(ui_state_t *ui, int x, int y);

static void navigate_to(ui_state_t *ui, ui_screen_t screen,
                        list_mode_t mode, int book_id);
static int navigate_to_folder(ui_state_t *ui, const char *folder_path);
static void navigate_back(ui_state_t *ui);

/* ---- Render-cache rebuild (event thread) --------------------------------
 * Each rebuild does its DB I/O OUTSIDE g_cache_lock, builds a fresh heap
 * buffer, then swaps it under the lock and frees the old buffer. The render
 * thread reads the cache under the same lock during its draw. */
static void rebuild_home(ui_state_t *ui);
static void rebuild_list(ui_state_t *ui);
static void rebuild_current_book(ui_state_t *ui, int with_cover);
static void rebuild_cur_prog(ui_state_t *ui);
static void rebuild_bookmarks(ui_state_t *ui);
static void rebuild_chapters(ui_state_t *ui);
static void rebuild_screen(ui_state_t *ui);
static void free_render_cache(ui_state_t *ui);

/* Collector callbacks defined later (Bookmarks/Chapters sections) —
 * forward-declared so the rebuild block can reference them. */
static int bm_collect_cb(const audiobook_bookmark_t *bm, void *ctx);
static int ch_collect_cb(const audiobook_chapter_t *ch, void *ctx);

/* ---- Thumbnail pre-warm failed-set -------------------------------------- *
 * cover_thumb_prewarm fails permanently for some covers (a progressive JPEG
 * bails at read_header; missing/non-JPEG covers bail too). Without this set,
 * draw_list would re-target the SAME failing book every tick (it's always the
 * "first uncached visible"), starving every book below it of pre-warm. We mark
 * a failed book_id here so draw_list skips it and the walk advances. */

static void thumb_failed_clear(ui_state_t *ui) {
    ui->thumb_failed_n = 0;
}

static int thumb_is_failed(ui_state_t *ui, int book_id) {
    for (int i = 0; i < ui->thumb_failed_n; i++)
        if (ui->thumb_failed[i] == book_id) return 1;
    return 0;
}

static void thumb_mark_failed(ui_state_t *ui, int book_id) {
    if (thumb_is_failed(ui, book_id)) return;
    if (ui->thumb_failed_n < (int)(sizeof(ui->thumb_failed) / sizeof(int)))
        ui->thumb_failed[ui->thumb_failed_n++] = book_id;
}

/* ---- Utility ------------------------------------------------------------ */

/* Persistent log fd — avoids per-call open/close on /tmp (was causing fs stalls).
 * Exported from hook.c; -1 means "not yet opened" or "needs retry after error". */
extern int get_log_fd(void);

static void ui_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    /* Write to persistent hook log fd (open-once with O_APPEND). */
    int fd = get_log_fd();
    if (fd >= 0) { write(fd, buf, strlen(buf)); }
    else {
        /* Fallback: the fd failed — retry open and write. Rare, transient. */
        fd = open("/tmp/.audiobook_hook.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { write(fd, buf, strlen(buf)); close(fd); }
    }
    va_end(ap);
}

/* ---- Time helper ------------------------------------------------------- */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void show_volume_overlay(ui_state_t *ui) {
    int pct = player_volume();
    if (pct < 0) return;
    if (pct > 100) pct = 100;
    ui->volume_overlay_pct = pct;
    ui->volume_overlay_until_ms = now_ms() + 1500;
}

static void volume_key_event(ui_state_t *ui, int dir, int value) {
    uint64_t now = now_ms();
    if (value == 0) {
        if (ui->volume_hold_dir == dir) ui->volume_hold_dir = 0;
        return;
    }
    if (ui->volume_hold_dir == dir) return; /* duplicate down/repeat event */

    player_volume_step(dir);
    show_volume_overlay(ui);
    ui->volume_hold_dir = dir;
    ui->volume_hold_started_ms = now;
    ui->volume_hold_next_ms = now + 400;
}

static void volume_hold_tick(ui_state_t *ui, uint64_t now) {
    if (ui->volume_hold_dir == 0 || now < ui->volume_hold_next_ms) return;
    /* Prevent a lost key-up event from ramping forever. Ten seconds is still
     * long enough to traverse the complete fine-step volume range. */
    if (now - ui->volume_hold_started_ms >= 10000) {
        ui->volume_hold_dir = 0;
        return;
    }
    player_volume_step(ui->volume_hold_dir);
    show_volume_overlay(ui);
    ui->volume_hold_next_ms = now + 120;
}

/* ---- Input handling ---------------------------------------------------- */

/* Find one of hiby_player's already-open fds for a given input device path by
 * scanning /proc/self/fd readlinks, then dup() it as a non-blocking fd. We do
 * this because hiby_player EVIOCGRABs the key devices exclusively, so opening
 * a fresh fd gets no events. But we're the same process: hiby_player's grabbed
 * fd is in our fd table, and since we've blocked hiby_player's main thread in
 * ui_run, that fd sits unread with key events (power/volume) queued on it. A
 * dup shares the same open file description (and its exclusive grab + queued
 * events), so we can drain it. Returns the dup'd non-blocking fd, or -1. */
static int dup_hiby_input_fd(const char *devpath) {
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    int out = -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char link[128];
        char proc[64];
        snprintf(proc, sizeof(proc), "/proc/self/fd/%s", de->d_name);
        ssize_t n = readlink(proc, link, sizeof(link) - 1);
        if (n <= 0) continue;
        link[n] = '\0';
        if (strcmp(link, devpath) != 0) continue;
        int srcfd = atoi(de->d_name);
        int newfd = dup(srcfd);
        if (newfd < 0) continue;
        out = newfd;
        break;
    }
    closedir(d);
    return out;
}

/* Transfer exclusive ownership of a physical-key device from hiby_player to
 * the audiobook UI. Reading a dup of HiBy's fd lets two readers race for one
 * queue; whichever thread reads first consumes the press. Releasing the stock
 * grab through its dup and immediately grabbing a fresh fd gives each mode one
 * deterministic reader. The retained stock dup restores HiBy's grab on exit. */
static void take_over_key_device(ui_state_t *ui, const char *devpath) {
    if (ui->n_key_fds >= 4) return;

    int slot = ui->n_key_fds++;
    ui->key_fds[slot] = -1;
    ui->key_stock_fds[slot] = -1;
    ui->key_stock_flags[slot] = -1;
    ui->key_exclusive[slot] = 0;

    int stock_fd = dup_hiby_input_fd(devpath);
    int stock_flags = stock_fd >= 0 ? fcntl(stock_fd, F_GETFL, 0) : -1;

    /* Most R1 builds leave these key devices ungrabbed. Prefer the simple,
     * race-free path first: grab our fresh queue directly. EVIOCGRAB makes
     * events exclusive to this fd even while HiBy keeps its own fd open. */
    int own_fd = open(devpath, O_RDONLY | O_NONBLOCK);
    if (own_fd >= 0 && ioctl(own_fd, EVIOCGRAB, 1) == 0) {
        ui->key_fds[slot] = own_fd;
        ui->key_exclusive[slot] = 1;
        if (stock_fd >= 0) close(stock_fd);
        ui_log("[ui] key direct-grab %s own=%d\n", devpath, own_fd);
        return;
    }

    if (stock_fd >= 0 && ioctl(stock_fd, EVIOCGRAB, 0) == 0) {
        if (own_fd < 0) own_fd = open(devpath, O_RDONLY | O_NONBLOCK);
        if (own_fd >= 0 && ioctl(own_fd, EVIOCGRAB, 1) == 0) {
            ui->key_fds[slot] = own_fd;
            ui->key_stock_fds[slot] = stock_fd;
            ui->key_stock_flags[slot] = stock_flags;
            ui->key_exclusive[slot] = 1;
            ui_log("[ui] key takeover %s own=%d stock=%d\n",
                   devpath, own_fd, stock_fd);
            return;
        }
        if (own_fd >= 0) { close(own_fd); own_fd = -1; }
        ioctl(stock_fd, EVIOCGRAB, 1);
    }
    if (own_fd >= 0) { close(own_fd); own_fd = -1; }

    /* Degraded fallback for an unexpected kernel/device state. Preserve the
     * old behavior but restore the original status flags when the UI exits. */
    if (stock_fd >= 0) {
        if (stock_flags >= 0)
            fcntl(stock_fd, F_SETFL, stock_flags | O_NONBLOCK);
        ui->key_fds[slot] = stock_fd;
        ui->key_stock_flags[slot] = stock_flags;
        ui_log("[ui] WARNING: key takeover failed for %s; shared fd=%d\n",
               devpath, stock_fd);
        return;
    }

    own_fd = open(devpath, O_RDONLY | O_NONBLOCK);
    if (own_fd >= 0) {
        ui->key_exclusive[slot] = (ioctl(own_fd, EVIOCGRAB, 1) == 0);
        ui->key_fds[slot] = own_fd;
        ui_log("[ui] key fresh %s fd=%d exclusive=%d\n",
               devpath, own_fd, ui->key_exclusive[slot]);
    } else {
        ui_log("[ui] no key fd for %s: %s\n", devpath, strerror(errno));
    }
}

/* Find the Bluetooth AVRCP input device by scanning /sys/class/input for a
 * device named "<something> (AVRCP)", and return its /dev/input/eventN path in
 * `out` (size out_sz). Returns 1 if found, 0 if none (no BT remote connected).
 * The device is created dynamically by BlueZ when a BT A2DP sink connects, so
 * it may not exist at app start. */
static int find_avrcp_dev(char *out, size_t out_sz) {
    DIR *d = opendir("/sys/class/input");
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char p[128];
        snprintf(p, sizeof(p), "/sys/class/input/%s/device/name", de->d_name);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        char nm[96] = {0};
        ssize_t n = read(fd, nm, sizeof(nm) - 1);
        close(fd);
        if (n <= 0) continue;
        if (n && nm[n-1] == '\n') nm[n-1] = '\0';
        if (strstr(nm, "(AVRCP)")) {
            snprintf(out, out_sz, "/dev/input/%s", de->d_name);
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

/* (Re)open the AVRCP remote fd if not already open and the BT link is up.
 * Idempotent; safe to call from the event loop. */
static void avrcp_open(ui_state_t *ui) {
    if (ui->avrcp_fd >= 0) return;
    char dev[64];
    if (!find_avrcp_dev(dev, sizeof(dev))) return;
    int fd = dup_hiby_input_fd(dev);   /* in case hiby_player grabbed it */
    if (fd < 0) fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        ui->avrcp_fd = fd;
        ui_log("[ui] AVRCP remote fd=%d (%s)\n", fd, dev);
    }
}

static int open_input(ui_state_t *ui) {
    ui->input_fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if (ui->input_fd < 0) {
        ui_log("[ui] WARNING: cannot open /dev/input/event1: %s\n",
               strerror(errno));
    } else {
        /* Grab the touch device exclusively so hiby_player's own fd for
         * event1 doesn't receive (and react to) touch events while we're
         * in audiobook mode. Released in close_input. */
        if (ioctl(ui->input_fd, EVIOCGRAB, 1) < 0) {
            ui_log("[ui] WARNING: EVIOCGRAB failed: %s\n", strerror(errno));
        } else {
            ui_log("[ui] EVIOCGRAB acquired on event1\n");
        }
    }
    /* Open the key devices via hiby_player's grabbed fds. event0=md-gpio-keys,
     * event2=jz adc keyboard, event3=earpods_adc — one of these carries the
     * power key. Fall back to a fresh open() (works only if not grabbed). */
    ui->n_key_fds = 0;
    const char *keydevs[] = {"/dev/input/event0", "/dev/input/event2",
                             "/dev/input/event3"};
    for (int i = 0; i < 3; i++) take_over_key_device(ui, keydevs[i]);
    ui->key_fd = -1;  /* legacy field unused */
    ui->avrcp_fd = -1;
    avrcp_open(ui);   /* BT remote may already be connected at app start */
    return ui->input_fd >= 0 ? 0 : -1;
}

static void close_input(ui_state_t *ui) {
    if (ui->input_fd >= 0) {
        ioctl(ui->input_fd, EVIOCGRAB, 0);  /* release exclusive grab */
        close(ui->input_fd);
    }
    for (int i = 0; i < ui->n_key_fds; i++) {
        if (ui->key_exclusive[i] && ui->key_fds[i] >= 0)
            ioctl(ui->key_fds[i], EVIOCGRAB, 0);
        if (ui->key_stock_fds[i] >= 0) {
            if (ioctl(ui->key_stock_fds[i], EVIOCGRAB, 1) < 0)
                ui_log("[ui] WARNING: restore stock grab fd=%d failed: %s\n",
                       ui->key_stock_fds[i], strerror(errno));
            if (ui->key_stock_flags[i] >= 0)
                fcntl(ui->key_stock_fds[i], F_SETFL, ui->key_stock_flags[i]);
            close(ui->key_stock_fds[i]);
        } else if (!ui->key_exclusive[i] && ui->key_fds[i] >= 0
                   && ui->key_stock_flags[i] >= 0) {
            fcntl(ui->key_fds[i], F_SETFL, ui->key_stock_flags[i]);
        }
        if (ui->key_fds[i] >= 0) close(ui->key_fds[i]);
        ui->key_fds[i] = -1;
        ui->key_stock_fds[i] = -1;
    }
    ui->n_key_fds = 0;
    if (ui->key_fd >= 0) close(ui->key_fd);
    if (ui->avrcp_fd >= 0) { close(ui->avrcp_fd); ui->avrcp_fd = -1; }
}

/* ---- Screen blank (power button) --------------------------------------- */

#define BACKLIGHT_PATH "/sys/class/backlight/backlight_pwm0/brightness"

static int read_brightness(void) {
    int fd = open(BACKLIGHT_PATH, O_RDONLY);
    if (fd < 0) return -1;
    char b[16]; ssize_t n = read(fd, b, sizeof(b) - 1); close(fd);
    if (n <= 0) return -1;
    b[n] = '\0';
    return atoi(b);
}

static void write_brightness(int v) {
    int fd = open(BACKLIGHT_PATH, O_WRONLY);
    if (fd < 0) return;
    char b[16]; int len = snprintf(b, sizeof(b), "%d", v);
    write(fd, b, len);
    close(fd);
}

/* Lightweight blank: backlight off only. We keep panning (touch IC stays
 * alive → double-tap wake works) and the decode thread runs (audiobook plays
 * with the screen dark). Wake on power press or touchscreen double-tap. */
static void set_blanked(ui_state_t *ui, int on, int fb_fd) {
    (void)fb_fd;
    if (on) {
        if (ui->blanked) return;
        ui->saved_brightness = read_brightness();
        if (ui->saved_brightness <= 0) ui->saved_brightness = 50;
        write_brightness(0);
        ui->blanked = 1;
        ui->last_touch_up_ms = 0;
        ui_log("[ui] BLANK on (saved brightness=%d)\n", ui->saved_brightness);
    } else {
        if (!ui->blanked) return;
        write_brightness(ui->saved_brightness ? ui->saved_brightness : 50);
        ui->blanked = 0;
        ui_log("[ui] BLANK off (restored brightness=%d)\n", ui->saved_brightness);
    }
}

/* Sync our blanked flag with the real backlight. The stock system may auto-dim
 * the screen independently; if we don't notice, our blanked flag drifts and
 * the power button toggles the wrong way. */
static void sync_blank_state(ui_state_t *ui, int fb_fd) {
    int bl = read_brightness();
    if (bl < 0) return;  /* can't read — leave state alone */
    if (bl == 0 && !ui->blanked) {
        ui->saved_brightness = 50;
        ui->blanked = 1;
        ui_log("[ui] auto-blank sync (backlight went to 0)\n");
    } else if (bl > 0 && ui->blanked) {
        ui->blanked = 0;
        ui_log("[ui] auto-wake sync (backlight went to %d)\n", bl);
    }
}

/* Try to reopen key fds that are missing or dead. Called periodically from
 * the event loop so a device re-enumeration (e.g. after screen dark) doesn't
 * leave us with stale fds forever. */
static void reopen_key_fds(ui_state_t *ui) {
    const char *keydevs[] = {"/dev/input/event0", "/dev/input/event2",
                             "/dev/input/event3"};
    int have[3] = {0, 0, 0};
    for (int i = 0; i < ui->n_key_fds; i++) {
        if (ui->key_fds[i] < 0) continue;
        /* Probe the fd WITHOUT consuming events. A read() here would swallow
         * the next queued input event — including the very wake-key press that
         * unblanks the screen — leaving key presses silently eaten every 5 s
         * and the screen looking frozen when dark. EVIOCGNAME queries the
         * device name; it returns -1 (ENODEV/EBADF) on a dead or re-enumerated
         * device and never reads the event queue. */
        char namebuf[128];
        if (ioctl(ui->key_fds[i], EVIOCGNAME(sizeof(namebuf) - 1), namebuf) < 0) {
            ui_log("[ui] key fd %d dead (%s), closing\n",
                   ui->key_fds[i], strerror(errno));
            close(ui->key_fds[i]);
            ui->key_fds[i] = -1;
            continue;
        }
        /* Match against known device paths to mark which ones we have. */
        char link[128];
        char proc[64];
        int found = 0;
        snprintf(proc, sizeof(proc), "/proc/self/fd/%d", ui->key_fds[i]);
        ssize_t n = readlink(proc, link, sizeof(link) - 1);
        if (n > 0) {
            link[n] = '\0';
            for (int k = 0; k < 3; k++) {
                if (strcmp(link, keydevs[k]) == 0) { have[k] = 1; found = 1; }
            }
        }
        if (!found) have[0] = have[1] = have[2] = 1; /* unknown — don't add more */
    }
    /* Compact out dead entries */
    int j = 0;
    for (int i = 0; i < ui->n_key_fds; i++) {
        if (ui->key_fds[i] >= 0) ui->key_fds[j++] = ui->key_fds[i];
    }
    ui->n_key_fds = j;
    /* Reopen missing ones */
    for (int k = 0; k < 3; k++) {
        if (have[k]) continue;
        int fd = dup_hiby_input_fd(keydevs[k]);
        const char *src = "hiby-dup";
        if (fd < 0) { fd = open(keydevs[k], O_RDONLY | O_NONBLOCK); src = "fresh"; }
        if (fd >= 0) {
            if (ui->n_key_fds < 4) {
                ui->key_fds[ui->n_key_fds++] = fd;
                ui_log("[ui] key fd REOPEN %s = %d (%s)\n", keydevs[k], fd, src);
            } else {
                close(fd);
            }
        }
    }
}

/* R1 touch screen reports ABS_MT_POSITION_X/Y in the range 0-479/0-799
 * (screen coords). Some kernels report 0-255 for X and 0-255 for Y and
 * need scaling, but on R1 the events are already screen-resolution. */
static int scale_touch(int val, int max) {
    (void)max;
    return val;
}

/* Screens that support vertical drag-scroll. The list/bookmarks/chapters
 * screens draw item rows of LIST_ITEM_H and set ui->scroll_max from their
 * item count; dragging vertically scrolls within [0, scroll_max]. */
static int screen_is_scrollable(ui_state_t *ui) {
    return ui->screen == SCREEN_LIST ||
           ui->screen == SCREEN_BOOKMARKS ||
           ui->screen == SCREEN_CHAPTERS;
}

static int process_touch_event(ui_state_t *ui, struct input_event *ev) {
    if (ev->type == EV_ABS) {
        switch (ev->code) {
            case ABS_MT_POSITION_X:
                ui->touch_x = scale_touch(ev->value, RENDER_FB_W);
                if (!ui->touch_active) {
                    ui->touch_start_x = ui->touch_x;
                } else if (ui->scrub_active) {
                    handle_now_playing_move(ui, ui->touch_x, ui->touch_y);
                }
                break;
            case ABS_MT_POSITION_Y: {
                int newy = scale_touch(ev->value, RENDER_FB_H);
                if (!ui->touch_active) {
                    ui->touch_y = newy;
                    ui->touch_start_y = newy;
                } else if (ui->scrub_active) {
                    ui->touch_y = newy;
                    handle_now_playing_move(ui, ui->touch_x, ui->touch_y);
                } else if (screen_is_scrollable(ui)) {
                    /* Live drag-scroll: move the list with the finger. Clamped
                     * to [0, scroll_max] (set by the screen's draw function).
                     * did_scroll is set so finger-up swallows the event instead
                     * of tap-opening a row or back-swiping. */
                    int dy = newy - ui->touch_y;
                    ui->touch_y = newy;
                    if (dy != 0) {
                        ui->scroll_offset -= dy;
                        if (ui->scroll_offset < 0) ui->scroll_offset = 0;
                        if (ui->scroll_offset > ui->scroll_max)
                            ui->scroll_offset = ui->scroll_max;
                        ui->did_scroll = 1;
                    }
                } else {
                    ui->touch_y = newy;
                }
                break;
            }
            case ABS_PRESSURE:
                /* Pressure > 0 = finger down, 0 = finger up */
                if (ev->value > 0 && !ui->touch_active) {
                    ui->touch_active = 1;
                    ui->touch_start_x = ui->touch_x;
                    ui->touch_start_y = ui->touch_y;
                    ui->touch_down_ms = now_ms();
                    ui->did_scroll = 0;
                    /* Start a scrub if the press lands on the handle (Now
                     * Playing only — seek_bar_y is stale on other screens). */
                    if (ui->screen == SCREEN_NOW_PLAYING &&
                        handle_now_playing_down(ui, ui->touch_x, ui->touch_y))
                        ui_log("[ui] scrub start at (%d, %d)\n",
                               ui->touch_x, ui->touch_y);
                } else if (ev->value == 0 && ui->touch_active) {
                    int dx = ui->touch_x - ui->touch_start_x;
                    int dy = ui->touch_y - ui->touch_start_y;
                    int moved2 = dx * dx + dy * dy;
                    uint64_t held = now_ms() - ui->touch_down_ms;
                    ui->touch_active = 0;
                    /* Commit a scrub-drag: seek to the preview position and
                     * swallow the up event (no tap/swipe). */
                    if (ui->scrub_active) {
                        int64_t target = ui->scrub_preview_ms;
                        ui->scrub_active = 0;
                        ui_log("[ui] scrub seek -> %lld ms\n",
                               (long long)target);
                        player_seek_book_ms(target);
                        return 1;
                    }
                    /* A live drag-scroll already moved the list; swallow the
                     * up so we don't tap-open a row or back-swipe. */
                    if (ui->did_scroll) {
                        ui->did_scroll = 0;
                        return 1;
                    }
                    /* Finger up with minimal movement: tap, or long-press if
                     * held > 600ms (e.g. delete a bookmark). */
                    if (moved2 < 400) { /* ~20px threshold */
                        int tx = ui->touch_start_x;
                        int ty = ui->touch_start_y;
                        if (held > 600) {
                            ui_log("[ui] long-press at (%d, %d) held=%llums\n",
                                   tx, ty, (unsigned long long)held);
                            return ui_handle_longpress(ui, tx, ty);
                        }
                        ui_log("[ui] tap at (%d, %d)\n", tx, ty);
                        return ui_handle_tap(ui, tx, ty);
                    } else {
                        ui_log("[ui] swipe dx=%d dy=%d\n", dx, dy);
                        return ui_handle_swipe(ui, dx, dy);
                    }
                }
                break;
        }
    } else if (ev->type == EV_KEY) {
        /* BTN_TOUCH = 0x14a */
        if (ev->code == 0x14a) {
            if (ev->value && !ui->touch_active) {
                ui->touch_active = 1;
                ui->touch_start_x = ui->touch_x;
                ui->touch_start_y = ui->touch_y;
                ui->touch_down_ms = now_ms();
                ui->did_scroll = 0;
                if (ui->screen == SCREEN_NOW_PLAYING &&
                    handle_now_playing_down(ui, ui->touch_x, ui->touch_y))
                    ui_log("[ui] scrub start at (%d, %d)\n",
                           ui->touch_x, ui->touch_y);
            } else if (!ev->value && ui->touch_active) {
                int dx = ui->touch_x - ui->touch_start_x;
                int dy = ui->touch_y - ui->touch_start_y;
                int moved2 = dx * dx + dy * dy;
                uint64_t held = now_ms() - ui->touch_down_ms;
                ui->touch_active = 0;
                if (ui->scrub_active) {
                    int64_t target = ui->scrub_preview_ms;
                    ui->scrub_active = 0;
                    ui_log("[ui] scrub seek -> %lld ms\n",
                           (long long)target);
                    player_seek_book_ms(target);
                    return 1;
                }
                if (ui->did_scroll) {
                    ui->did_scroll = 0;
                    return 1;
                }
                if (moved2 < 400) {
                    if (held > 600)
                        return ui_handle_longpress(ui, ui->touch_start_x,
                                                   ui->touch_start_y);
                    return ui_handle_tap(ui, ui->touch_start_x,
                                         ui->touch_start_y);
                } else {
                    return ui_handle_swipe(ui, dx, dy);
                }
            }
        }
    }
    return 0;
}

/* ---- Main event loop ---------------------------------------------------- */

/* Global UI state — accessible from the ioctl hook (in the render thread)
 * for drawing. The event loop (main thread) updates this state; the ioctl
 * hook reads it to draw. Simple UI state — stale reads across threads are
 * harmless (worst case: one frame of stale content). */
static ui_state_t g_ui;
static volatile int g_ui_active = 0;

/* Render cache lock. The render/pan thread (hiby_player's render thread OR our
 * event-loop manual pan, both via the ioctl hook → ui_draw_frame) holds this
 * across its whole memory-walk draw of the cached rows; the event thread does
 * all its DB I/O OUTSIDE the lock, then takes it only long enough to swap the
 * cache pointer + free the old buffer. Single, non-nested lock → no deadlock,
 * and the render thread never observes a half-swapped/freed buffer. The event
 * thread NEVER holds this lock across a pan ioctl (which would re-enter the
 * hook → ui_draw_frame → draw → lock → deadlock), so rebuilds are kept short
 * and lock-free outside the swap. */
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* Set by the player thread (save_progress) when the current book's progress
 * changes; polled + cleared by the event loop each tick so it can refresh the
 * cached progress + home counts without the render thread touching the DB. */
volatile int g_progress_dirty = 0;

void ui_draw_frame(uint16_t *buf) {
    if (!g_ui_active || !buf) return;
    /* Temporarily point the renderer at this buffer, clear, and draw. */
    uint16_t *saved_fb = g_ui.rend.fb;
    g_ui.rend.fb = buf;
    render_clear(&g_ui.rend);
    switch (g_ui.screen) {
        case SCREEN_HOME:       draw_home(&g_ui); break;
        case SCREEN_LIST:       draw_list(&g_ui); break;
        case SCREEN_DETAIL:     draw_detail(&g_ui); break;
        case SCREEN_NOW_PLAYING: draw_now_playing(&g_ui); break;
        case SCREEN_BOOKMARKS:  draw_bookmarks(&g_ui); break;
        case SCREEN_CHAPTERS:   draw_chapters(&g_ui); break;
        default:                draw_home(&g_ui); break;
    }
    draw_volume_overlay(&g_ui);
    g_ui.rend.fb = saved_fb;
}

int ui_run(uint16_t *fb, int fb_fd) {
    ui_state_t *ui = &g_ui;
    memset(ui, 0, sizeof(*ui));
    renderer_init(&ui->rend, fb, fb_fd);
    ui->screen = SCREEN_HOME;
    ui->home_selected = 0;
    ui->running = 1;
    ui->input_fd = -1;
    ui->key_fd = -1;

    /* Open library DB */
    if (audiobook_db_open(AUDIOBOOK_DB_PATH, &ui->db) < 0) {
        ui_log("[ui] Failed to open library DB\n");
        return -1;
    }
    ui_log("[ui] DB opened, starting event loop\n");

    /* Build the initial screen's render cache (HOME) before enabling the draw
     * hook, so the first frame the render thread draws has valid data. */
    rebuild_screen(ui);

    /* Start the playback engine (dlopens mpg123 + ALSA on first use). The
     * engine opens its OWN library DB connection — it does NOT share ui->db
     * (THREADSAFE=0: one connection per thread). */
    if (player_init() < 0)
        ui_log("[ui] player_init failed — playback unavailable\n");

    g_ui_active = 1;  /* ioctl hook starts drawing our UI */

    if (open_input(ui) < 0) {
        ui_log("[ui] No input device — will run for 30s then exit\n");
        sleep(30);
        g_ui_active = 0;
        player_shutdown();
        audiobook_db_close(ui->db);
        return 0;
    }

    /* Read the current framebuffer variable screen info so we can drive
     * the pan ourselves. hiby_player's render thread depends on the main
     * thread (which is blocked in hook_b), so it stops panning when we
     * enter. We take over: toggle the yoffset between the two buffers and
     * call FBIOPAN_DISPLAY at ~30fps. Our ioctl hook (in hook.c) draws
     * our UI to the target buffer before the real pan. This keeps the
     * display updating and the touch IC alive. */
    struct fb_var_screeninfo vinfo;
    int can_pan = 0;
    if (fb_fd >= 0) {
        if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
            ui_log("[ui] fb: %dx%d yres_virtual=%d yoffset=%d\n",
                   vinfo.xres, vinfo.yres, vinfo.yres_virtual, vinfo.yoffset);
            can_pan = (vinfo.yres_virtual >= vinfo.yres * 2);
        } else {
            ui_log("[ui] FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        }
    }

    uint64_t last_pan = 0;
    const uint64_t PAN_INTERVAL_MS = 33;  /* ~30fps */

    /* Main event loop — reads touch and drives the pan loop.
     * The ioctl hook (ui_draw_frame in hook.c) draws our UI before each
     * FBIOPAN_DISPLAY. We don't draw here — we just drive the pan.
     * Also reads the key devices (via hiby_player's grabbed fds) for the
     * power button (blank/wake) and back/esc. */
    static int key_log_count = 0;
    uint64_t last_key_reopen = 0;
    uint64_t last_blank_sync = 0;
    uint64_t last_storage_check = 0;
    while (ui->running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        if (ui->input_fd >= 0) {
            FD_SET(ui->input_fd, &rfds);
            if (ui->input_fd > maxfd) maxfd = ui->input_fd;
        }
        for (int i = 0; i < ui->n_key_fds; i++) {
            if (ui->key_fds[i] >= 0) {
                FD_SET(ui->key_fds[i], &rfds);
                if (ui->key_fds[i] > maxfd) maxfd = ui->key_fds[i];
            }
        }
        /* AVRCP remote: (re)open if the BT link came up since last tick, and
         * include it in the select set. */
        if (ui->avrcp_fd < 0) {
            uint64_t now = now_ms();
            if (now >= ui->avrcp_next_open_ms) {
                avrcp_open(ui);
                ui->avrcp_next_open_ms = now + 2000;  /* retry every 2s */
            }
        }
        if (ui->avrcp_fd >= 0) {
            FD_SET(ui->avrcp_fd, &rfds);
            if (ui->avrcp_fd > maxfd) maxfd = ui->avrcp_fd;
        }
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 20000;  /* 20ms — short so we can pan promptly */
        int rv = select(maxfd + 1, &rfds, NULL, NULL, &timeout);
        uint64_t now = now_ms();
        /* Periodically sync blank state and reopen dead key fds */
        if (now - last_blank_sync >= 1000) {
            last_blank_sync = now;
            sync_blank_state(ui, fb_fd);
        }
        if (now - last_key_reopen >= 5000) {
            last_key_reopen = now;
            /* Key ownership is transferred once for the lifetime of ui_run.
             * Re-duplicating descriptors here would recreate the shared-queue
             * race. The R1's built-in key devices do not re-enumerate while
             * the screen is blanked. */
        }
        if (now - last_storage_check >= 30000) {
            last_storage_check = now;
            storage_guard_poll();
        }
        if (rv > 0) {
            if (ui->input_fd >= 0 && FD_ISSET(ui->input_fd, &rfds)) {
                struct input_event ev;
                while (read(ui->input_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ui->blanked) {
                        /* While blanked, don't navigate on taps; watch for a
                         * double-tap (two finger-ups within 350ms) to wake. */
                        int is_up = 0;
                        if (ev.type == EV_ABS && ev.code == ABS_PRESSURE
                            && ev.value == 0) is_up = 1;
                        else if (ev.type == EV_KEY && ev.code == 0x14a
                                 && ev.value == 0) is_up = 1;
                        if (is_up) {
                            uint64_t now = now_ms();
                            if (ui->last_touch_up_ms != 0
                                && (now - ui->last_touch_up_ms) < 350) {
                                ui_log("[ui] double-tap wake\n");
                                set_blanked(ui, 0, fb_fd);
                                ui->last_touch_up_ms = 0;
                            } else {
                                ui->last_touch_up_ms = now;
                            }
                        }
                    } else {
                        process_touch_event(ui, &ev);
                    }
                }
            }
            for (int i = 0; i < ui->n_key_fds; i++) {
                if (ui->key_fds[i] < 0
                    || !FD_ISSET(ui->key_fds[i], &rfds)) continue;
                struct input_event ev;
                ssize_t r;
                while ((r = read(ui->key_fds[i], &ev, sizeof(ev))) == sizeof(ev)) {
                    if (ev.type != EV_KEY) continue;
                    /* Diagnostic: log the first ~40 key events so we can
                     * identify the exact power keycode. */
                    if (key_log_count < 200 && ev.value == 1) {
                        key_log_count++;
                        ui_log("[ui] KEY press code=%d (0x%x) value=%d fd=%d t=%llu\n",
                               ev.code, ev.code, ev.value, ui->key_fds[i],
                               (unsigned long long)now_ms());
                    }
                    /* Any key press (except power, which toggles) wakes the
                     * screen if it is dark — matches stock player behavior. */
                    if (ev.value == 1 && ev.code != KEY_POWER && ui->blanked) {
                        set_blanked(ui, 0, fb_fd);
                    }
                    /* The R1 driver does not reliably emit value=2 repeats.
                     * Track volume down/up ourselves and let the event-loop
                     * timer ramp while held. Consume all volume events here. */
                    if (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN) {
                        volume_key_event(ui,
                            ev.code == KEY_VOLUMEUP ? +1 : -1, ev.value);
                        continue;
                    }
                    if (ev.value == 1) {
                        switch (ev.code) {
                            case KEY_POWER:  /* 116 */
                                set_blanked(ui, !ui->blanked, fb_fd);
                                break;
                            case KEY_BACK:
                            case KEY_ESC:
                                if (!ui->blanked) navigate_back(ui);
                                break;
                            case KEY_PLAYPAUSE:  /* 164 */
                                player_toggle();
                                break;
                            case KEY_NEXT:       /* 163 */
                            case KEY_FASTFORWARD: /* 208 */
                                player_ff();
                                break;
                            case KEY_PREVIOUS:    /* 165 */
                            case KEY_REWIND:       /* 207 */
                                player_rw();
                                break;
                        }
                    }
                }
                if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    ui_log("[ui] key fd %d read err (%s); closing\n",
                           ui->key_fds[i], strerror(errno));
                    close(ui->key_fds[i]);
                    ui->key_fds[i] = -1;
                }
            }
            /* Bluetooth AVRCP remote (BT speaker's play/pause button). The one
             * button alternates KEY_PLAYCD(200)/KEY_PAUSECD(201) per press; we
             * treat both (and the generic KEY_PLAYPAUSE if a remote sends it)
             * as a single toggle, so each press flips play/pause regardless of
             * the remote's internal state guess. */
            if (ui->avrcp_fd >= 0 && FD_ISSET(ui->avrcp_fd, &rfds)) {
                struct input_event ev;
                ssize_t r;
                while ((r = read(ui->avrcp_fd, &ev, sizeof(ev))) == sizeof(ev)) {
                    if (ev.type != EV_KEY || ev.value != 1) continue;
                    ui_log("[ui] AVRCP key code=%d (0x%x)\n", ev.code, ev.code);
                    if (ev.code == KEY_PLAYCD      /* 200 */
                        || ev.code == KEY_PAUSECD  /* 201 */
                        || ev.code == KEY_PLAYPAUSE /* 164 (some remotes) */
                        || ev.code == KEY_PLAY      /* 200 alias */ ) {
                        player_toggle();
                    }
                }
                if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    /* Link gone (BT disconnected) — close so we reopen later. */
                    ui_log("[ui] AVRCP read err (%s); closing\n", strerror(errno));
                    close(ui->avrcp_fd); ui->avrcp_fd = -1;
                }
            }
        }

        /* Drive the pan loop to keep the display + touch IC alive and
         * show our UI. Our ioctl hook draws before the real pan.
         * Keep panning even when blanked (backlight off) so the touch IC
         * stays alive for double-tap wake. */
        now = now_ms();
        volume_hold_tick(ui, now);
        if (can_pan && (now - last_pan) >= PAN_INTERVAL_MS) {
            last_pan = now;
            vinfo.yoffset = (vinfo.yoffset == 0) ? vinfo.yres : 0;
            /* This ioctl goes through our hook → draws UI → real pan */
            ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
        } else if (!can_pan && fb_fd >= 0 && (now - last_pan) >= PAN_INTERVAL_MS) {
            /* Single-buffer: still call FBIOPAN_DISPLAY (no-op pan) so
             * the ioctl hook fires and draws our UI. */
            last_pan = now;
            vinfo.yoffset = 0;
            ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
        }

        /* Pre-warm ONE list thumbnail per tick. draw_list (run inside the pan
         * above) recorded the first uncached visible book_id; we decode it here,
         * OUTSIDE the render hook, so a slow libjpeg decode can't freeze the
         * display (decoding several thumbs in a single frame froze the device). */
        if (ui->thumb_warm_target && !ui->refresh_scanning) {
            int bid = ui->thumb_warm_target;
            ui->thumb_warm_target = 0;
            if (!cover_thumb_prewarm(ui->db, bid))
                thumb_mark_failed(ui, bid);  /* don't retry; let walk advance */
        }

        /* If the player saved progress (g_progress_dirty, set in save_progress),
         * refresh the cached current-book progress + home counts so the UI
         * updates without the render thread touching the DB. Done here on the
         * event thread, outside the pan ioctl and outside the cache lock's draw
         * critical section. The list's per-book progress is intentionally NOT
         * rebuilt here (that would re-run the per-book query storm we just
         * killed); it refreshes on the next navigate/scan. */
        if (g_progress_dirty) {
            g_progress_dirty = 0;
            rebuild_cur_prog(ui);
            rebuild_home(ui);
        }

        int scan_rc = -1;
        if (scan_worker_poll(&scan_rc)) {
            ui->refresh_scanning = 0;
            if (scan_rc < 0)
                ui->refresh_err_until_ms = now_ms() + 3500;
            else
                ui->refresh_msg_until_ms = now_ms() + 2500;
            /* The worker owns its DB connection. Rebuild caches here on the
             * event thread after its transaction has committed. */
            rebuild_home(ui);
            if (ui->screen == SCREEN_LIST) rebuild_list(ui);
            if (ui->screen == SCREEN_DETAIL || ui->screen == SCREEN_NOW_PLAYING)
                rebuild_current_book(ui, 1);
        }
    }

    g_ui_active = 0;  /* stop drawing */
    set_blanked(ui, 0, fb_fd);  /* restore backlight on exit */
    close_input(ui);
    scan_worker_join();
    free_render_cache(ui);  /* free the list/bookmark/chapter caches */
    player_shutdown();   /* stop playback + save progress (uses db) */
    cover_shutdown();   /* free the cached cover art */
    audiobook_db_close(ui->db);
    ui_log("[ui] event loop exited\n");
    return 0;
}

/* ---- Navigation --------------------------------------------------------- */

static int push_nav_state(ui_state_t *ui) {
    if (ui->nav_depth >=
        (int)(sizeof(ui->nav_stack) / sizeof(ui->nav_stack[0]))) {
        ui_log("[ui] navigation refused: stack full at depth=%d\n",
               ui->nav_depth);
        return 0;
    }
    ui->nav_stack[ui->nav_depth] = ui->screen;
    ui->nav_list_mode[ui->nav_depth] = ui->list_mode;
    ui->nav_book_id[ui->nav_depth] = ui->current_book_id;
    strncpy(ui->nav_folder_path[ui->nav_depth], ui->folder_path,
            sizeof(ui->nav_folder_path[0]) - 1);
    ui->nav_folder_path[ui->nav_depth][sizeof(ui->nav_folder_path[0]) - 1] = '\0';
    ui->nav_depth++;
    return 1;
}

static void set_nav_destination(ui_state_t *ui, ui_screen_t screen,
                                list_mode_t mode, int book_id) {
    ui->screen = screen;
    ui->list_mode = mode;
    ui->current_book_id = book_id;
    ui->selected_idx = 0;
    ui->scroll_offset = 0;
    ui->scrub_active = 0;  /* a scrub can't span screen changes */
    ui->thumb_warm_target = 0;  /* don't pre-warm a stale book_id */
    thumb_failed_clear(ui);  /* re-try covers on a fresh screen visit */
}

static void navigate_to(ui_state_t *ui, ui_screen_t screen,
                        list_mode_t mode, int book_id) {
    if (!push_nav_state(ui)) return;
    set_nav_destination(ui, screen, mode, book_id);
    /* Build the entered screen's render cache now (event thread) so the next
     * frame the render thread draws has valid data. For Detail/Now-Playing this
     * also pre-decodes the cover off the render thread. */
    rebuild_screen(ui);
}

/* Folder entry is separate from navigate_to(). Previously navigate_to()
 * rebuilt the parent before folder_path changed, then the caller rebuilt the
 * child. Combined with the touch handler's row query, that scanned the full
 * catalog three times and paused framebuffer panning during every folder tap. */
static int navigate_to_folder(ui_state_t *ui, const char *folder_path) {
    if (!folder_path || !folder_path[0]) return 0;
    size_t path_len = strlen(folder_path);
    if (path_len >= sizeof(ui->folder_path)) {
        ui_log("[ui] folder path refused: %lu bytes (max %lu)\n",
               (unsigned long)path_len,
               (unsigned long)(sizeof(ui->folder_path) - 1));
        return 0;
    }
    if (!push_nav_state(ui)) return 0;

    set_nav_destination(ui, SCREEN_LIST, LIST_FOLDERS, 0);
    memcpy(ui->folder_path, folder_path, path_len + 1);

    uint64_t started = now_ms();
    rebuild_list(ui);
    ui_log("[ui] folder entered in %llums path='%s'\n",
           (unsigned long long)(now_ms() - started), ui->folder_path);
    return 1;
}

static void navigate_back(ui_state_t *ui) {
    if (ui->nav_depth > 0) {
        ui->nav_depth--;
        ui->screen = ui->nav_stack[ui->nav_depth];
        ui->list_mode = ui->nav_list_mode[ui->nav_depth];
        ui->current_book_id = ui->nav_book_id[ui->nav_depth];
        strncpy(ui->folder_path, ui->nav_folder_path[ui->nav_depth],
                sizeof(ui->folder_path) - 1);
        ui->folder_path[sizeof(ui->folder_path) - 1] = '\0';
        ui->selected_idx = 0;
        ui->scroll_offset = 0;
        /* Restore rendered the screen we came from; rebuild its cache. */
        rebuild_screen(ui);
    } else {
        if (ui->refresh_scanning) return;
        /* No more back — exit to launcher */
        ui->running = 0;
    }
}

/* ---- Event handlers (forward-declared in ui.h) ------------------------- */

int ui_handle_tap(ui_state_t *ui, int x, int y) {
    switch (ui->screen) {
        case SCREEN_HOME:       return handle_home_touch(ui, x, y);
        case SCREEN_LIST:       return handle_list_touch(ui, x, y);
        case SCREEN_DETAIL:     return handle_detail_touch(ui, x, y);
        case SCREEN_NOW_PLAYING: return handle_now_playing_touch(ui, x, y);
        case SCREEN_BOOKMARKS: return handle_bookmarks_touch(ui, x, y);
        case SCREEN_CHAPTERS:  return handle_chapters_touch(ui, x, y);
        default: return 0;
    }
}

/* Long-press (finger held > ~600ms with minimal movement). Used on the
 * Bookmarks screen to delete a bookmark (tap jumps to it instead). */
static int ui_handle_longpress(ui_state_t *ui, int x, int y) {
    switch (ui->screen) {
        case SCREEN_BOOKMARKS: return handle_bookmarks_longpress(ui, x, y);
        default: return 0;
    }
}

int ui_handle_swipe(ui_state_t *ui, int dx, int dy) {
    /* Vertical scroll is handled by live drag-scroll in process_touch_event
     * (the list follows the finger); a vertical gesture never reaches here
     * because did_scroll swallows the finger-up. So this function only
     * handles horizontal swipes (back / jump-to-Now-Playing). */
    (void)dy;
    /* Right-to-left swipe (finger moves left, dx negative) = jump to Now
     * Playing, mirroring the original device's gesture. Only when a book is
     * loaded and we're not already on Now Playing (avoids pushing a duplicate
     * nav-stack entry). Works from any screen. */
    if (dx < -80 && ui->screen != SCREEN_NOW_PLAYING) {
        int bid = player_current_book();
        if (bid > 0) {
            ui->current_book_id = bid;
            navigate_to(ui, SCREEN_NOW_PLAYING, ui->list_mode, bid);
            return 1;
        }
    }
    /* Left-to-right swipe = back navigation */
    if (dx > 80) {
        navigate_back(ui);
        return 1;
    }
    return 0;
}

/* Compact system-style HUD for physical volume feedback. Drawn last so it is
 * visible on every audiobook screen, including lists and Now Playing. */
static void draw_volume_overlay(ui_state_t *ui) {
    if (ui->volume_overlay_until_ms == 0 ||
        now_ms() >= ui->volume_overlay_until_ms)
        return;

    int pct = ui->volume_overlay_pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    const int x = 56, y = 326, w = 368, h = 124;
    char label[32];
    snprintf(label, sizeof(label), "Volume %d%%", pct);
    render_fill_rect(&ui->rend, x, y, w, h, COL_BLACK);
    render_draw_rect(&ui->rend, x, y, w, h, COL_GRAY_LT);
    render_text_centered(&ui->rend, x, y + 14, w, label,
                         FONT_SCALE_2, COL_WHITE);
    render_progress_bar(&ui->rend, x + 24, y + 84, w - 48, 16,
                        (double)pct / 100.0, COL_ACCENT, COL_GRAY_DK);
}

/* ---- Screen drawing: Home ---------------------------------------------- */

static void draw_home(ui_state_t *ui) {
    renderer_t *r = &ui->rend;

    /* Title: white text floating on black (no filled bar), gray divider below
     * — matches the system launcher header. */
    render_text(r, 18, 16, "Audiobooks", FONT_SCALE_2, COL_WHITE);
    render_draw_hline(r, 0, TITLE_BAR_H - 1, RENDER_FB_W, COL_DIVIDER);

    /* Cached home counts (built by rebuild_home on the event thread). Read
     * under the cache lock; stale-by-one-frame is harmless. */
    int home_cont, home_fin, home_total;
    pthread_mutex_lock(&g_cache_lock);
    home_cont = ui->home_continue_n;
    home_fin = ui->home_finished_n;
    home_total = ui->home_total_n;
    pthread_mutex_unlock(&g_cache_lock);

    /* Items */
    int y = TITLE_BAR_H;
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        int is_selected = (i == ui->home_selected);
        if (is_selected)
            render_fill_rect(r, 0, y, 4, HOME_ITEM_H, COL_ACCENT); /* focus bar */
        render_text(r, 24, y + 22, home_labels[i], FONT_SCALE_2, COL_WHITE);

        /* Show continue count */
        if (i == HOME_CONTINUE) {
            int count = home_cont;
            if (count > 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", count);
                render_text_right(r, RENDER_FB_W - 24, y + 22, buf,
                                  FONT_SCALE_2, COL_GRAY_LT);
            }
        } else if (i == HOME_FINISHED) {
            int count = home_fin;
            if (count > 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", count);
                render_text_right(r, RENDER_FB_W - 24, y + 22, buf,
                                  FONT_SCALE_2, COL_GRAY_LT);
            }
        }

        y += HOME_ITEM_H;
        render_draw_hline(r, 0, y - 1, RENDER_FB_W, COL_DIVIDER);
    }

    /* "Library refreshed" confirmation flash (set in handle_home_touch after a
     * Refresh). Shows in green just above the footer for ~2.5s. */
    if (ui->refresh_scanning) {
        render_text_centered(r, 0, RENDER_FB_H - FOOTER_H - 36, RENDER_FB_W,
                             "Refreshing library...", FONT_SCALE_2, COL_WHITE);
    } else if (ui->refresh_msg_until_ms && now_ms() < ui->refresh_msg_until_ms) {
        render_text_centered(r, 0, RENDER_FB_H - FOOTER_H - 36, RENDER_FB_W,
                             "Library refreshed", FONT_SCALE_2, COL_GREEN);
    }
    if (ui->refresh_err_until_ms && now_ms() < ui->refresh_err_until_ms) {
        render_text_centered(r, 0, RENDER_FB_H - FOOTER_H - 36, RENDER_FB_W,
                             "Scan failed: storage full", FONT_SCALE_2, COL_RED);
    }

    /* Footer */
    char footer[64];
    snprintf(footer, sizeof(footer), "%d books in library", home_total);
    render_text_centered(r, 0, RENDER_FB_H - FOOTER_H + 10, RENDER_FB_W,
                        footer, FONT_SCALE_1, COL_GRAY_LT);
}

static int handle_home_touch(ui_state_t *ui, int x, int y) {
    if (y < TITLE_BAR_H) return 0;

    int item_y = TITLE_BAR_H;
    int idx = (y - item_y) / HOME_ITEM_H;
    if (idx < 0 || idx >= HOME_ITEM_COUNT) return 0;

    ui->home_selected = idx;

    switch (idx) {
        case HOME_CONTINUE:
            navigate_to(ui, SCREEN_LIST, LIST_CONTINUE, 0);
            break;
        case HOME_TITLES:
            navigate_to(ui, SCREEN_LIST, LIST_TITLES, 0);
            break;
        case HOME_AUTHORS:
            navigate_to(ui, SCREEN_LIST, LIST_AUTHORS, 0);
            break;
        case HOME_SERIES:
            navigate_to(ui, SCREEN_LIST, LIST_SERIES, 0);
            break;
        case HOME_FOLDERS:
            ui->folder_path[0] = '\0';   /* Folders tile always starts at root */
            navigate_to(ui, SCREEN_LIST, LIST_FOLDERS, 0);
            break;
        case HOME_FINISHED:
            navigate_to(ui, SCREEN_LIST, LIST_FINISHED, 0);
            break;
        case HOME_REFRESH: {
            int started = scan_worker_start();
            if (started > 0) {
                ui->refresh_scanning = 1;
                ui->refresh_msg_until_ms = 0;
                ui->refresh_err_until_ms = 0;
            } else if (started < 0) {
                ui->refresh_err_until_ms = now_ms() + 3500;
            }
            break;
        }
        case HOME_BACK:
            if (!ui->refresh_scanning) ui->running = 0;
            break;
    }
    return 1;
}

/* ---- Screen drawing: List ---------------------------------------------- */

/* list_item_t is defined in ui.h (shared with the render cache). */
typedef struct {
    list_item_t *items;
    int count;
    int capacity;
    sqlite3 *db;
} list_ctx_t;

static int list_collect_cb(const audiobook_book_t *b, void *ctx) {
    list_ctx_t *lc = (list_ctx_t *)ctx;
    if (lc->count >= lc->capacity) {
        int new_cap = lc->capacity * 2;
        list_item_t *ni = realloc(lc->items, new_cap * sizeof(list_item_t));
        if (!ni) return 1;
        lc->items = ni;
        lc->capacity = new_cap;
    }
    list_item_t *item = &lc->items[lc->count++];
    item->book_id = b->book_id;
    item->is_folder = 0;
    strncpy(item->title, b->title, sizeof(item->title) - 1);
    item->title[sizeof(item->title) - 1] = '\0';
    strncpy(item->author, b->author, sizeof(item->author) - 1);
    item->author[sizeof(item->author) - 1] = '\0';
    item->duration_ms = b->total_duration_ms;
    item->completed = b->completed;

    audiobook_progress_t p;
    item->has_progress = (audiobook_get_progress(lc->db, b->book_id, &p) > 0);
    item->elapsed_ms = item->has_progress ? p.total_book_elapsed_ms : 0;
    return 0;
}

/* Collect book rows for the current list_mode into lc. */
static void collect_list_books(ui_state_t *ui, list_ctx_t *lc) {
    switch (ui->list_mode) {
        case LIST_CONTINUE:
            audiobook_list_continue(ui->db, list_collect_cb, lc); break;
        case LIST_FINISHED:
            audiobook_list_finished(ui->db, list_collect_cb, lc); break;
        case LIST_AUTHOR_BOOKS:
            audiobook_list_books_by_author(ui->db, ui->list_filter,
                                           list_collect_cb, lc); break;
        case LIST_SERIES_BOOKS:
            audiobook_list_books_by_series(ui->db, ui->list_filter,
                                           list_collect_cb, lc); break;
        case LIST_TITLES:
            audiobook_list_books(ui->db, list_collect_cb, lc); break;
        case LIST_FOLDERS: {
            /* Drill-down folder hierarchy. ui->folder_path ("") is the current
             * level; list every book whose root_path == folder_path, plus one
             * row per distinct immediate subfolder (sorted), folders first.
             * Tapping a folder row descends; Back ascends. */
            const char *prefix = ui->folder_path[0] ? ui->folder_path
                                                    : AUDIOBOOK_LIBRARY_ROOT;
            size_t plen = strlen(prefix);
            /* Heap-allocate the 32 KB folder-name table (was a 128*256 stack
             * frame). This runs on the event thread now (Stage 2), but heap +
             * NULL-check keeps it bounded and avoids a large stack frame. */
            char (*folder_names)[256] = malloc((size_t)128 * 256);
            int folder_count = 0;
            int hidden_folder_count = 0;
            int long_segment_count = 0;
            if (!folder_names) break;   /* OOM: leave this view empty */

            sqlite3_stmt *stmt = NULL;
            char child_lower[514];
            char child_upper[514];
            int lower_len = snprintf(child_lower, sizeof(child_lower),
                                     "%s/", prefix);
            int upper_len = snprintf(child_upper, sizeof(child_upper),
                                     "%s0", prefix);
            if (lower_len < 0 || lower_len >= (int)sizeof(child_lower) ||
                upper_len < 0 || upper_len >= (int)sizeof(child_upper)) {
                ui_log("[ui] FOLDERS prefix too long for child range: '%s'\n",
                       prefix);
                free(folder_names);
                break;
            }
            /* NOTE: the books table has NO `author` column — author is a FK
             * (author_id) into the separate `authors` table. Selecting a bare
             * `author` here made sqlite3_prepare_v2 fail with "no such column:
             * author", so the row loop never ran and Folders showed nothing
             * (folders=0 books=0). Join authors like every other list query
             * (SQL_LIST_BOOKS) does. Column indices are unchanged: 0=book_id,
             * 1=title, 2=author display name, 3=total_duration_ms, 4=completed,
             * 5=root_path. */
            const char *sql =
                "SELECT b.book_id, b.title, COALESCE(a.display_name,''), "
                "b.total_duration_ms, b.completed, b.root_path "
                "FROM books b LEFT JOIN authors a ON a.author_id=b.author_id "
                "WHERE b.root_path=?1 OR "
                "(b.root_path>=?2 AND b.root_path<?3) "
                "ORDER BY b.root_path, b.sort_title";
            if (sqlite3_prepare_v2(ui->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, child_lower, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, child_upper, -1, SQLITE_TRANSIENT);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    int bid = sqlite3_column_int(stmt, 0);
                    const char *title = (const char *)sqlite3_column_text(stmt, 1);
                    const char *author = (const char *)sqlite3_column_text(stmt, 2);
                    int64_t dur = sqlite3_column_int64(stmt, 3);
                    int completed = sqlite3_column_int(stmt, 4);
                    const char *rpath = (const char *)sqlite3_column_text(stmt, 5);
                    if (!rpath) continue;

                    if (strcmp(rpath, prefix) == 0) {
                        /* Book directly at this level. */
                        if (lc->count >= lc->capacity) {
                            int nc = lc->capacity ? lc->capacity * 2 : 64;
                            list_item_t *ni = realloc(lc->items, nc * sizeof(list_item_t));
                            if (!ni) break;
                            lc->items = ni;
                            lc->capacity = nc;
                        }
                        list_item_t *it = &lc->items[lc->count++];
                        it->book_id = bid;
                        it->is_folder = 0;
                        strncpy(it->title, title ? title : "?", sizeof(it->title) - 1);
                        it->title[sizeof(it->title) - 1] = '\0';
                        strncpy(it->author, author ? author : "", sizeof(it->author) - 1);
                        it->author[sizeof(it->author) - 1] = '\0';
                        it->duration_ms = dur;
                        it->completed = completed;
                        audiobook_progress_t p;
                        it->has_progress = (audiobook_get_progress(lc->db, bid, &p) > 0);
                        it->elapsed_ms = it->has_progress ? p.total_book_elapsed_ms : 0;
                    } else if (strncmp(rpath, prefix, plen) == 0 && rpath[plen] == '/') {
                        /* A book in a subfolder: record the immediate child name. */
                        const char *rest = rpath + plen + 1;
                        const char *end = strchr(rest, '/');
                        int seg_len = end ? (int)(end - rest) : (int)strlen(rest);
                        if (seg_len <= 0) continue;
                        if (seg_len >= 256) {
                            long_segment_count++;
                            continue;
                        }

                        int found = 0;
                        for (int i = 0; i < folder_count; i++) {
                            if (strncmp(folder_names[i], rest, seg_len) == 0
                                && folder_names[i][seg_len] == '\0') {
                                found = 1;
                                break;
                            }
                        }
                        if (!found && folder_count < 128) {
                            strncpy(folder_names[folder_count], rest, seg_len);
                            folder_names[folder_count][seg_len] = '\0';
                            folder_count++;
                        } else if (!found) {
                            hidden_folder_count++;
                        }
                    }
                }
                sqlite3_finalize(stmt);
            }
            ui_log("[ui] FOLDERS prefix='%s' folders=%d books=%d "
                   "hidden=%d long_segments=%d\n",
                   prefix, folder_count, lc->count, hidden_folder_count,
                   long_segment_count);

            /* Sort folders alphabetically (case-insensitive). */
            for (int i = 0; i < folder_count - 1; i++) {
                for (int j = i + 1; j < folder_count; j++) {
                    if (strcasecmp(folder_names[i], folder_names[j]) > 0) {
                        char tmp[256];
                        memcpy(tmp, folder_names[i], 256);
                        memcpy(folder_names[i], folder_names[j], 256);
                        memcpy(folder_names[j], tmp, 256);
                    }
                }
            }

            /* Prepend folder rows before the book rows already collected. */
            if (folder_count > 0 && lc->items) {
                int total = folder_count + lc->count;
                if (total > lc->capacity) {
                    int nc = total;
                    list_item_t *ni = realloc(lc->items, nc * sizeof(list_item_t));
                    if (ni) {
                        lc->items = ni;
                        lc->capacity = nc;
                    }
                }
                if (lc->capacity >= total) {
                    memmove(&lc->items[folder_count], lc->items,
                            lc->count * sizeof(list_item_t));
                    for (int i = 0; i < folder_count; i++) {
                        list_item_t *it = &lc->items[i];
                        it->book_id = 0;
                        it->is_folder = 1;
                        strncpy(it->title, folder_names[i], sizeof(it->title) - 1);
                        it->title[sizeof(it->title) - 1] = '\0';
                        it->author[0] = '\0';
                        it->duration_ms = 0;
                        it->completed = 0;
                        it->has_progress = 0;
                        it->elapsed_ms = 0;
                    }
                    lc->count = total;
                }
            }
            free(folder_names);
            break;
        }
        default:
            audiobook_list_books(ui->db, list_collect_cb, lc); break;
    }
}

/* String-list collector for Authors/Series. */
typedef struct {
    char **names;
    int count;
    int capacity;
} strlist_ctx_t;

static int strlist_collect_cb(const char *name, void *ctx) {
    strlist_ctx_t *sc = (strlist_ctx_t *)ctx;
    if (!name || !name[0]) return 0;
    if (sc->count >= sc->capacity) {
        int nc = sc->capacity ? sc->capacity * 2 : 64;
        char **nn = realloc(sc->names, nc * sizeof(char *));
        if (!nn) return 1;
        sc->names = nn;
        sc->capacity = nc;
    }
    char *dup = strdup(name);
    if (!dup) return 1;
    sc->names[sc->count++] = dup;
    return 0;
}

static void free_strlist(strlist_ctx_t *sc) {
    for (int i = 0; i < sc->count; i++) free(sc->names[i]);
    free(sc->names);
    sc->names = NULL;
    sc->count = sc->capacity = 0;
}

/* ---- Render-cache rebuild (event thread) --------------------------------
 * Each rebuild does its DB I/O OUTSIDE g_cache_lock, builds a fresh heap
 * buffer, then swaps the cached pointer under the lock and frees the OLD
 * buffer OUTSIDE the lock (the render thread, which reads under the same lock,
 * only ever sees the new pointer, so freeing the old one can't race it). */

static void rebuild_home(ui_state_t *ui) {
    int cont = audiobook_list_continue(ui->db, NULL, NULL);
    int fin = audiobook_list_finished(ui->db, NULL, NULL);
    int total = audiobook_list_books(ui->db, NULL, NULL);
    pthread_mutex_lock(&g_cache_lock);
    ui->home_continue_n = cont;
    ui->home_finished_n = fin;
    ui->home_total_n = total;
    pthread_mutex_unlock(&g_cache_lock);
}

static void rebuild_list(ui_state_t *ui) {
    int is_str = (ui->list_mode == LIST_AUTHORS || ui->list_mode == LIST_SERIES);
    if (is_str) {
        strlist_ctx_t sc;
        memset(&sc, 0, sizeof(sc));
        sc.capacity = 64;
        sc.names = calloc(sc.capacity, sizeof(char *));
        if (sc.names) {
            if (ui->list_mode == LIST_AUTHORS)
                audiobook_list_authors(ui->db, strlist_collect_cb, &sc);
            else
                audiobook_list_series(ui->db, strlist_collect_cb, &sc);
        }
        list_item_t *old_items = NULL;
        char **old_names = NULL;
        int old_nc = 0;
        pthread_mutex_lock(&g_cache_lock);
        old_items = ui->list_items;
        ui->list_items = NULL; ui->list_count = ui->list_cap = 0;
        old_names = ui->strlist; old_nc = ui->strlist_count;
        ui->strlist = sc.names; ui->strlist_count = sc.count;
        ui->strlist_cap = sc.capacity;
        ui->list_is_strlist = 1;
        pthread_mutex_unlock(&g_cache_lock);
        free(old_items);
        for (int i = 0; i < old_nc; i++) free(old_names[i]);
        free(old_names);
    } else {
        list_ctx_t lc;
        memset(&lc, 0, sizeof(lc));
        lc.capacity = 64;
        lc.items = calloc(lc.capacity, sizeof(list_item_t));
        lc.db = ui->db;
        if (lc.items) collect_list_books(ui, &lc);
        list_item_t *old_items = NULL;
        char **old_names = NULL;
        int old_nc = 0;
        pthread_mutex_lock(&g_cache_lock);
        old_items = ui->list_items;
        old_names = ui->strlist; old_nc = ui->strlist_count;
        ui->strlist = NULL; ui->strlist_count = ui->strlist_cap = 0;
        ui->list_items = lc.items; ui->list_count = lc.count;
        ui->list_cap = lc.capacity;
        ui->list_is_strlist = 0;
        pthread_mutex_unlock(&g_cache_lock);
        free(old_items);
        for (int i = 0; i < old_nc; i++) free(old_names[i]);
        free(old_names);
    }
}

/* Rebuild the current-book cache (Detail / Now-Playing). with_cover=1 does
 * the (slow, event-thread) cover decode + copy into cur_cover_buf; pass 0 for
 * a progress-only refresh (leaves the cover untouched). */
static void rebuild_current_book(ui_state_t *ui, int with_cover) {
    audiobook_book_t b;
    audiobook_progress_t p;
    char description[2048];
    int bok = 0, pok = 0;
    const uint16_t *cov = NULL;
    memset(&b, 0, sizeof(b));
    memset(&p, 0, sizeof(p));
    description[0] = '\0';
    if (ui->current_book_id > 0) {
        if (audiobook_get_book(ui->db, ui->current_book_id, &b) > 0) bok = 1;
        if (audiobook_get_progress(ui->db, ui->current_book_id, &p) > 0) pok = 1;
        if (with_cover && bok) {
            audiobook_get_book_description(ui->db, ui->current_book_id,
                                           description,
                                           sizeof(description));
            cov = cover_get(ui->db, ui->current_book_id);  /* event-thread decode */
        }
    }
    pthread_mutex_lock(&g_cache_lock);
    ui->cur_book = b; ui->cur_book_ok = bok;
    ui->cur_prog = p; ui->cur_prog_ok = pok;
    if (with_cover) {
        strncpy(ui->cur_description, description,
                sizeof(ui->cur_description) - 1);
        ui->cur_description[sizeof(ui->cur_description) - 1] = '\0';
        if (cov) {
            memcpy(ui->cur_cover_buf, cov, sizeof(ui->cur_cover_buf));
            ui->cur_cover_ok = 1;
        } else {
            ui->cur_cover_ok = 0;
        }
    }
    pthread_mutex_unlock(&g_cache_lock);
}

/* Progress-only refresh of the current-book cache (used on g_progress_dirty
 * ticks — the cover and book metadata don't change, so skip the decode). */
static void rebuild_cur_prog(ui_state_t *ui) {
    audiobook_progress_t p;
    int pok = 0;
    memset(&p, 0, sizeof(p));
    if (ui->current_book_id > 0)
        pok = (audiobook_get_progress(ui->db, ui->current_book_id, &p) > 0);
    pthread_mutex_lock(&g_cache_lock);
    ui->cur_prog = p; ui->cur_prog_ok = pok;
    pthread_mutex_unlock(&g_cache_lock);
}

static void rebuild_bookmarks(ui_state_t *ui) {
    bm_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.capacity = 64;
    c.rows = calloc(c.capacity, sizeof(bookmark_row_t));
    if (c.rows)
        audiobook_list_bookmarks(ui->db, ui->current_book_id, bm_collect_cb, &c);
    bookmark_row_t *old = NULL;
    pthread_mutex_lock(&g_cache_lock);
    old = ui->bm_rows;
    ui->bm_rows = c.rows; ui->bm_count = c.count; ui->bm_cap = c.capacity;
    pthread_mutex_unlock(&g_cache_lock);
    free(old);
}

static void rebuild_chapters(ui_state_t *ui) {
    ch_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.capacity = 64;
    c.rows = calloc(c.capacity, sizeof(chapter_row_t));
    if (c.rows)
        audiobook_get_chapters(ui->db, ui->current_book_id, ch_collect_cb, &c);

    /* Metadata tools often put the distinguishing "pt01" suffix at the END of
     * each filename/title, beyond the R1 screen's truncation point. If a book
     * spans multiple physical tracks, prefix every row with its track's part
     * number. A single-file M4B has only one track and keeps its chapter names
     * unchanged. Rows are already ordered by disc/track/chapter. */
    int track_count = c.count > 0 ? 1 : 0;
    for (int i = 1; i < c.count; i++)
        if (c.rows[i].track_id != c.rows[i - 1].track_id) track_count++;
    int part = 1;
    for (int i = 0; i < c.count; i++) {
        if (i > 0 && c.rows[i].track_id != c.rows[i - 1].track_id) part++;
        if (track_count > 1) {
            char original[256];
            strncpy(original, c.rows[i].title, sizeof(original) - 1);
            original[sizeof(original) - 1] = '\0';
            snprintf(c.rows[i].title, sizeof(c.rows[i].title),
                     "Part %d - %.235s", part, original);
        }
    }
    chapter_row_t *old = NULL;
    pthread_mutex_lock(&g_cache_lock);
    old = ui->ch_rows;
    ui->ch_rows = c.rows; ui->ch_count = c.count; ui->ch_cap = c.capacity;
    pthread_mutex_unlock(&g_cache_lock);
    free(old);
}

static void rebuild_screen(ui_state_t *ui) {
    switch (ui->screen) {
        case SCREEN_HOME:        rebuild_home(ui); break;
        case SCREEN_LIST:        rebuild_list(ui); break;
        case SCREEN_DETAIL:      rebuild_current_book(ui, 1); break;
        case SCREEN_NOW_PLAYING: rebuild_current_book(ui, 1); break;
        case SCREEN_BOOKMARKS:   rebuild_bookmarks(ui); break;
        case SCREEN_CHAPTERS:    rebuild_chapters(ui); break;
    }
}

static void free_render_cache(ui_state_t *ui) {
    pthread_mutex_lock(&g_cache_lock);
    free(ui->list_items); ui->list_items = NULL;
    ui->list_count = ui->list_cap = 0;
    for (int i = 0; i < ui->strlist_count; i++) free(ui->strlist[i]);
    free(ui->strlist); ui->strlist = NULL;
    ui->strlist_count = ui->strlist_cap = 0;
    free(ui->bm_rows); ui->bm_rows = NULL; ui->bm_count = ui->bm_cap = 0;
    free(ui->ch_rows); ui->ch_rows = NULL; ui->ch_count = ui->ch_cap = 0;
    ui->list_is_strlist = 0;
    ui->cur_book_ok = 0; ui->cur_prog_ok = 0; ui->cur_cover_ok = 0;
    pthread_mutex_unlock(&g_cache_lock);
}

static void draw_list(ui_state_t *ui) {
    renderer_t *r = &ui->rend;
    /* Clear each frame; the book-row loop records the first uncached visible
     * book_id for the event loop to pre-warm (one decode per tick). */
    ui->thumb_warm_target = 0;
    const char *title = "List";
    switch (ui->list_mode) {
        case LIST_TITLES:        title = "Titles"; break;
        case LIST_AUTHORS:       title = "Authors"; break;
        case LIST_SERIES:        title = "Series"; break;
        case LIST_FOLDERS:       title = "Folders"; break;
        case LIST_FINISHED:      title = "Finished"; break;
        case LIST_CONTINUE:      title = "Continue"; break;
        case LIST_AUTHOR_BOOKS:  title = ui->list_filter; break;
        case LIST_SERIES_BOOKS:  title = ui->list_filter; break;
    }

    /* Header: "Back" left + title right (system style). Back is always
     * top-left on every screen so the user doesn't have to hunt for it. */
    render_text(r, 18, 16, "Back", FONT_SCALE_1, COL_GRAY_LT);
    render_text_right(r, RENDER_FB_W - 18, 16, title, FONT_SCALE_2, COL_WHITE);
    render_draw_hline(r, 0, TITLE_BAR_H - 1, RENDER_FB_W, COL_DIVIDER);

    /* Draw from the render cache (built by rebuild_list on the event thread).
     * Hold the cache lock across the whole iteration so the event thread's
     * swap-and-free can't race us. Branch on the cache's own flag (not
     * ui->list_mode) so we always render exactly what the cache holds, even
     * across a one-frame mode change. No DB I/O happens here. */
    pthread_mutex_lock(&g_cache_lock);

    if (ui->list_is_strlist) {
        char **names = ui->strlist;
        int count = ui->strlist_count;

        ui->scroll_max = count * LIST_ITEM_H - LIST_VIEWPORT_H;
        if (ui->scroll_max < 0) ui->scroll_max = 0;
        if (ui->scroll_offset > ui->scroll_max) ui->scroll_offset = ui->scroll_max;

        int y = TITLE_BAR_H - ui->scroll_offset;
        if (count == 0) {
            render_text_centered(r, 0, RENDER_FB_H / 2, RENDER_FB_W,
                                "None found", FONT_SCALE_2, COL_GRAY_LT);
        }
        for (int i = 0; i < count; i++) {
            int item_top = y;
            if (item_top + LIST_ITEM_H < TITLE_BAR_H) { y += LIST_ITEM_H; continue; }
            if (item_top >= RENDER_FB_H - FOOTER_H) break;
            if (i == ui->selected_idx)
                render_fill_rect(r, 0, item_top, 4, LIST_ITEM_H, COL_ACCENT);
            char name_buf[256];
            strncpy(name_buf, names[i], sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';
            while (render_text_width(name_buf, FONT_SCALE_2) > RENDER_FB_W - 48 &&
                   strlen(name_buf) > 1)
                name_buf[strlen(name_buf) - 1] = '\0';
            render_text(r, 24, item_top + 30, name_buf, FONT_SCALE_2, COL_WHITE);
            y += LIST_ITEM_H;
            if (y < RENDER_FB_H - FOOTER_H)
                render_draw_hline(r, 0, y - 1, RENDER_FB_W, COL_DIVIDER);
        }

        char footer[64];
        snprintf(footer, sizeof(footer), "%d items", count);
        render_draw_hline(r, 0, RENDER_FB_H - FOOTER_H, RENDER_FB_W, COL_DIVIDER);
        render_text_centered(r, 0, RENDER_FB_H - FOOTER_H + 10, RENDER_FB_W,
                            footer, FONT_SCALE_1, COL_GRAY_LT);
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }

    /* Book rows (Titles / Folders / Finished / Continue / author- or series-
     * filtered). Read from the cache; the lock is still held from above. */
    list_item_t *items = ui->list_items;
    int count = ui->list_count;

    ui->scroll_max = count * LIST_ITEM_H - LIST_VIEWPORT_H;
    if (ui->scroll_max < 0) ui->scroll_max = 0;
    if (ui->scroll_offset > ui->scroll_max) ui->scroll_offset = ui->scroll_max;

    /* Draw items */
    int y = TITLE_BAR_H - ui->scroll_offset;

    for (int i = 0; i < count; i++) {
        int item_top = y;
        if (item_top + LIST_ITEM_H < TITLE_BAR_H) { y += LIST_ITEM_H; continue; }
        if (item_top >= RENDER_FB_H - FOOTER_H) break;

        int is_selected = (i == ui->selected_idx);
        if (is_selected)
            render_fill_rect(r, 0, item_top, 4, LIST_ITEM_H, COL_ACCENT);

        if (items[i].is_folder) {
            /* Folder row: name only, vertically centered, truncated to fit. */
            char name_buf[256];
            strncpy(name_buf, items[i].title, sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';
            while (render_text_width(name_buf, FONT_SCALE_2) > RENDER_FB_W - 48 &&
                   strlen(name_buf) > 1)
                name_buf[strlen(name_buf) - 1] = '\0';
            render_text(r, 24, item_top + 30, name_buf, FONT_SCALE_2, COL_WHITE);
        } else {
        /* Cover thumbnail (small, left). When present, the text column shifts
         * right; when absent, the existing full-width layout is used. We ONLY
         * read the cache here (cover_thumb_cached) — never decode — so the
         * render/pan hook can't block on a libjpeg decode. The event loop
         * pre-warms one uncached thumbnail per tick (cover_thumb_prewarm). */
        int bid = items[i].book_id;
        const uint16_t *thumb = cover_thumb_cached(bid);
        if (!thumb && ui->thumb_warm_target == 0 && !thumb_is_failed(ui, bid))
            ui->thumb_warm_target = bid;   /* ask the event loop to decode this */
        int text_x = 24;
        int right_margin = 18;
        if (thumb) {
            int ts = COVER_THUMB_PX;
            int tx = 12;
            int ty = item_top + (LIST_ITEM_H - ts) / 2;
            render_blit_rgb565(r, tx, ty, ts, ts, thumb, ts, ts);
            render_draw_rect(r, tx, ty, ts, ts, COL_GRAY_DK);
            text_x = tx + ts + 12;   /* 80 */
        }
        int avail_w = RENDER_FB_W - text_x - right_margin;

        /* Title (truncate to fit, reserving room for "Done" if completed) */
        int title_max_w = avail_w;
        if (items[i].completed)
            title_max_w -= render_text_width("Done", FONT_SCALE_1) + 16;
        char title_buf[256];
        strncpy(title_buf, items[i].title, sizeof(title_buf) - 1);
        title_buf[sizeof(title_buf) - 1] = '\0';
        while (render_text_width(title_buf, FONT_SCALE_2) > title_max_w &&
               strlen(title_buf) > 1) {
            title_buf[strlen(title_buf) - 1] = '\0';
        }
        /* If we truncated, strip trailing spaces and add ellipsis */
        if (strcmp(title_buf, items[i].title) != 0) {
            int len = (int)strlen(title_buf);
            while (len > 0 && title_buf[len - 1] == ' ') { title_buf[--len] = '\0'; }
            if (len > 3) { title_buf[len - 3] = '.'; title_buf[len - 2] = '.';
                            title_buf[len - 1] = '.'; }
        }
        render_text(r, text_x, item_top + 10, title_buf, FONT_SCALE_2, COL_WHITE);

        /* Second line: author + duration */
        char line2[256];
        char dur[32];
        int total_sec = (int)(items[i].duration_ms / 1000);
        int h = total_sec / 3600;
        int m = (total_sec % 3600) / 60;
        if (h > 0) snprintf(dur, sizeof(dur), "%dh %dm", h, m);
        else snprintf(dur, sizeof(dur), "%dm", m);

        if (items[i].has_progress && items[i].elapsed_ms > 0) {
            int done_sec = (int)(items[i].elapsed_ms / 1000);
            int dh = done_sec / 3600;
            int dm = (done_sec % 3600) / 60;
            char done[32];
            if (dh > 0) snprintf(done, sizeof(done), "%dh %dm", dh, dm);
            else snprintf(done, sizeof(done), "%dm", dm);
            snprintf(line2, sizeof(line2), "%s / %s", done, dur);
        } else {
            snprintf(line2, sizeof(line2), "%s", dur);
        }

        if (items[i].author[0]) {
            strncat(line2, "  -  ", sizeof(line2) - strlen(line2) - 1);
            strncat(line2, items[i].author, sizeof(line2) - strlen(line2) - 1);
        }

        render_text(r, text_x, item_top + 62, line2, FONT_SCALE_1, COL_GRAY_LT);

        /* Progress bar */
        if (items[i].has_progress && items[i].duration_ms > 0) {
            double frac = (double)items[i].elapsed_ms /
                          items[i].duration_ms;
            render_progress_bar(r, text_x, item_top + LIST_ITEM_H - 12,
                               avail_w, 4, frac,
                               COL_ACCENT, COL_GRAY_DK);
        }

        if (items[i].completed) {
            render_text_right(r, RENDER_FB_W - 18, item_top + 10, "Done",
                             FONT_SCALE_1, COL_GREEN);
        }
        }   /* end else (book row) */

        y += LIST_ITEM_H;
        if (y < RENDER_FB_H - FOOTER_H)
            render_draw_hline(r, 0, y - 1, RENDER_FB_W, COL_DIVIDER);
    }

    /* Footer */
    char footer[64];
    if (ui->list_mode == LIST_FOLDERS)
        snprintf(footer, sizeof(footer), "%d items", count);
    else
        snprintf(footer, sizeof(footer), "%d books", count);
    render_draw_hline(r, 0, RENDER_FB_H - FOOTER_H, RENDER_FB_W, COL_DIVIDER);
    render_text_centered(r, 0, RENDER_FB_H - FOOTER_H + 10, RENDER_FB_W,
                        footer, FONT_SCALE_1, COL_GRAY_LT);

    pthread_mutex_unlock(&g_cache_lock);
}

static int handle_list_touch(ui_state_t *ui, int x, int y) {
    (void)x;
    /* Tap on title bar = back */
    if (y < TITLE_BAR_H) {
        navigate_back(ui);
        return 1;
    }
    /* Tap on footer = nothing */
    if (y >= RENDER_FB_H - FOOTER_H) return 0;

    int item_y = TITLE_BAR_H - ui->scroll_offset;
    int idx = (y - item_y) / LIST_ITEM_H;
    if (idx < 0) return 0;

    /* Authors/Series: tap a name → filtered book list. */
    if (ui->list_mode == LIST_AUTHORS || ui->list_mode == LIST_SERIES) {
        char selected_name[256] = "";
        pthread_mutex_lock(&g_cache_lock);
        if (ui->list_is_strlist && idx < ui->strlist_count &&
            ui->strlist[idx]) {
            strncpy(selected_name, ui->strlist[idx],
                    sizeof(selected_name) - 1);
            selected_name[sizeof(selected_name) - 1] = '\0';
        }
        pthread_mutex_unlock(&g_cache_lock);
        if (selected_name[0]) {
            strncpy(ui->list_filter, selected_name,
                    sizeof(ui->list_filter) - 1);
            ui->list_filter[sizeof(ui->list_filter) - 1] = '\0';
            navigate_to(ui, SCREEN_LIST,
                        ui->list_mode == LIST_AUTHORS ? LIST_AUTHOR_BOOKS
                                                       : LIST_SERIES_BOOKS,
                        0);
        }
        return 1;
    }

    /* Use the exact book/folder row already cached for rendering. */
    list_item_t selected;
    int have_selected = 0;
    memset(&selected, 0, sizeof(selected));
    pthread_mutex_lock(&g_cache_lock);
    if (!ui->list_is_strlist && idx < ui->list_count) {
        selected = ui->list_items[idx];
        have_selected = 1;
    }
    pthread_mutex_unlock(&g_cache_lock);
    if (have_selected) {
        if (selected.is_folder) {
            /* Descend while preserving the current folder for Back. */
            const char *prefix = ui->folder_path[0] ? ui->folder_path
                                                    : AUDIOBOOK_LIBRARY_ROOT;
            char new_path[512];
            int written = snprintf(new_path, sizeof(new_path), "%s/%s",
                                   prefix, selected.title);
            if (written < 0 || written >= (int)sizeof(new_path)) {
                ui_log("[ui] folder path too long: prefix='%s' child='%s'\n",
                       prefix, selected.title);
                return 1;
            }
            navigate_to_folder(ui, new_path);
        } else {
            navigate_to(ui, SCREEN_DETAIL, ui->list_mode, selected.book_id);
        }
    }
    return 1;
}

/* ---- Screen drawing: Detail -------------------------------------------- */

/* Draw cover art at (x,y) sized size x size, with a 1px light border so dark
 * covers stay defined against the black background. cov is the COVER_PX cache;
 * the blit scales it to size (downscale = crisp, upscale = blocky, so prefer
 * size <= COVER_PX). No-op if cov is NULL (caller skips the gap). */
static void draw_cover_bordered(renderer_t *r, int x, int y, int size,
                                const uint16_t *cov) {
    if (!cov) return;
    render_blit_rgb565(r, x, y, size, size, cov, COVER_PX, COVER_PX);
    render_draw_rect(r, x, y, size, size, COL_GRAY_LT);
}

static void draw_detail(ui_state_t *ui) {
    renderer_t *r = &ui->rend;

    /* Read the current-book cache (built by rebuild_current_book on the event
     * thread). Hold the cache lock across the whole draw — the cover blit
     * reads cur_cover_buf, which the event thread overwrites under this same
     * lock, so it can't race a re-decode. No DB I/O here. */
    pthread_mutex_lock(&g_cache_lock);
    audiobook_book_t b = ui->cur_book;
    int book_ok = ui->cur_book_ok;
    int prog_ok = ui->cur_prog_ok;
    audiobook_progress_t p = ui->cur_prog;
    int has_cover = ui->cur_cover_ok;

    if (!book_ok) {
        pthread_mutex_unlock(&g_cache_lock);
        render_text(r, 16, 100, "Book not found", FONT_SCALE_2, COL_WHITE);
        return;
    }

    /* Header: "Back" on black + divider (system style) */
    render_text(r, 18, 16, "Back", FONT_SCALE_1, COL_GRAY_LT);
    render_draw_hline(r, 0, TITLE_BAR_H - 1, RENDER_FB_W, COL_DIVIDER);

    /* Cover art on the left (bordered, 200px). When a cover is present the
     * title/author/duration form a right-hand column beside it; with no cover
     * the title falls back to full width (the original layout). */
    const int DETAIL_COVER = 200;
    const int cover_top = TITLE_BAR_H + 8;            /* 84 */
    int text_x = 16, text_w = RENDER_FB_W - 32;
    int cover_bottom = 0;
    if (has_cover) {
        draw_cover_bordered(r, 16, cover_top, DETAIL_COVER, ui->cur_cover_buf);
        text_x = 16 + DETAIL_COVER + 16;              /* 232 */
        text_w = RENDER_FB_W - text_x - 16;           /* 232 */
        cover_bottom = cover_top + DETAIL_COVER;      /* 284 */
    }

    /* Title + author + duration (right column if cover, else full width). */
    int y = cover_top + 4;
    int title_scale = has_cover ? FONT_SCALE_2 : FONT_SCALE_3;
    y = render_text_wrap(r, text_x, y, text_w, 3, b.title, title_scale,
                         COL_WHITE);
    y += 10;
    if (b.author[0]) {
        render_text(r, text_x, y, b.author, FONT_SCALE_1, COL_GRAY_LT);
        y += 32;
    }
    render_time(r, text_x, y, b.total_duration_ms, FONT_SCALE_1, COL_GRAY_LT);
    y += 36;

    /* Publisher summary fills the space between the cover/header and the
     * bottom-anchored progress block. It was normalized during scanning, so
     * drawing stays allocation-free. Long summaries stop at a complete line
     * before the progress region. */
    int desc_y = y + 10;
    if (has_cover && desc_y < cover_bottom + 14) desc_y = cover_bottom + 14;
    int desc_bottom = DETAIL_PROGRESS_LABEL_Y - 2;
    if (ui->cur_description[0] && desc_y + 30 < desc_bottom) {
        render_text(r, 16, desc_y, "Description", FONT_SCALE_1, COL_GRAY_LT);
        desc_y += render_text_line_height(FONT_SCALE_1);
        int line_h = render_text_line_height(FONT_SCALE_4);
        int max_lines = (desc_bottom - desc_y) / line_h;
        if (max_lines > 0)
            render_text_wrap(r, 16, desc_y, RENDER_FB_W - 32, max_lines,
                             ui->cur_description, FONT_SCALE_4, COL_WHITE);
    }

    /* Progress stays immediately above the controls. A book with no saved
     * progress row displays a consistent zero state. */
    int64_t elapsed_ms = prog_ok ? p.total_book_elapsed_ms : 0;
    render_text(r, 16, DETAIL_PROGRESS_LABEL_Y, "Progress:",
                FONT_SCALE_1, COL_GRAY_LT);
    render_time(r, 16, DETAIL_PROGRESS_TIME_Y, elapsed_ms,
                FONT_SCALE_2, COL_WHITE);
    if (b.total_duration_ms > 0) {
        double frac = (double)elapsed_ms / b.total_duration_ms;
        render_progress_bar(r, 16, DETAIL_PROGRESS_BAR_Y,
                            RENDER_FB_W - 32, 10, frac,
                            COL_ACCENT, COL_GRAY_DK);
    }

    /* Buttons (2x2 grid, bottom-anchored). */
    int btn_y = DETAIL_BTN_ROW1_Y;
    int btn_w = (RENDER_FB_W - 48) / 2;

    /* Row 1: Play, Chapters */
    render_fill_rect(r, 16, btn_y, btn_w, 64, COL_GREEN);
    render_text_centered(r, 16, btn_y + 18, btn_w, "Play", FONT_SCALE_4,
                         COL_BLACK);
    render_fill_rect(r, 32 + btn_w, btn_y, btn_w, 64, COL_GRAY);
    render_text_centered(r, 32 + btn_w, btn_y + 18, btn_w, "Chapters",
                         FONT_SCALE_4, COL_WHITE);
    btn_y = DETAIL_BTN_ROW2_Y;

    /* Row 2: Bookmarks, Menu */
    render_fill_rect(r, 16, btn_y, btn_w, 64, COL_GRAY);
    render_text_centered(r, 16, btn_y + 18, btn_w, "Bookmarks", FONT_SCALE_4,
                         COL_WHITE);
    render_fill_rect(r, 32 + btn_w, btn_y, btn_w, 64, COL_GRAY_DK);
    render_text_centered(r, 32 + btn_w, btn_y + 18, btn_w, "Menu",
                         FONT_SCALE_4, COL_WHITE);

    pthread_mutex_unlock(&g_cache_lock);
}

static int handle_detail_touch(ui_state_t *ui, int x, int y) {
    if (y < TITLE_BAR_H) {
        navigate_back(ui);
        return 1;
    }

    int btn_y = DETAIL_BTN_ROW1_Y;
    int btn_w = (RENDER_FB_W - 48) / 2;

    /* Row 1: Play, Chapters */
    if (y >= btn_y && y < btn_y + 64) {
        if (x < 16 + btn_w) {
            player_play_book(ui->current_book_id, 1);  /* resume from progress */
            navigate_to(ui, SCREEN_NOW_PLAYING, ui->list_mode,
                        ui->current_book_id);
            return 1;
        } else if (x >= 32 + btn_w) {
            navigate_to(ui, SCREEN_CHAPTERS, ui->list_mode,
                        ui->current_book_id);
            return 1;
        }
    }
    btn_y = DETAIL_BTN_ROW2_Y;

    /* Row 2: Bookmarks, Menu */
    if (y >= btn_y && y < btn_y + 64) {
        if (x < 16 + btn_w) {
            navigate_to(ui, SCREEN_BOOKMARKS, ui->list_mode,
                        ui->current_book_id);
            return 1;
        } else if (x >= 32 + btn_w) {
            ui->running = 0;
            return 1;
        }
    }
    return 0;
}

/* ---- Screen drawing: Now Playing --------------------------------------- */

static void draw_now_playing(ui_state_t *ui) {
    renderer_t *r = &ui->rend;

    /* Read the current-book cache (built by rebuild_current_book on the event
     * thread). Hold the lock across the whole draw — the cover blit reads
     * cur_cover_buf. Live playback state (player_position_ms etc.) is
     * lock-free and safe to read under the lock. No DB I/O here. */
    pthread_mutex_lock(&g_cache_lock);
    audiobook_book_t b = ui->cur_book;
    int book_ok = ui->cur_book_ok;
    int prog_ok = ui->cur_prog_ok;
    audiobook_progress_t prog = ui->cur_prog;
    int has_cover = ui->cur_cover_ok;

    if (!book_ok) {
        pthread_mutex_unlock(&g_cache_lock);
        render_text(r, 16, 100, "Book not found", FONT_SCALE_2, COL_WHITE);
        return;
    }

    /* Header: Back / Now Playing on black + divider (system style) */
    render_text(r, 18, 16, "Back", FONT_SCALE_1, COL_GRAY_LT);
    render_text_right(r, RENDER_FB_W - 18, 16, "Now Playing", FONT_SCALE_2,
                      COL_WHITE);
    render_draw_hline(r, 0, TITLE_BAR_H - 1, RENDER_FB_W, COL_DIVIDER);

    int y = TITLE_BAR_H + 8;
    ui->seek_bar_y = -1;   /* no scrub target unless we draw the bar below */

    /* Cover art: cached RGB565 (COVER_PX square), centered, with a 1px border.
     * Blitted from cur_cover_buf (event-thread pre-decoded). If the book has no
     * cover or the decode failed, has_cover is 0 and we skip with no gap — the
     * existing layout follows. */
    if (has_cover) {
        int cx = (RENDER_FB_W - COVER_PX) / 2;
        draw_cover_bordered(r, cx, y, COVER_PX, ui->cur_cover_buf);
        y += COVER_PX + 10;
    }

    /* Book title (wrapped, hero) */
    y = render_text_wrap(r, 16, y, RENDER_FB_W - 32, 2,
                         b.title, FONT_SCALE_2, COL_WHITE);
    y += 10;

    /* Author */
    if (b.author[0]) {
        render_text(r, 16, y, b.author, FONT_SCALE_1, COL_GRAY_LT);
        y += 32;
    }

    player_snapshot_t snap;
    player_get_snapshot(&snap);

    /* If this book's format isn't playable yet, say so. */
    if (snap.book_id == ui->current_book_id &&
        snap.format_unsupported) {
        render_text(r, 16, y, "M4B playback coming soon", FONT_SCALE_2,
                    COL_ORANGE);
        y += 50;
    }
    if (snap.book_id == ui->current_book_id && snap.media_missing) {
        render_text(r, 16, y, "SD card unavailable", FONT_SCALE_2, COL_RED);
        y += 50;
    }

    /* Live position from the engine (falls back to saved progress if idle). */
    int64_t pos_ms = 0, total_ms = b.total_duration_ms;
    player_state_t pst = snap.state;
    int engine_live = (snap.book_id == ui->current_book_id &&
                       snap.state != PLAYER_STOPPED);
    if (engine_live) {
        pos_ms = snap.position_ms;
        total_ms = snap.total_ms ? snap.total_ms : total_ms;
    } else if (prog_ok) {
        pos_ms = prog.total_book_elapsed_ms;
    }

    /* While scrubbing, show the finger's target position live (the handle
     * tracks the finger); the actual seek commits on finger-up. */
    if (ui->scrub_active && ui->scrub_total_ms > 0) {
        pos_ms = ui->scrub_preview_ms;
        total_ms = ui->scrub_total_ms;
    }

    render_text(r, 16, y, "Position:", FONT_SCALE_1, COL_GRAY_LT);
    y += 30;
    render_time(r, 16, y, pos_ms, FONT_SCALE_3, COL_WHITE);
    y += 70;

    /* Progress bar with a draggable handle at the current position. Pressing
     * the handle (handle_now_playing_down) starts a scrub; the handle follows
     * the finger and the seek commits on release. */
    if (total_ms > 0) {
        double frac = (double)pos_ms / (double)total_ms;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        ui->seek_bar_y = y;   /* record for handle_now_playing_down hit-test */
        render_progress_bar(r, 16, y, RENDER_FB_W - 32, 14, frac,
                           COL_ACCENT, COL_GRAY_DK);
        /* Draggable handle: white ring + accent core, centered on the bar. */
        int bar_w = RENDER_FB_W - 32;
        int hx = 16 + (int)(frac * (double)bar_w);
        int hy = y + 7;   /* bar is 14px tall → vertical center */
        render_fill_circle(r, hx, hy, 8, COL_WHITE);
        render_fill_circle(r, hx, hy, 6, ui->scrub_active ? COL_ORANGE
                                                          : COL_ACCENT);
        y += 18;

        /* Time labels */
        render_time(r, 16, y, pos_ms, FONT_SCALE_1, COL_GRAY_LT);
        {
            char dur_buf[32];
            int tsec = (int)(total_ms / 1000);
            int th = tsec / 3600, tm = (tsec % 3600) / 60;
            if (th > 0) snprintf(dur_buf, sizeof(dur_buf), "%d:%02d:%02d", th, tm, tsec % 60);
            else snprintf(dur_buf, sizeof(dur_buf), "%d:%02d", tm, tsec % 60);
            render_text_right(r, RENDER_FB_W - 18, y, dur_buf,
                             FONT_SCALE_1, COL_GRAY_LT);
        }
        y += 30;
    }

    /* Playing / Paused indicator */
    const char *ind = (pst == PLAYER_PLAYING) ? "Playing" :
                      (pst == PLAYER_PAUSED)  ? "Paused"  : "Stopped";
    uint16_t ind_col = (pst == PLAYER_PLAYING) ? COL_GREEN : COL_GRAY_LT;
    render_text(r, 16, y, ind, FONT_SCALE_1, ind_col);

    /* Playback controls */
    int ctrl_y = RENDER_FB_H - 150;
    int ctrl_w = 120;
    int gap = (RENDER_FB_W - 3 * ctrl_w) / 4;   /* 30 */

    /* Prev = rewind 30s */
    render_fill_rect(r, gap, ctrl_y, ctrl_w, 60, COL_GRAY);
    render_text_centered(r, gap, ctrl_y + 16, ctrl_w, "-30s",
                         FONT_SCALE_4, COL_WHITE);

    /* Play/Pause — label + color reflect state */
    int playing = (pst == PLAYER_PLAYING);
    render_fill_rect(r, gap * 2 + ctrl_w, ctrl_y, ctrl_w, 60,
                     playing ? COL_ORANGE : COL_GREEN);
    render_text_centered(r, gap * 2 + ctrl_w, ctrl_y + 16, ctrl_w,
                         playing ? "Pause" : "Play", FONT_SCALE_4, COL_BLACK);

    /* Next = fast-forward 60s */
    render_fill_rect(r, gap * 3 + 2 * ctrl_w, ctrl_y, ctrl_w, 60, COL_GRAY);
    render_text_centered(r, gap * 3 + 2 * ctrl_w, ctrl_y + 16, ctrl_w,
                         "+60s", FONT_SCALE_4, COL_WHITE);

    /* Row 2 (4 buttons, own tighter spacing than row 1): Mark / Sleep / Speed /
     * Chaps. Mark adds a bookmark (flashes green); Sleep cycles Off/15/30/60 min
     * with a live countdown when armed; Speed cycles
     * 1.0/1.1/1.25/1.5/2.0x; Chaps opens the chapter list. */
    ctrl_y += 72;
    int g2 = 16, w2 = 100;
    int b0 = g2, b1 = g2 * 2 + w2, b2 = g2 * 3 + 2 * w2, b3 = g2 * 4 + 3 * w2;

    int mark_flash = (now_ms() < ui->mark_flash_until_ms);
    render_fill_rect(r, b0, ctrl_y, w2, 52,
                     mark_flash ? COL_GREEN : COL_GRAY_DK);
    render_text_centered(r, b0, ctrl_y + 12, w2, "Mark",
                         FONT_SCALE_4, mark_flash ? COL_BLACK : COL_WHITE);

    int64_t srem = player_sleep_remaining_ms();
    int sleep_armed = (srem >= 0);
    render_fill_rect(r, b1, ctrl_y, w2, 52,
                     sleep_armed ? COL_ORANGE : COL_GRAY_DK);
    {
        char sbuf[16];
        if (sleep_armed) {
            int sec = (int)(srem / 1000);
            snprintf(sbuf, sizeof(sbuf), "%d:%02d", sec / 60, sec % 60);
        } else {
            snprintf(sbuf, sizeof(sbuf), "Sleep");
        }
        render_text_centered(r, b1, ctrl_y + 12, w2, sbuf,
                             FONT_SCALE_4, sleep_armed ? COL_BLACK : COL_WHITE);
    }

    /* Speed: cycle 1.0/1.1/1.25/1.5/2.0x. Highlight when not 1.0x so the user sees
     * it's active. WSOLA time-stretch — tempo changes, pitch preserved. */
    int spd = player_get_speed();
    int spd_on = (spd != 1000);
    render_fill_rect(r, b2, ctrl_y, w2, 52,
                     spd_on ? COL_ORANGE : COL_GRAY_DK);
    {
        char sbuf[8];
        if (spd % 100 == 0) snprintf(sbuf, sizeof(sbuf), "%.1fx", spd / 1000.0);
        else snprintf(sbuf, sizeof(sbuf), "%.2fx", spd / 1000.0);
        render_text_centered(r, b2, ctrl_y + 12, w2, sbuf,
                             FONT_SCALE_4, spd_on ? COL_BLACK : COL_WHITE);
    }

    render_fill_rect(r, b3, ctrl_y, w2, 52, COL_GRAY_DK);
    render_text_centered(r, b3, ctrl_y + 12, w2, "Chaps",
                         FONT_SCALE_4, COL_WHITE);

    pthread_mutex_unlock(&g_cache_lock);
}

/* ---- Now Playing scrub-drag (handle press / drag) --------------------- */

/* Resolve the current book's live position + total duration for display and
 * handle hit-testing (mirrors draw_now_playing's logic). Returns 1 if the book
 * is loaded, 0 otherwise. */
static int now_playing_pos_total(ui_state_t *ui, int64_t *pos_ms,
                                 int64_t *total_ms) {
    /* Event-thread only (scrub handlers): reads the render cache directly — no
     * lock needed since rebuilds also run on the event thread (never concurrent
     * with this), and the render thread only reads. */
    *pos_ms = 0; *total_ms = 0;
    if (!ui->cur_book_ok) return 0;
    *total_ms = ui->cur_book.total_duration_ms;
    player_snapshot_t snap;
    player_get_snapshot(&snap);
    int engine_live = (snap.book_id == ui->current_book_id &&
                       snap.state != PLAYER_STOPPED);
    if (engine_live) {
        *pos_ms = snap.position_ms;
        if (snap.total_ms) *total_ms = snap.total_ms;
    } else if (ui->cur_prog_ok) {
        *pos_ms = ui->cur_prog.total_book_elapsed_ms;
    }
    return 1;
}

/* Finger-down: start a scrub ONLY if the press lands on the handle (a generous
 * radius around the current-position circle, within the bar's vertical band).
 * Pressing elsewhere on the bar does not seek — this is what prevents the
 * accidental progress changes the old tap-the-bar behavior caused. While
 * dragging, the handle jumps to the finger and preview tracks it. */
static int handle_now_playing_down(ui_state_t *ui, int x, int y) {
    if (ui->seek_bar_y < 0) return 0;  /* no bar drawn (unknown duration) */
    if (y < ui->seek_bar_y - 16 || y > ui->seek_bar_y + 28) return 0;
    int64_t pos_ms = 0, total_ms = 0;
    if (!now_playing_pos_total(ui, &pos_ms, &total_ms) || total_ms <= 0)
        return 0;
    int bar_x = 16, bar_w = RENDER_FB_W - 32;
    double frac = (double)pos_ms / (double)total_ms;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int handle_x = bar_x + (int)(frac * (double)bar_w);
    /* Generous horizontal grab radius so the small circle is easy to catch on
     * a touchscreen, but narrow enough that tapping the bar elsewhere (or the
     * controls just below) won't start a scrub. */
    if (x < handle_x - 26 || x > handle_x + 26) return 0;
    ui->scrub_active = 1;
    ui->scrub_total_ms = total_ms;
    double f = (double)(x - bar_x) / (double)bar_w;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    ui->scrub_preview_ms = (int64_t)(f * (double)total_ms);
    return 1;
}

/* Finger-move while scrubbing: update the preview to track the finger. The
 * handle + time display follow in draw_now_playing next frame. */
static int handle_now_playing_move(ui_state_t *ui, int x, int y) {
    (void)y;
    if (!ui->scrub_active) return 0;
    int bar_x = 16, bar_w = RENDER_FB_W - 32;
    double f = (double)(x - bar_x) / (double)bar_w;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    ui->scrub_preview_ms = (int64_t)(f * (double)ui->scrub_total_ms);
    return 1;
}

static int handle_now_playing_touch(ui_state_t *ui, int x, int y) {
    if (y < TITLE_BAR_H) {
        navigate_back(ui);
        return 1;
    }
    int ctrl_y = RENDER_FB_H - 150;
    int ctrl_w = 120;
    int gap = (RENDER_FB_W - 3 * ctrl_w) / 4;   /* 30 */

    /* Row 1: RW (-30s) / Play-Pause / FF (+60s) */
    if (y >= ctrl_y && y < ctrl_y + 60) {
        if (x >= gap && x < gap + ctrl_w) { player_rw(); return 1; }
        if (x >= gap * 2 + ctrl_w && x < gap * 2 + 2 * ctrl_w) {
            player_toggle(); return 1;
        }
        if (x >= gap * 3 + 2 * ctrl_w && x < gap * 3 + 3 * ctrl_w) {
            player_ff(); return 1;
        }
    }
    /* Row 2: Mark (add bookmark) / Sleep (cycle 0/15/30/60 min) /
     * Speed (cycle 1.0/1.1/1.25/1.5/2.0x) / Chaps. Uses the row-2 4-button layout
     * (g2=16, w2=100), independent of row 1's 3-button spacing. */
    int row2_y = ctrl_y + 72;
    if (y >= row2_y && y < row2_y + 52) {
        int g2 = 16, w2 = 100;
        int b0 = g2, b1 = g2 * 2 + w2, b2 = g2 * 3 + 2 * w2, b3 = g2 * 4 + 3 * w2;

        if (x >= b0 && x < b0 + w2) {
            player_snapshot_t snap;
            player_get_snapshot(&snap);
            int bid = player_add_bookmark(ui->db, snap.track_title);
            ui_log("[ui] add bookmark -> id=%d label=\"%s\"\n", bid,
                   snap.track_title);
            ui->mark_flash_until_ms = now_ms() + 800;
            return 1;
        }
        if (x >= b1 && x < b1 + w2) {
            static const int cyc[4] = {0, 15, 30, 60};
            int i = 0;
            for (int k = 0; k < 4; k++)
                if (cyc[k] == ui->sleep_minutes) { i = k; break; }
            ui->sleep_minutes = cyc[(i + 1) % 4];
            player_set_sleep_minutes(ui->sleep_minutes);
            return 1;
        }
        if (x >= b2 && x < b2 + w2) {
            static const int sp[5] = {1000, 1100, 1250, 1500, 2000};
            int cur = player_get_speed();
            int i = 0;
            for (int k = 0; k < 5; k++)
                if (sp[k] == cur) { i = k; break; }
            player_set_speed(sp[(i + 1) % 5]);
            return 1;
        }
        if (x >= b3 && x < b3 + w2) {
            navigate_to(ui, SCREEN_CHAPTERS, ui->list_mode,
                        ui->current_book_id);
            return 1;
        }
    }
    return 0;
}

/* ---- Screen drawing: Bookmarks ----------------------------------------- */

/* bookmark_row_t + bm_ctx_t are defined in ui.h (shared with the render cache). */
static int bm_collect_cb(const audiobook_bookmark_t *bm, void *ctx) {
    bm_ctx_t *c = (bm_ctx_t *)ctx;
    if (c->count >= c->capacity) {
        int nc = c->capacity ? c->capacity * 2 : 64;
        bookmark_row_t *nr = realloc(c->rows, nc * sizeof(bookmark_row_t));
        if (!nr) return 1;
        c->rows = nr;
        c->capacity = nc;
    }
    bookmark_row_t *row = &c->rows[c->count++];
    row->bookmark_id = bm->bookmark_id;
    row->position_ms = bm->total_book_position_ms;
    strncpy(row->label, bm->label[0] ? bm->label : "Bookmark",
            sizeof(row->label) - 1);
    row->label[sizeof(row->label) - 1] = '\0';
    return 0;
}

static void draw_bookmarks(ui_state_t *ui) {
    renderer_t *r = &ui->rend;

    render_text(r, 18, 16, "Back", FONT_SCALE_1, COL_GRAY_LT);
    render_text_right(r, RENDER_FB_W - 18, 16, "Bookmarks", FONT_SCALE_2,
                      COL_WHITE);
    render_draw_hline(r, 0, TITLE_BAR_H - 1, RENDER_FB_W, COL_DIVIDER);

    /* Draw from the render cache (built by rebuild_bookmarks on the event
     * thread). Hold the lock across the iteration so the event thread's
     * swap-and-free can't race us. */
    pthread_mutex_lock(&g_cache_lock);
    bookmark_row_t *rows = ui->bm_rows;
    int count = ui->bm_count;

    if (count == 0) {
        pthread_mutex_unlock(&g_cache_lock);
        render_text_centered(r, 0, RENDER_FB_H / 2, RENDER_FB_W,
                            "No bookmarks yet", FONT_SCALE_2, COL_GRAY_LT);
        return;
    }

    ui->scroll_max = count * LIST_ITEM_H - LIST_VIEWPORT_H;
    if (ui->scroll_max < 0) ui->scroll_max = 0;
    if (ui->scroll_offset > ui->scroll_max) ui->scroll_offset = ui->scroll_max;

    int y = TITLE_BAR_H - ui->scroll_offset;
    for (int i = 0; i < count; i++) {
        int item_top = y;
        if (item_top + LIST_ITEM_H < TITLE_BAR_H) { y += LIST_ITEM_H; continue; }
        if (item_top >= RENDER_FB_H - FOOTER_H) break;
        if (i == ui->selected_idx)
            render_fill_rect(r, 0, item_top, 4, LIST_ITEM_H, COL_ACCENT);

        char label_buf[256];
        strncpy(label_buf, rows[i].label, sizeof(label_buf) - 1);
        label_buf[sizeof(label_buf) - 1] = '\0';
        while (render_text_width(label_buf, FONT_SCALE_2) > RENDER_FB_W - 160 &&
               strlen(label_buf) > 1)
            label_buf[strlen(label_buf) - 1] = '\0';
        render_text(r, 24, item_top + 10, label_buf, FONT_SCALE_2, COL_WHITE);

        render_time(r, 24, item_top + 62, rows[i].position_ms, FONT_SCALE_1,
                    COL_GRAY_LT);

        y += LIST_ITEM_H;
        if (y < RENDER_FB_H - FOOTER_H)
            render_draw_hline(r, 0, y - 1, RENDER_FB_W, COL_DIVIDER);
    }

    char footer[96];
    snprintf(footer, sizeof(footer), "Tap: jump   Hold: delete   |   %d bookmark%s",
             count, count == 1 ? "" : "s");
    render_draw_hline(r, 0, RENDER_FB_H - FOOTER_H, RENDER_FB_W, COL_DIVIDER);
    render_text_centered(r, 0, RENDER_FB_H - FOOTER_H + 10, RENDER_FB_W,
                        footer, FONT_SCALE_1, COL_GRAY_LT);
    pthread_mutex_unlock(&g_cache_lock);
}

/* Tap a bookmark → jump to it (seek + play) and go to Now Playing. */
static int handle_bookmarks_touch(ui_state_t *ui, int x, int y) {
    if (y < TITLE_BAR_H) {
        navigate_back(ui);
        return 1;
    }
    if (y >= RENDER_FB_H - FOOTER_H) return 0;

    int item_y = TITLE_BAR_H - ui->scroll_offset;
    int idx = (y - item_y) / LIST_ITEM_H;
    if (idx < 0) return 0;

    bm_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.capacity = 64;
    c.rows = calloc(c.capacity, sizeof(bookmark_row_t));
    if (c.rows)
        audiobook_list_bookmarks(ui->db, ui->current_book_id, bm_collect_cb, &c);
    int hit = (idx < c.count);
    int64_t target = hit ? c.rows[idx].position_ms : 0;
    int book_id = ui->current_book_id;
    free(c.rows);
    if (hit) {
        player_play_book_from(book_id, target);
        navigate_to(ui, SCREEN_NOW_PLAYING, ui->list_mode, book_id);
    }
    return 1;
}

/* Long-press a bookmark → delete it (tap jumps instead). */
static int handle_bookmarks_longpress(ui_state_t *ui, int x, int y) {
    if (y < TITLE_BAR_H || y >= RENDER_FB_H - FOOTER_H) return 0;

    int item_y = TITLE_BAR_H - ui->scroll_offset;
    int idx = (y - item_y) / LIST_ITEM_H;
    if (idx < 0) return 0;

    bm_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.capacity = 64;
    c.rows = calloc(c.capacity, sizeof(bookmark_row_t));
    if (c.rows)
        audiobook_list_bookmarks(ui->db, ui->current_book_id, bm_collect_cb, &c);
    if (idx < c.count) {
        if (audiobook_db_write_trylock() == 0) {
            audiobook_delete_bookmark(ui->db, ui->current_book_id,
                                      c.rows[idx].bookmark_id);
            audiobook_db_write_unlock();
        }
        ui_log("[ui] deleted bookmark idx=%d id=%d\n", idx,
               c.rows[idx].bookmark_id);
    }
    free(c.rows);
    ui->selected_idx = 0;
    rebuild_bookmarks(ui);   /* refresh the cached list so the row disappears */
    return 1;
}

/* ---- Screen drawing: Chapters ------------------------------------------ */

/* chapter_row_t + ch_ctx_t are defined in ui.h (shared with the render cache). */
static int ch_collect_cb(const audiobook_chapter_t *ch, void *ctx) {
    ch_ctx_t *c = (ch_ctx_t *)ctx;
    /* Hard cap: stop collecting beyond MAX_CHAPTER_ROWS so the render allocation
     * can't grow unboundedly and OOM the device on a pathological chapter count. */
    if (c->count >= MAX_CHAPTER_ROWS) return 1;
    if (c->count >= c->capacity) {
        int nc = c->capacity ? c->capacity * 2 : 64;
        chapter_row_t *nr = realloc(c->rows, nc * sizeof(chapter_row_t));
        if (!nr) return 1;
        c->rows = nr;
        c->capacity = nc;
    }
    chapter_row_t *row = &c->rows[c->count++];
    row->start_ms = ch->start_ms;
    row->track_id = ch->track_id;
    strncpy(row->title, ch->title[0] ? ch->title : "Chapter",
            sizeof(row->title) - 1);
    row->title[sizeof(row->title) - 1] = '\0';
    return 0;
}

static void draw_chapters(ui_state_t *ui) {
    renderer_t *r = &ui->rend;

    render_text(r, 18, 16, "Back", FONT_SCALE_1, COL_GRAY_LT);
    render_text_right(r, RENDER_FB_W - 18, 16, "Chapters", FONT_SCALE_2,
                      COL_WHITE);
    render_draw_hline(r, 0, TITLE_BAR_H - 1, RENDER_FB_W, COL_DIVIDER);

    /* Draw from the render cache (built by rebuild_chapters on the event
     * thread). Hold the lock across the iteration. */
    pthread_mutex_lock(&g_cache_lock);
    chapter_row_t *rows = ui->ch_rows;
    int count = ui->ch_count;

    if (count == 0) {
        pthread_mutex_unlock(&g_cache_lock);
        render_text_centered(r, 0, RENDER_FB_H / 2, RENDER_FB_W,
                            "No chapters", FONT_SCALE_2, COL_GRAY_LT);
        return;
    }

    ui->scroll_max = count * LIST_ITEM_H - LIST_VIEWPORT_H;
    if (ui->scroll_max < 0) ui->scroll_max = 0;
    if (ui->scroll_offset > ui->scroll_max) ui->scroll_offset = ui->scroll_max;

    int y = TITLE_BAR_H - ui->scroll_offset;
    for (int i = 0; i < count; i++) {
        int item_top = y;
        if (item_top + LIST_ITEM_H < TITLE_BAR_H) { y += LIST_ITEM_H; continue; }
        if (item_top >= RENDER_FB_H - FOOTER_H) break;
        if (i == ui->selected_idx)
            render_fill_rect(r, 0, item_top, 4, LIST_ITEM_H, COL_ACCENT);

        /* Use the full row width and wrap once at a word boundary. Multipart
         * labels begin with "Part N", so the distinguishing text is visible
         * even when the metadata title itself is very long. */
        const int title_w = RENDER_FB_W - 48;
        char line1[256], line2[256] = {0};
        strncpy(line1, rows[i].title, sizeof(line1) - 1);
        line1[sizeof(line1) - 1] = '\0';
        if (render_text_width(line1, FONT_SCALE_2) > title_w) {
            int cut = (int)strlen(line1);
            while (cut > 1) {
                char saved = line1[cut];
                line1[cut] = '\0';
                if (render_text_width(line1, FONT_SCALE_2) <= title_w) {
                    line1[cut] = saved;
                    break;
                }
                line1[cut] = saved;
                cut--;
            }
            int split = cut;
            while (split > 0 && line1[split - 1] != ' ') split--;
            if (split < cut / 2) split = cut;  /* avoid a tiny first line */
            const char *rest = rows[i].title + split;
            while (*rest == ' ') rest++;
            line1[split] = '\0';
            while (split > 0 && line1[split - 1] == ' ')
                line1[--split] = '\0';
            strncpy(line2, rest, sizeof(line2) - 1);
            line2[sizeof(line2) - 1] = '\0';
            while (render_text_width(line2, FONT_SCALE_2) > title_w &&
                   strlen(line2) > 1)
                line2[strlen(line2) - 1] = '\0';
        }
        render_text(r, 24, item_top + 8, line1, FONT_SCALE_2, COL_WHITE);
        if (line2[0])
            render_text(r, 24, item_top + 36, line2, FONT_SCALE_2, COL_WHITE);

        render_time(r, 24, item_top + (line2[0] ? 76 : 62),
                    rows[i].start_ms, FONT_SCALE_1,
                    COL_GRAY_LT);

        y += LIST_ITEM_H;
        if (y < RENDER_FB_H - FOOTER_H)
            render_draw_hline(r, 0, y - 1, RENDER_FB_W, COL_DIVIDER);
    }

    char footer[64];
    snprintf(footer, sizeof(footer), "%d chapters", count);
    render_draw_hline(r, 0, RENDER_FB_H - FOOTER_H, RENDER_FB_W, COL_DIVIDER);
    render_text_centered(r, 0, RENDER_FB_H - FOOTER_H + 10, RENDER_FB_W,
                        footer, FONT_SCALE_1, COL_GRAY_LT);
    pthread_mutex_unlock(&g_cache_lock);
}

/* Collect chapter start_ms (by list position) for chapter-tap seek. */
typedef struct { int64_t *starts; int count; int cap; } ch_starts_t;
static int ch_starts_cb(const audiobook_chapter_t *ch, void *ctx) {
    ch_starts_t *c = (ch_starts_t *)ctx;
    if (c->count >= c->cap) return 1;
    c->starts[c->count++] = ch->start_ms;
    return 0;
}

static int handle_chapters_touch(ui_state_t *ui, int x, int y) {
    if (y < TITLE_BAR_H) {
        navigate_back(ui);
        return 1;
    }
    if (y >= RENDER_FB_H - FOOTER_H) return 0;

    int item_y = TITLE_BAR_H - ui->scroll_offset;
    int idx = (y - item_y) / LIST_ITEM_H;
    if (idx < 0) return 0;

    /* Resolve the tapped chapter's start_ms by list position. Sized to
     * MAX_CHAPTER_ROWS to match the render cap, so a tap on any visible row
     * maps to a valid entry (and taps beyond the cap harmlessly no-op). */
    int64_t *starts = malloc((size_t)MAX_CHAPTER_ROWS * sizeof(int64_t));
    if (!starts) return 1;
    ch_starts_t c = { starts, 0, MAX_CHAPTER_ROWS };
    audiobook_get_chapters(ui->db, ui->current_book_id, ch_starts_cb, &c);
    if (idx < c.count) {
        player_play_book_from(ui->current_book_id, starts[idx]);
        navigate_to(ui, SCREEN_NOW_PLAYING, ui->list_mode,
                    ui->current_book_id);
    }
    free(starts);
    return 1;
}

/* ---- Dummy implementations for forward-declared functions --------------- */
/* These need to exist in ui.h's contract but are defined here */
