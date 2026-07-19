/*
 * r1_audiobook_smoke — Phase 1 de-risking binary.
 *
 * Proves standalone /dev/fb0 + /dev/snd/pcmC0D0p + /dev/input/event1 work on
 * the Ingenic X1600E without stock hiby_player owning any of them.
 *
 *   1. Opens /dev/fb0, unblank, confirms 480x800 RGB565, mmaps it, draws a
 *      bold recognizable test pattern (colored quadrants + border + center bar).
 *   2. Opens /dev/snd/pcmC0D0p, negotiates RW_INTERLEAVED / S16_LE / 2ch /
 *      44100, writes ~2s of 440Hz sine, drains.
 *   3. Reads /dev/input/event1 concurrently and prints ABS_MT_POSITION_X/Y +
 *      BTN_TOUCH + EV_KEY events to stderr.
 *   4. Exits cleanly: unmaps fb, closes pcm, closes input, FBIOBLANK(POWERDOWN)
 *      is NOT done (the supervisor re-inits fb after we exit).
 *
 * Text rendering is intentionally NOT done here — it is a Phase 3 concern.
 * The test pattern alone is a sufficient visual confirmation.
 *
 * Cross-compiled with Zig: zig cc -target mipsel-linux-musleabi -static -Os -s
 *
 * Usage: r1_audiobook_smoke [--secs N] [--no-fb] [--no-audio] [--no-input]
 *   Defaults: 5 seconds total, all subsystems enabled.
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ---- framebuffer (kernel uapi via musl) ---------------------------------- */
#include <linux/fb.h>

/* ---- input events (kernel uapi via musl) --------------------------------- */
#include <linux/input.h>

/* ---- ALSA PCM ioctls (real kernel uapi) ---------------------------------- */
/* sys/ioctl.h (included above) provides _IO/_IOW/_IOR/_IOWR. The kernel uapi
 * header defines the structs AND the ioctl numbers together, so the size
 * encoded in the ioctl number always matches the struct. HW_PARAMS is _IOWR
 * (not _IOW) — getting that wrong yields ENOTTY on the device. */
#include <sound/asound.h>

/* Convenience aliases for indexing hw_params.masks[] / .intervals[]. */
#define HW_ACCESS        SNDRV_PCM_HW_PARAM_ACCESS
#define HW_FORMAT        SNDRV_PCM_HW_PARAM_FORMAT
#define HW_SUBFORMAT     SNDRV_PCM_HW_PARAM_SUBFORMAT
#define HW_SAMPLE_BITS   SNDRV_PCM_HW_PARAM_SAMPLE_BITS
#define HW_FRAME_BITS    SNDRV_PCM_HW_PARAM_FRAME_BITS
#define HW_CHANNELS      SNDRV_PCM_HW_PARAM_CHANNELS
#define HW_RATE          SNDRV_PCM_HW_PARAM_RATE
#define HW_PERIOD_TIME   SNDRV_PCM_HW_PARAM_PERIOD_TIME
#define HW_PERIOD_SIZE   SNDRV_PCM_HW_PARAM_PERIOD_SIZE
#define HW_PERIOD_BYTES  SNDRV_PCM_HW_PARAM_PERIOD_BYTES
#define HW_PERIODS       SNDRV_PCM_HW_PARAM_PERIODS
#define HW_BUFFER_TIME   SNDRV_PCM_HW_PARAM_BUFFER_TIME
#define HW_BUFFER_SIZE   SNDRV_PCM_HW_PARAM_BUFFER_SIZE
#define HW_BUFFER_BYTES  SNDRV_PCM_HW_PARAM_BUFFER_BYTES
#define HW_TICK_TIME     SNDRV_PCM_HW_PARAM_TICK_TIME

/* Sanity: the ioctl number encodes sizeof, so a wrong struct size gives a
 * wrong ioctl number and the kernel returns ENOTTY. Print the computed
 * numbers so an on-device failure is diagnosable. */
static void log_ioctl_numbers(void) {
    fprintf(stderr, "[smoke] ioctl sizes: hw_params=%zu sw_params=%zu info=%zu\n",
            sizeof(struct snd_pcm_hw_params),
            sizeof(struct snd_pcm_sw_params),
            sizeof(struct snd_pcm_info));
    fprintf(stderr, "[smoke] ioctl numbers: HW_PARAMS=0x%08x SW_PARAMS=0x%08x "
            "PREPARE=0x%08x START=0x%08x DRAIN=0x%08x\n",
            (unsigned)SNDRV_PCM_IOCTL_HW_PARAMS,
            (unsigned)SNDRV_PCM_IOCTL_SW_PARAMS,
            (unsigned)SNDRV_PCM_IOCTL_PREPARE,
            (unsigned)SNDRV_PCM_IOCTL_START,
            (unsigned)SNDRV_PCM_IOCTL_DRAIN);
}

/* ---- helpers ------------------------------------------------------------ */

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void mask_set(struct snd_mask *m, unsigned int value) {
    memset(m, 0, sizeof(*m));
    m->bits[value / 32] |= 1u << (value % 32);
}

static void interval_set_single(struct snd_interval *i, unsigned int v) {
    i->min = v;
    i->max = v;
    i->openmin = 0;
    i->openmax = 0;
    i->integer = 1;
}

static void interval_open(struct snd_interval *i) {
    i->min = 0;
    i->max = (unsigned int)-1;
    i->openmin = 0;
    i->openmax = 0;
    i->integer = 0;
}

static int mask_is_set(const struct snd_mask *m, unsigned int value) {
    return (m->bits[value / 32] >> (value % 32)) & 1u;
}

/* ---- framebuffer -------------------------------------------------------- */

#define FB_W 480
#define FB_H 800
#define RGB565(r,g,b) (((unsigned short)(((r)&0xf8)<<8) | (((g)&0xfc)<<3) | (((b)&0xf8)>>3)))

static int fb_draw_pattern(int fb_fd) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fb_fd, FBIOBLANK, (void *)0) < 0) {     /* FBIOBLANK_UNBLANK = 0 */
        perror("[smoke] FBIOBLANK unblank");
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("[smoke] FBIOGET_VSCREENINFO");
        return -1;
    }
    fprintf(stderr, "[smoke] fb var: %ux%u bpp=%u virtual=%ux%u stride_line=%u\n",
            vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
            vinfo.xres_virtual, vinfo.yres_virtual,
            vinfo.xres_virtual * (vinfo.bits_per_pixel / 8));

    if (vinfo.xres != FB_W || vinfo.yres != FB_H || vinfo.bits_per_pixel != 16) {
        /* Try to set 480x800 RGB565 explicitly. */
        memset(&vinfo, 0, sizeof(vinfo));
        vinfo.xres = FB_W; vinfo.yres = FB_H;
        vinfo.xres_virtual = FB_W; vinfo.yres_virtual = FB_H * 2; /* double buffer */
        vinfo.bits_per_pixel = 16;
        vinfo.red.offset = 11; vinfo.red.length = 5; vinfo.red.msb_right = 0;
        vinfo.green.offset = 5; vinfo.green.length = 6; vinfo.green.msb_right = 0;
        vinfo.blue.offset = 0; vinfo.blue.length = 5; vinfo.blue.msb_right = 0;
        vinfo.transp.offset = 0; vinfo.transp.length = 0; vinfo.transp.msb_right = 0;
        vinfo.activate = FB_ACTIVATE_NOW;
        if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
            perror("[smoke] FBIOPUT_VSCREENINFO (continuing with current mode)");
        }
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
        fprintf(stderr, "[smoke] fb var after set: %ux%u bpp=%u virtual=%ux%u\n",
                vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
                vinfo.xres_virtual, vinfo.yres_virtual);
    } else {
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    }

    size_t map_len = finfo.smem_len;
    if (map_len == 0) map_len = vinfo.xres_virtual * vinfo.yres_virtual * 2;
    fprintf(stderr, "[smoke] fb fix: smem_len=%zu line_length=%u\n",
            (size_t)finfo.smem_len, finfo.line_length);

    unsigned short *fb = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fb_fd, 0);
    if (fb == MAP_FAILED) {
        perror("[smoke] mmap fb");
        return -1;
    }

    unsigned int stride = finfo.line_length / 2;   /* shorts per line */
    /* Four colored quadrants + a white border + a red center bar. */
    for (unsigned y = 0; y < FB_H; y++) {
        for (unsigned x = 0; x < FB_W; x++) {
            unsigned short c;
            int top = (y < FB_H / 2), left = (x < FB_W / 2);
            if      (top && left)  c = RGB565(31, 0, 0);    /* red TL */
            else if (top && !left) c = RGB565(0, 31, 0);    /* green TR */
            else if (!top && left) c = RGB565(0, 0, 31);    /* blue BL */
            else                  c = RGB565(31, 31, 0);   /* yellow BR */
            /* white border 4px */
            if (x < 4 || x >= FB_W - 4 || y < 4 || y >= FB_H - 4)
                c = RGB565(31, 31, 31);
            fb[y * stride + x] = c;
        }
    }
    /* Red center bar 120px tall, full-width-ish, to prove ordered writes. */
    for (unsigned y = 340; y < 460; y++) {
        for (unsigned x = 80; x < FB_W - 80; x++) {
            fb[y * stride + x] = RGB565(31, 0, 0);
        }
    }
    /* Inscribe a label-free marker: a white square in the very center. */
    for (unsigned y = 380; y < 420; y++)
        for (unsigned x = 220; x < 260; x++)
            fb[y * stride + x] = RGB565(31, 31, 31);

    munmap(fb, map_len);
    return 0;
}

/* ---- ALSA --------------------------------------------------------------- */

/* Phase 1 smoke: prove the PCM device negotiates and plays.
 *
 * Flow: HW_REFINE (everything open) to discover supported ranges, then set
 * singles for the key params and HW_PARAMS. This is the libasound pattern.
 * We print the refined ranges so an on-device failure is diagnosable. */

static void dump_refined(const struct snd_pcm_hw_params *hw) {
    fprintf(stderr, "[smoke] refined: access=0x%08x%08x format=0x%08x%08x "
            "subfmt=0x%08x%08x\n",
            hw->masks[HW_ACCESS-HW_ACCESS].bits[1], hw->masks[HW_ACCESS-HW_ACCESS].bits[0],
            hw->masks[HW_FORMAT-HW_ACCESS].bits[1], hw->masks[HW_FORMAT-HW_ACCESS].bits[0],
            hw->masks[HW_SUBFORMAT-HW_ACCESS].bits[1], hw->masks[HW_SUBFORMAT-HW_ACCESS].bits[0]);
    fprintf(stderr, "[smoke] refined: channels=[%u,%u] rate=[%u,%u] "
            "sample_bits=[%u,%u] period_size=[%u,%u] periods=[%u,%u] "
            "buffer_size=[%u,%u]\n",
            hw->intervals[HW_CHANNELS-HW_SAMPLE_BITS].min,
            hw->intervals[HW_CHANNELS-HW_SAMPLE_BITS].max,
            hw->intervals[HW_RATE-HW_SAMPLE_BITS].min,
            hw->intervals[HW_RATE-HW_SAMPLE_BITS].max,
            hw->intervals[HW_SAMPLE_BITS-HW_SAMPLE_BITS].min,
            hw->intervals[HW_SAMPLE_BITS-HW_SAMPLE_BITS].max,
            hw->intervals[HW_PERIOD_SIZE-HW_SAMPLE_BITS].min,
            hw->intervals[HW_PERIOD_SIZE-HW_SAMPLE_BITS].max,
            hw->intervals[HW_PERIODS-HW_SAMPLE_BITS].min,
            hw->intervals[HW_PERIODS-HW_SAMPLE_BITS].max,
            hw->intervals[HW_BUFFER_SIZE-HW_SAMPLE_BITS].min,
            hw->intervals[HW_BUFFER_SIZE-HW_SAMPLE_BITS].max);
}

static int pcm_play_tone(int pcm_fd) {
    struct snd_pcm_hw_params hw;
    struct snd_pcm_sw_params sw;

    /* --- 1. HW_REFINE with everything open to discover supported ranges --- */
    memset(&hw, 0, sizeof(hw));
    for (int i = 0; i < 3; i++) {
        memset(&hw.masks[i], 0xff, sizeof(hw.masks[i]));   /* all allowed */
    }
    for (int i = 0; i < (int)(sizeof(hw.intervals)/sizeof(hw.intervals[0])); i++) {
        interval_open(&hw.intervals[i]);
    }
    hw.rmask = (unsigned)-1;        /* refine all */
    hw.cmask = 0;
    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw) < 0) {
        perror("[smoke] HW_REFINE");
        return -1;
    }
    dump_refined(&hw);

    /* --- 2. Pick supported values --- */
    unsigned int channels = 2;
    if (hw.intervals[HW_CHANNELS - HW_SAMPLE_BITS].max < 2 ||
        !mask_is_set(&hw.masks[HW_ACCESS - HW_ACCESS], SNDRV_PCM_ACCESS_RW_INTERLEAVED)) {
        fprintf(stderr, "[smoke] RW_INTERLEAVED or stereo not supported\n");
        return -1;
    }
    if (hw.intervals[HW_CHANNELS - HW_SAMPLE_BITS].min > 2) channels = 2;
    if (channels > hw.intervals[HW_CHANNELS - HW_SAMPLE_BITS].max) channels = 2;
    /* Prefer 44100, fall back to 48000. */
    unsigned int rate = 44100;
    if (rate < hw.intervals[HW_RATE - HW_SAMPLE_BITS].min ||
        rate > hw.intervals[HW_RATE - HW_SAMPLE_BITS].max) {
        rate = 48000;
    }
    if (rate < hw.intervals[HW_RATE - HW_SAMPLE_BITS].min ||
        rate > hw.intervals[HW_RATE - HW_SAMPLE_BITS].max) {
        rate = hw.intervals[HW_RATE - HW_SAMPLE_BITS].min;
        if (rate == 0) rate = 44100;
    }
    unsigned int sample_bits = 16;
    /* period_size / buffer_size are picked DYNAMICALLY after the rate/channels
     * refines narrow the ranges. Initial hints only. */
    unsigned int period_size = 0;   /* filled after refine */
    unsigned int periods_hint = hw.intervals[HW_PERIODS - HW_SAMPLE_BITS].min;
    if (periods_hint < 2) periods_hint = 4;
    if (periods_hint > 8) periods_hint = 8;
    unsigned int buffer_size = 0;   /* filled after refine */
    fprintf(stderr, "[smoke] chosen: rate=%u ch=%u periods_hint=%u\n",
            rate, channels, periods_hint);

    /* --- 3. Incremental refine to narrow to a single config (libasound flow) ---
     *
     * HW_REFINE narrows dependent params automatically. We start from the
     * all-open refined struct and constrain one independent param at a time,
     * refining after each. The kernel propagates constraints and empties
     * unsupported params (e.g. TICK_TIME on tick-less hardware) cleanly.
     * Then HW_PARAMS applies the fully-determined config. */
    struct snd_mask *m_access   = &hw.masks[HW_ACCESS    - HW_ACCESS];
    struct snd_mask *m_format    = &hw.masks[HW_FORMAT    - HW_ACCESS];
    struct snd_mask *m_subformat = &hw.masks[HW_SUBFORMAT - HW_ACCESS];
    /* hw currently holds the all-open refined result. */

    #define REFINE_ONE(param) do { \
        hw.rmask = 1u << (param); hw.cmask = 0; \
        if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw) < 0) { \
            perror("[smoke] HW_REFINE " #param); return -1; \
        } \
    } while (0)

    mask_set(m_access, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    REFINE_ONE(HW_ACCESS);
    mask_set(m_format, SNDRV_PCM_FORMAT_S16_LE);
    REFINE_ONE(HW_FORMAT);
    mask_set(m_subformat, SNDRV_PCM_SUBFORMAT_STD);
    REFINE_ONE(HW_SUBFORMAT);
    interval_set_single(&hw.intervals[HW_CHANNELS - HW_SAMPLE_BITS], channels);
    REFINE_ONE(HW_CHANNELS);
    interval_set_single(&hw.intervals[HW_RATE - HW_SAMPLE_BITS], rate);
    REFINE_ONE(HW_RATE);
    /* Now period_size range is narrowed by rate/channels/format. Pick the min. */
    {
        struct snd_interval *ps = &hw.intervals[HW_PERIOD_SIZE - HW_SAMPLE_BITS];
        struct snd_interval *bs = &hw.intervals[HW_BUFFER_SIZE - HW_SAMPLE_BITS];
        struct snd_interval *pp = &hw.intervals[HW_PERIODS - HW_SAMPLE_BITS];
        fprintf(stderr, "[smoke] post-rate: period_size=[%u,%u] periods=[%u,%u] "
                "buffer_size=[%u,%u]\n",
                ps->min, ps->max, pp->min, pp->max, bs->min, bs->max);
        period_size = ps->min;
        if (period_size == 0) period_size = 1024;
        if (period_size > 16384) period_size = 16384;
        /* Aim for periods_hint periods, but clamp into the allowed range. */
        unsigned int periods = periods_hint;
        if (periods < pp->min) periods = pp->min;
        if (periods > pp->max) periods = pp->max;
        buffer_size = period_size * periods;
        if (buffer_size < bs->min) buffer_size = bs->min;
        if (buffer_size > bs->max) buffer_size = bs->max;
    }
    interval_set_single(&hw.intervals[HW_PERIOD_SIZE - HW_SAMPLE_BITS], period_size);
    REFINE_ONE(HW_PERIOD_SIZE);
    interval_set_single(&hw.intervals[HW_BUFFER_SIZE - HW_SAMPLE_BITS], buffer_size);
    REFINE_ONE(HW_BUFFER_SIZE);
    /* Read back the final determined values for sw_params. */
    period_size  = hw.intervals[HW_PERIOD_SIZE  - HW_SAMPLE_BITS].min;
    buffer_size  = hw.intervals[HW_BUFFER_SIZE  - HW_SAMPLE_BITS].min;
    fprintf(stderr, "[smoke] after refine: period_size=%u buffer_size=%u\n",
            period_size, buffer_size);

    hw.rmask = 0;                   /* apply, do not refine */
    hw.cmask = 0;
    hw.info = 0;
    hw.msbits = 0;
    hw.rate_num = rate;
    hw.rate_den = 1;
    hw.fifo_size = 0;

    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0) {
        perror("[smoke] HW_PARAMS");
        fprintf(stderr, "[smoke] cmask=0x%x info=0x%x rate_num=%u rate_den=%u\n",
                hw.cmask, hw.info, hw.rate_num, hw.rate_den);
        return -1;
    }
    fprintf(stderr, "[smoke] HW_PARAMS ok (cmask=0x%x info=0x%x)\n",
            hw.cmask, hw.info);

    /* --- 4. SW_PARAMS + PREPARE --- */
    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = 0;
    sw.period_step = 1;
    sw.sleep_min = 0;
    sw.avail_min = period_size;
    sw.start_threshold = 1;          /* start on first write */
    sw.stop_threshold = buffer_size;
    sw.silence_threshold = 0;
    sw.silence_size = 0;
    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0) {
        perror("[smoke] SW_PARAMS");
        return -1;
    }
    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0) {
        perror("[smoke] PREPARE");
        return -1;
    }
    fprintf(stderr, "[smoke] PREPARE ok\n");

    /* --- 5. Write 2s of 440Hz sine --- */
    unsigned long total_frames = (unsigned long)rate * 2;
    unsigned long frames_left = total_frames;
    static short chunk[1024 * 2];   /* up to 1024 frames * 2ch */
    const double freq = 440.0;
    double phase = 0.0;
    const double incr = 2.0 * M_PI * freq / (double)rate;
    unsigned long frames_written = 0;
    int started = 0;

    while (frames_left > 0) {
        unsigned long n = period_size;
        if (n > frames_left) n = frames_left;
        for (unsigned long i = 0; i < n; i++) {
            short s = (short)(lrint(0.3 * sin(phase) * 32767.0));
            chunk[i * 2 + 0] = s;
            chunk[i * 2 + 1] = s;
            phase += incr;
        }
        size_t bytes = (size_t)n * channels * sizeof(short);
        ssize_t w = write(pcm_fd, chunk, bytes);
        if (w < 0) {
            if (errno == EINTR) continue;
            perror("[smoke] pcm write");
            return -1;
        }
        if (w == 0) {
            fprintf(stderr, "[smoke] pcm write returned 0 (underrun?)\n");
            break;
        }
        unsigned long wf = (unsigned long)w / (channels * sizeof(short));
        frames_written += wf;
        frames_left -= wf;
        if (!started) {
            ioctl(pcm_fd, SNDRV_PCM_IOCTL_START, 0);
            started = 1;
        }
    }
    fprintf(stderr, "[smoke] wrote %lu frames (%.1fs), draining\n",
            frames_written, (double)frames_written / rate);

    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_DRAIN, 0) < 0) {
        perror("[smoke] DRAIN");
    }
    return 0;
}

/* ---- input -------------------------------------------------------------- */

static int input_read_loop(int input_fd, long long until_ms) {
    int saw_touch = 0, saw_key = 0;
    while (now_ms() < until_ms) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(input_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 200000;
        int rc = select(input_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) { if (errno == EINTR) continue; perror("[smoke] select"); break; }
        if (rc == 0) continue;
        struct input_event ev;
        ssize_t n = read(input_fd, &ev, sizeof(ev));
        if (n == 0) continue;
        if (n < 0) { if (errno == EINTR) continue; perror("[smoke] read event"); break; }
        if ((size_t)n != sizeof(ev)) continue;
        if (ev.type == EV_ABS) {
            fprintf(stderr, "[smoke] EV_ABS code=%u value=%d\n", ev.code, ev.value);
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_MT_POSITION_Y) saw_touch = 1;
        } else if (ev.type == EV_KEY) {
            fprintf(stderr, "[smoke] EV_KEY  code=%u value=%d\n", ev.code, ev.value);
            saw_key = 1;
        } else if (ev.type == EV_SYN) {
            /* quiet */
        } else {
            fprintf(stderr, "[smoke] ev type=%u code=%u value=%d\n",
                    ev.type, ev.code, ev.value);
        }
    }
    fprintf(stderr, "[smoke] input summary: saw_touch=%d saw_key=%d\n",
            saw_touch, saw_key);
    return (saw_touch || saw_key) ? 0 : 1;
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv) {
    int do_fb = 1, do_audio = 1, do_input = 1;
    int secs = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-fb")) do_fb = 0;
        else if (!strcmp(argv[i], "--no-audio")) do_audio = 0;
        else if (!strcmp(argv[i], "--no-input")) do_input = 0;
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) secs = atoi(argv[++i]);
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    log_ioctl_numbers();

    int fb_fd = -1, pcm_fd = -1, input_fd = -1;
    int rc = 0;

    if (do_fb) {
        fb_fd = open("/dev/fb0", O_RDWR);
        if (fb_fd < 0) { perror("[smoke] open /dev/fb0"); rc = 1; }
        else {
            if (fb_draw_pattern(fb_fd) < 0) rc = 1;
            fprintf(stderr, "[smoke] fb pattern drawn\n");
        }
    }

    if (do_audio) {
        pcm_fd = open("/dev/snd/pcmC0D0p", O_RDWR);
        if (pcm_fd < 0) { perror("[smoke] open /dev/snd/pcmC0D0p"); rc = 1; }
        else {
            if (pcm_play_tone(pcm_fd) < 0) rc = 1;
        }
    }

    if (do_input) {
        input_fd = open("/dev/input/event1", O_RDONLY);
        if (input_fd < 0) { perror("[smoke] open /dev/input/event1"); rc = 1; }
    }

    /* Hold the display + read input until the timer expires. Audio runs
     * concurrently; if audio is enabled it finishes in ~2s then we just
     * hold the screen for the remainder. */
    if (do_input && input_fd >= 0) {
        long long until = now_ms() + (long long)secs * 1000LL;
        fprintf(stderr, "[smoke] reading input for %d seconds...\n", secs);
        input_read_loop(input_fd, until);
    } else if (secs > 0) {
        fprintf(stderr, "[smoke] sleeping %d seconds to hold the display\n", secs);
        sleep(secs);
    }

    if (fb_fd >= 0) {
        /* Power down the display to release the DMA controller so
         * hiby_player can re-acquire it after we exit. Without this the
         * jzfb DMA stays "in use" and hiby_player fails with
         * "Access hgl dma failed". */
        if (ioctl(fb_fd, FBIOBLANK, (void *)1) < 0)
            perror("[smoke] FBIOBLANK powerdown");
        usleep(100000); /* give the DMA 100ms to actually power down */
        close(fb_fd);
    }
    if (pcm_fd >= 0) close(pcm_fd);
    if (input_fd >= 0) close(input_fd);

    fprintf(stderr, "[smoke] done rc=%d\n", rc);
    return rc;
}