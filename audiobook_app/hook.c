/* hook.c — LD_PRELOAD library for in-process audiobook UI.
 *
 * Architecture:
 *
 *   ioctl hook: We intercept the libc ioctl() function via LD_PRELOAD.
 *     When hiby_player calls ioctl(fb_fd, FBIOPAN_DISPLAY, &vscreeninfo),
 *     we draw our audiobook UI to the target buffer (at the yoffset from
 *     vscreeninfo) BEFORE letting the real pan proceed. This way:
 *       - hiby_player's display loop keeps running (the pan happens normally)
 *       - the touch controller stays alive (it needs the display loop)
 *       - our UI is what the user sees (we overwrote hiby_player's content
 *         right before the pan)
 *     This replaces the old Hook A trampoline which suppressed the pan
 *     entirely — that killed the touch hardware (the touch controller
 *     needs hiby_player's periodic display activity to stay alive).
 *
 *   Hook B: Audiobooks tile callback — inline trampoline on the code cave
 *     at 0x0075DAEC. When the user taps the Audiobooks tile, our hook_b
 *     runs instead of the stock cave code. hook_b sets audiobook_mode=1,
 *     reads the fb mmap pointer, opens /dev/input/event1 for touch, and
 *     runs the UI event loop. The ioctl hook (in hiby_player's render
 *     thread) draws the UI on every frame. The event loop (in the main
 *     thread) reads touch and updates UI state. On exit, audiobook_mode=0
 *     and the ioctl hook stops drawing — hiby_player's content shows again.
 *
 * Build: zig cc -target mipsel-linux-gnueabihf.2.22 -shared -fPIC \
 *   -fvisibility=hidden -fno-common -Os -s -I <zig>/lib/libc/include/any-linux-any \
 *   -o libaudiobook_hook.so hook.c ui.c render.c library.c scan.c tags.c sqlite3.c
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h>

#include "ui.h"
#include "font.h"
#include "storage_guard.h"

/* ---- hiby_player addresses (stock binary) ------------------------------- */
#define TILE_CAVE_ADDR           0x0075DAECu  /* code-cave: patched tile callback */
#define TILE_CAVE_PAGE           0x0075D000u
#define TILE_CAVE_PROLOGUE       0x27bdffe8u  /* addiu sp,sp,-0x18 */
#define FB_MMAP_PTR_VADDR       0x008b4c14u   /* .bss: fb mmap pointer */

/* ---- Known fb geometry (fixed for R1) ----------------------------------- */
#define FB_W    480
#define FB_H    800
#define FB_BPP  16
#define FB_STRIDE 960   /* bytes per line (480 * 2) */
#define FB_BUF_SIZE (FB_STRIDE * FB_H)  /* 768000 bytes per buffer */

/* ---- Global state ------------------------------------------------------- */
static volatile int audiobook_mode = 0;  /* 1 = draw UI before pan, 0 = passthrough */
static uint16_t *g_fb = NULL;            /* hiby_player's fb mmap base */
static int hook_b_installed = 0;
static uint32_t saved_insns_b[4];

typedef struct {
    uint16_t *fb;
    uint16_t *snapshot;
    struct fb_var_screeninfo vinfo;
    int have_vinfo;
} fb_handoff_t;

/* ---- Forward declarations ----------------------------------------------- */
static int hook_b(void *arg0, void *arg1);

/* ---- Persistent log fd (eliminate per-call open/close) -------------------- */
/* All log calls share this persistent fd. O_APPEND ensures concurrent writes
 * don't interleave. Only reopens on EIO/EBADF — the fd is never closed during
 * normal operation. This eliminates 10-50 ms fs stalls that were blocking the
 * event loop when held during key-repeat volume bursts or rapid navigation. */
static int g_log_fd = -1;
#define LOG_PATH "/tmp/.audiobook_hook.log"

static void log_open(void) {
    if (g_log_fd >= 0) return;  /* already open */
    g_log_fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    if (g_log_fd < 0) log_open();  /* defer first open until first use */
    if (g_log_fd >= 0) {
        char buf[256];
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        if (n > 0) {
            if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
            ssize_t w = write(g_log_fd, buf, n);
            (void)w;
            /* On error, close so next call retries. O_APPEND guarantees atomic appends. */
            if (w < 0) { close(g_log_fd); g_log_fd = -1; }
        }
    } else {
        /* Fallback: open a temporary fd (first boot or /tmp unavailable). */
        int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            char buf[256];
            int n = vsnprintf(buf, sizeof(buf), fmt, ap);
            if (n > 0) {
                if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
                write(fd, buf, n);
            }
            close(fd);
        }
    }
    va_end(ap);
}

static void ui_log_raw(const char *buf, int len) {
    /* Write raw pre-formatted buffer — avoids double-vsnprintf in callers. */
    if (g_log_fd < 0) log_open();
    if (g_log_fd >= 0) {
        ssize_t w = write(g_log_fd, buf, len);
        (void)w;
        if (w < 0) { close(g_log_fd); g_log_fd = -1; }
    } else {
        int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { write(fd, buf, len); close(fd); }
    }
}

/* Expose the persistent log fd for ui.c to use directly. */
__attribute__((visibility("default")))
int get_log_fd(void) {
    if (g_log_fd < 0) log_open();
    return g_log_fd;
}

static void flush_icache(void *addr, size_t len) {
    (void)addr;
    (void)len;
    __asm__ __volatile__("sync" ::: "memory");
}

static int is_hiby_player(void) {
    int fd = open("/proc/self/comm", O_RDONLY);
    if (fd < 0) return 0;
    char buf[64];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    if (strstr(buf, "hiby_player")) return 1;
    if (strstr(buf, "system_main_thr")) return 1;
    return 0;
}

/* ---- Trampoline installer (for Hook B) --------------------------------- */

static void build_trampoline(uint32_t target_addr, uint32_t *out) {
    uint32_t hi = (target_addr + 0x8000) >> 16;
    uint32_t lo = target_addr & 0xFFFF;
    out[0] = 0x3C190000u | hi;        /* lui  t9, hi      */
    out[1] = 0x27390000u | lo;        /* addiu t9, t9, lo */
    out[2] = 0x03200008u;             /* jr   t9          */
    out[3] = 0x00000000u;             /* nop              */
}

static int install_trampoline_b(void) {
    uint32_t *target = (uint32_t *)TILE_CAVE_ADDR;

    uint32_t first = target[0];
    if (first != TILE_CAVE_PROLOGUE) {
        if ((first >> 26) == 0x0F) {  /* lui opcode = already hooked */
            logmsg("[hook] tile cave already hooked, skipping\n");
            return 1;
        }
        logmsg("[hook] ERROR: cave prologue mismatch at 0x%08X: expected 0x%08X, got 0x%08X\n",
               TILE_CAVE_ADDR, TILE_CAVE_PROLOGUE, first);
        return 0;
    }

    saved_insns_b[0] = target[0];
    saved_insns_b[1] = target[1];
    saved_insns_b[2] = target[2];
    saved_insns_b[3] = target[3];

    if (mprotect((void *)TILE_CAVE_PAGE, 0x2000,
                 PROT_READ | PROT_WRITE) < 0) {
        logmsg("[hook] Hook B mprotect RW failed: %s\n", strerror(errno));
        return 0;
    }

    uint32_t tramp[4];
    build_trampoline((uint32_t)&hook_b, tramp);
    target[0] = tramp[0];
    target[1] = tramp[1];
    target[2] = tramp[2];
    target[3] = tramp[3];

    if (mprotect((void *)TILE_CAVE_PAGE, 0x2000,
                 PROT_READ | PROT_EXEC) < 0) {
        logmsg("[hook] Hook B mprotect RX failed: %s\n", strerror(errno));
    }
    flush_icache((void *)target, 16);

    hook_b_installed = 1;
    logmsg("[hook] Hook B installed at 0x%08X → 0x%08X (hook_b)\n",
           TILE_CAVE_ADDR, (uint32_t)&hook_b);
    return 1;
}

static void restore_trampoline_b(void) {
    if (!hook_b_installed) return;
    uint32_t *target = (uint32_t *)TILE_CAVE_ADDR;
    target[0] = saved_insns_b[0];
    target[1] = saved_insns_b[1];
    target[2] = saved_insns_b[2];
    target[3] = saved_insns_b[3];
    flush_icache((void *)target, 16);
}

/* ---- ioctl() hook: draw UI before FBIOPAN_DISPLAY ---------------------- */
/* This is the core rendering mechanism. hiby_player's render thread calls
 * hgl_fb_display → ioctl(fb_fd, FBIOPAN_DISPLAY, &vscreeninfo) on every
 * frame. We intercept this call:
 *   1. If audiobook_mode: draw our UI to the target buffer (at yoffset)
 *      before the pan, so the pan shows our UI.
 *   2. Call the real ioctl (via syscall) so the pan proceeds normally.
 *   3. Return the result.
 * This keeps hiby_player's display loop alive (touch controller stays
 * alive) while showing our UI. */

/* Mark as visible so the dynamic linker finds it for LD_PRELOAD. */
__attribute__((visibility("default")))
int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    /* Log the first few ioctl calls to verify our hook is being called. */
    static int call_count = 0;
    if (call_count < 5) {
        call_count++;
        logmsg("[hook] ioctl#%d: fd=%d req=0x%lx mode=%d fb=%p arg=%p\n",
               call_count, fd, request, audiobook_mode, (void*)g_fb, arg);
    }

    /* Log the first few FBIOPAN_DISPLAY calls (avoid flooding at 30fps). */
    static int pan_count = 0;
    if (request == FBIOPAN_DISPLAY && pan_count < 10) {
        pan_count++;
        logmsg("[hook] FBIOPAN_DISPLAY#%d: fd=%d mode=%d yo=%d\n",
               pan_count, fd, audiobook_mode,
               arg ? ((struct fb_var_screeninfo*)arg)->yoffset : -1);
    }

    /* Stock hiby_player's idle path may hard-blank fb0 independently of the
     * audiobook UI. That leaves audio and hardware controls alive but powers
     * down the panel/touch path. While Audiobooks owns the screen, convert the
     * request into our lightweight backlight-only blank. */
    if (audiobook_mode && request == FBIOBLANK) {
        int blank = (int)(intptr_t)arg;
        ui_notify_fb_blank(blank != FB_BLANK_UNBLANK);
        logmsg("[hook] FBIOBLANK intercepted value=%d action=%s\n",
               blank, blank == FB_BLANK_UNBLANK ? "unblank" : "lightweight");
        if (blank != FB_BLANK_UNBLANK)
            return 0;
    }

    if (audiobook_mode && g_fb && request == FBIOPAN_DISPLAY && arg) {
        struct fb_var_screeninfo *vinfo = (struct fb_var_screeninfo *)arg;
        int yoffset = vinfo->yoffset;
        /* Draw our UI to the buffer at yoffset. The fb memory is
         * contiguous: buffer 0 at offset 0, buffer 1 at offset FB_H
         * rows. To draw to buffer at yoffset, we offset the fb pointer
         * by yoffset * (FB_STRIDE/2) pixels. */
        uint16_t *buf = g_fb + yoffset * (FB_STRIDE / 2);
        ui_draw_frame(buf);
    }

    /* Call the real ioctl via direct syscall (bypasses our hook). */
    return syscall(SYS_ioctl, fd, request, arg);
}

/* ---- Framebuffer helpers ------------------------------------------------ */

static uint32_t fb_page_hash(const uint16_t *page) {
    uint32_t hash = 2166136261u;
    int pixels = FB_BUF_SIZE / (int)sizeof(uint16_t);
    for (int i = 0; i < pixels; i++) {
        hash ^= page[i];
        hash *= 16777619u;
    }
    return hash;
}

/* HiBy occasionally renders post-callback screens into the hidden page
 * without panning them. The launcher then looks frozen until another touch or
 * power event kicks the display loop. Track both pages briefly and pan each
 * newly changed hidden page during the handoff window. */
static void *fb_handoff_thread(void *arg) {
    fb_handoff_t *h = (fb_handoff_t *)arg;
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0) fd = open("/dev/fb0", O_RDWR);

    uint32_t hash0 = fb_page_hash(h->snapshot);
    uint32_t hash1 = hash0;
    free(h->snapshot);
    h->snapshot = NULL;
    int pan_logs = 0;

    for (int i = 0; i < 30; i++) {
        usleep(50000);
        if (audiobook_mode) break;

        uint16_t *page0 = h->fb;
        uint16_t *page1 =
            h->fb + FB_BUF_SIZE / (int)sizeof(uint16_t);
        uint32_t next0 = fb_page_hash(page0);
        uint32_t next1 = fb_page_hash(page1);
        int changed0 = next0 != hash0;
        int changed1 = next1 != hash1;
        if (!changed0 && !changed1) continue;

        /* Let the stock renderer finish the frame before exposing it. */
        usleep(20000);
        if (audiobook_mode) break;
        next0 = fb_page_hash(page0);
        next1 = fb_page_hash(page1);
        changed0 = next0 != hash0;
        changed1 = next1 != hash1;

        if (fd >= 0 && h->have_vinfo) {
            struct fb_var_screeninfo v = h->vinfo;
            int target = -1;
            if (changed0 && !changed1) target = 0;
            else if (changed1 && !changed0) target = (int)v.yres;

            if (target >= 0) {
                v.yoffset = target;
                int rc = syscall(SYS_ioctl, fd, FBIOPAN_DISPLAY, &v);
                if (pan_logs++ < 6)
                    logmsg("[hook] handoff panned changed page yoffset=%d rc=%d\n",
                           target, rc);
            } else if (pan_logs++ < 6) {
                logmsg("[hook] handoff: both pages changed; stock pan active\n");
            }
        }
        hash0 = next0;
        hash1 = next1;
    }

    if (fd >= 0) close(fd);
    if (h->snapshot) free(h->snapshot);
    free(h);
    return NULL;
}

/* ---- Hook B: tile callback → audiobook mode ----------------------------- */

static int hook_b(void *arg0, void *arg1) {
    (void)arg0;
    (void)arg1;
    logmsg("[hook] Hook B called — entering audiobook mode\n");

    /* Get the framebuffer pointer from hiby_player's global */
    uint16_t *fb = *(uint16_t **)FB_MMAP_PTR_VADDR;
    logmsg("[hook] fb=%p\n", (void *)fb);

    if (!fb) {
        logmsg("[hook] ERROR: fb mmap pointer is null, aborting\n");
        return 1;
    }

    g_fb = fb;

    /* Preserve the launcher before audiobook rendering overwrites both
     * framebuffer pages. This snapshot is only 750 KiB and exists only while
     * Audiobooks is open. */
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) fb_fd = open("/dev/fb0", O_RDWR);
    logmsg("[hook] fb_fd=%d\n", fb_fd);

    struct fb_var_screeninfo launcher_vinfo;
    memset(&launcher_vinfo, 0, sizeof(launcher_vinfo));
    int have_launcher_vinfo =
        fb_fd >= 0 &&
        syscall(SYS_ioctl, fb_fd, FBIOGET_VSCREENINFO, &launcher_vinfo) == 0;
    int launcher_yoffset = 0;
    if (have_launcher_vinfo &&
        launcher_vinfo.yoffset + FB_H <= launcher_vinfo.yres_virtual) {
        launcher_yoffset = (int)launcher_vinfo.yoffset;
    }

    uint16_t *launcher_snapshot = malloc(FB_BUF_SIZE);
    if (launcher_snapshot) {
        const uint16_t *src =
            fb + launcher_yoffset * (FB_STRIDE / (int)sizeof(uint16_t));
        memcpy(launcher_snapshot, src, FB_BUF_SIZE);
        logmsg("[hook] launcher frame saved yoffset=%d bytes=%d\n",
               launcher_yoffset, FB_BUF_SIZE);
    } else {
        logmsg("[hook] WARNING: launcher frame snapshot allocation failed\n");
    }

    audiobook_mode = 1;  /* ioctl hook starts drawing our UI */

    /* Load the system truetype font (msyh.ttf) once. If it fails, the
     * renderer falls back to the embedded 8x12 bitmap. */
    static int font_tried = 0;
    if (!font_tried) {
        font_tried = 1;
        if (font_init())
            logmsg("[hook] font: msyh.ttf loaded\n");
        else
            logmsg("[hook] font: msyh.ttf load FAILED, using bitmap fallback\n");
    }

    /* Use /dev/fb0 so we can drive the pan loop ourselves. hiby_player's
     * render thread depends on the main thread (which we're blocking in
     * this tile callback). When the main thread blocks, the render thread
     * stops panning → display freezes → touch IC dies. By driving the pan
     * ourselves, the display keeps updating and the touch IC stays alive.
     * Our ioctl hook draws our UI before each pan. */
    logmsg("[hook] entering audiobook UI event loop...\n");

    /* The stock X1600 MMC driver runtime-autosuspends the SD card after three
     * idle seconds. Keep that path active while this UI owns the SD-backed
     * catalog and media; restore the stock policy as soon as we return. */
    storage_guard_acquire();

    /* Run the audiobook UI. Returns when user taps "Back to Menu".
     * ui_run drives the pan loop (ioctl FBIOPAN_DISPLAY) at ~30fps; the
     * ioctl hook draws our UI before each pan. The event loop reads touch
     * from /dev/input/event1 (with EVIOCGRAB for exclusive access). */
    ui_run(fb, fb_fd);

    logmsg("[hook] UI exited, cleaning up...\n");
    storage_guard_release();

    /* Restore the exact launcher frame captured on entry. Copy it to both
     * pages so the stock double-buffer loop cannot expose an old audiobook
     * frame, then pan it into view immediately. */
    audiobook_mode = 0;
    if (launcher_snapshot) {
        memcpy(fb, launcher_snapshot, FB_BUF_SIZE);
        memcpy(fb + FB_BUF_SIZE / (int)sizeof(uint16_t),
               launcher_snapshot, FB_BUF_SIZE);
        if (have_launcher_vinfo && fb_fd >= 0) {
            launcher_vinfo.yoffset = launcher_yoffset;
            int rc = syscall(SYS_ioctl, fb_fd, FBIOPAN_DISPLAY,
                             &launcher_vinfo);
            logmsg("[hook] launcher frame restored yoffset=%d pan_rc=%d\n",
                   launcher_yoffset, rc);
        } else {
            logmsg("[hook] launcher frame restored (pan unavailable)\n");
        }

        fb_handoff_t *handoff = malloc(sizeof(*handoff));
        if (handoff) {
            handoff->fb = fb;
            handoff->snapshot = launcher_snapshot;
            handoff->vinfo = launcher_vinfo;
            handoff->have_vinfo = have_launcher_vinfo;
            pthread_t thread;
            if (pthread_create(&thread, NULL, fb_handoff_thread, handoff) == 0) {
                pthread_detach(thread);
                launcher_snapshot = NULL;  /* handoff thread owns it */
            } else {
                free(handoff);
            }
        }
        if (launcher_snapshot) free(launcher_snapshot);
    }
    if (fb_fd >= 0) close(fb_fd);

    logmsg("[hook] Hook B returning, hiby_player resumes\n");
    return 1;
}

/* ---- Signal handler (diagnostic + graceful degradation) ----------------- */

static void crash_handler(int sig, siginfo_t *info, void *ctx) {
    const char *signame = "UNKNOWN";
    switch (sig) {
        case 4:  signame = "SIGILL";  break;
        case 6:  signame = "SIGABRT"; break;
        case 7:  signame = "SIGBUS";  break;
        case 8:  signame = "SIGFPE";  break;
        case 11: signame = "SIGSEGV"; break;
        case 12: signame = "SIGSYS";  break;
    }
    logmsg("[hook] SIGNAL %d (%s) addr=%p — restoring hooks\n",
           sig, signame, info ? info->si_addr : NULL);
    audiobook_mode = 0;
    restore_trampoline_b();
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ---- Constructor (runs before hiby_player main) ------------------------- */

/* ADB is now always-on at boot via the firmware's /etc/init.d/S90adb script
 * (enabled in the build with -EnableBootAdb). S90adb runs from rcS at boot and
 * calls the stock T90adb helper, but only when System -> USB working mode is
 * "Device" (mode 1) — so it stays out of the way of USB-DAC and other USB
 * modes. ADB and USB-DAC share the single USB gadget controller and are thus
 * mutually exclusive by design; the USB working mode setting picks one. The
 * old audiobook-app "ADB (durable)" toggle and its hook-side poll are removed
 * — there is no longer any marker file or hook involvement in ADB. */

__attribute__((constructor))
static void hook_init(void) {
    if (!is_hiby_player()) {
        return;
    }

    logmsg("[hook] === hook_init (pid=%d) ===\n", getpid());

    signal(SIGHUP, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    int sigs[] = {4, 6, 7, 8, 11, 12};
    for (size_t i = 0; i < sizeof(sigs)/sizeof(sigs[0]); i++) {
        sigaction(sigs[i], &sa, NULL);
    }

    if (!install_trampoline_b()) {
        logmsg("[hook] Hook B failed, hiby_player runs without hooks\n");
        return;
    }

    logmsg("[hook] Hook B installed; ioctl hook active when audiobook_mode=1\n");
}

/* ---- Destructor (cleanup on exit) --------------------------------------- */

__attribute__((destructor))
static void hook_fini(void) {
    if (!is_hiby_player()) return;
    audiobook_mode = 0;
    restore_trampoline_b();
    logmsg("[hook] hook_fini — hooks removed\n");
}
