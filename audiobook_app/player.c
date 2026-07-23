/* player.c — audio playback engine (MP3 via minimp3_ex, ALSA output).
 *
 * Decode->ALSA loop on a dedicated pthread. MP3 decoding uses minimp3_ex
 * (a self-contained, public-domain single-header MP3 decoder compiled
 * directly into this .so) and libasound is dlopen'd at runtime for ALSA
 * output (keeps us dep-free of HiBy's broken libs).
 *
 * Why not HiBy's libmp3.so (libmpg123), discovered by on-device probing
 * (2026-07-17):
 *  - libmp3.so's file readers are stubs (mpg123_open/_fd return -1), and
 *    mpg123_format resampling is stripped — those we could work around.
 *  - BUT its mpg123_read returns GARBLED PCM for 22050 Hz MPEG-2 (LSF)
 *    audiobook files: L channel = full-scale clipping noise (ZCR ~0.5,
 *    centroid ~5500Hz), R channel = low-frequency rumble, channels
 *    uncorrelated (corr ~0.000) — i.e. a broken MPEG-2/stereo decode.
 *    A real audiobook (mono duplicated to stereo) decodes to centroid
 *    ~500Hz, ZCR ~0.1, L-R correlation ~1.0. minimp3_ex produces exactly
 *    that. So we decode MP3 ourselves with minimp3_ex and feed ALSA.
 *  - We open the file ourselves and install read/seek callbacks via
 *    mp3dec_ex_open_cb (no reliance on the stubbed file readers).
 *  - Seek strategy: MP3D_SEEK_TO_BYTE (NOT SEEK_TO_SAMPLE). SEEK_TO_SAMPLE
 *    builds the FULL frame index on the first non-zero seek (~14.5MB for a
 *    6.6h file + a 193MB scan), which OOMs this 56MB-RAM device and freezes
 *    the player. SEEK_TO_BYTE just lseeks to seek_ms*bitrate_kbps/8 and
 *    syncs to the next frame — instant, ~0 memory, ~26ms accurate.
 *  - Output at the file's native rate; ALSA plughw:0,0 resamples to the
 *    hardware's discrete rate set. minimp3 outputs S16 (mp3d_sample_t =
 *    int16_t), little-endian on MIPS — matches A_FMT_S16_LE.
 *  - snd_pcm_set_params is flaky here; set hw/sw params manually
 *    (rate_near + buffer/period_time_near) and start_threshold=1.
 *
 * M4B/AAC is not wired here yet (step 4); books whose first track isn't MP3
 * report player_format_unsupported()=1 so the UI can show a message.
 */

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>
#include "player.h"
#include "tags.h"          /* audio_file_type / AUDIO_EXT_* */
#include "mp4_audio.h"     /* M4B/AAC demux (AAC decode via dlopen'd fdk-aac) */
#include "wsola.h"         /* pitch-preserving time-stretch (speed != 1.0x) */
#include "posstore.h"      /* SD-primary position store (never lost to full /usr/data) */

#define POSITION_SAVE_INTERVAL_MS 15000u
#define DB_MIRROR_INTERVAL_MS     60000u

/* ---- logging ----------------------------------------------------------- */
static void plog(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* Use persistent log fd (open-once with O_APPEND) — avoid per-call open/close stalls. */
    extern int get_log_fd(void);
    int fd = get_log_fd();
    if (fd >= 0) {
        write(fd, "[player] ", 9);
        write(fd, buf, strlen(buf));
        write(fd, "\n", 1);
    } else {
        /* Fallback for when persistent fd isn't ready. */
        int t = open("/tmp/.audiobook_hook.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (t >= 0) {
            write(t, "[player] ", 9);
            write(t, buf, strlen(buf));
            write(t, "\n", 1);
            close(t);
        }
    }
}

/* ---- ALSA function pointers (dlopen'd) --------------------------------- */
/* MP3 decode is handled by minimp3_ex (compiled in, no dlopen). Only ALSA
 * output is loaded at runtime via dlsym, matching how hiby_player uses it
 * and keeping our .so free of link-time deps. */

static void *g_alsa_lib = NULL;

/* ALSA */
static int (*x_snd_pcm_open)(void **, const char *, int, int);
static int (*x_snd_pcm_close)(void *);
static long (*x_snd_pcm_writei)(void *, const void *, unsigned long);
static int (*x_snd_pcm_prepare)(void *);
static int (*x_snd_pcm_drop)(void *);
static int (*x_snd_pcm_recover)(void *, int, int);
/* manual hw_params */
static int (*x_snd_pcm_hw_params_malloc)(void **);
static void (*x_snd_pcm_hw_params_free)(void *);
static int (*x_snd_pcm_hw_params_any)(void *, void *);
static int (*x_snd_pcm_hw_params_set_access)(void *, void *, int);
static int (*x_snd_pcm_hw_params_set_format)(void *, void *, int);
static int (*x_snd_pcm_hw_params_set_channels)(void *, void *, unsigned int);
static int (*x_snd_pcm_hw_params_set_rate_near)(void *, void *, unsigned int *, int *);
static int (*x_snd_pcm_hw_params_set_buffer_time_near)(void *, void *, unsigned int *, int *);
static int (*x_snd_pcm_hw_params_set_period_time_near)(void *, void *, unsigned int *, int *);
static int (*x_snd_pcm_hw_params_get_period_size)(void *, unsigned long *, int *);
static int (*x_snd_pcm_hw_params)(void *, void *);
/* manual sw_params */
static int (*x_snd_pcm_sw_params_malloc)(void **);
static void (*x_snd_pcm_sw_params_free)(void *);
static int (*x_snd_pcm_sw_params_current)(void *, void *);
static int (*x_snd_pcm_sw_params_set_start_threshold)(void *, void *, unsigned long);
static int (*x_snd_pcm_sw_params_set_avail_min)(void *, void *, unsigned long);
static int (*x_snd_pcm_sw_params)(void *, void *);
/* ALSA mixer (DAC hardware volume) */
static void *(*x_snd_mixer_open)(void **, int);
static int (*x_snd_mixer_attach)(void *, const char *);
static int (*x_snd_mixer_load)(void *);
static void (*x_snd_mixer_close)(void *);
static int (*x_snd_mixer_selem_register)(void *, void *, void **);
static void *(*x_snd_mixer_first_elem)(void *);
static void *(*x_snd_mixer_elem_next)(void *);
static const char *(*x_snd_mixer_selem_get_name)(void *);
static int (*x_snd_mixer_selem_has_playback_volume)(void *);
static int (*x_snd_mixer_selem_set_playback_volume_all)(void *, long);
static int (*x_snd_mixer_selem_get_playback_volume)(void *, int, long *);
static int (*x_snd_mixer_selem_get_playback_volume_range)(void *, long *, long *);

/* ---- fdk-aac function pointers (dlopen'd, OPTIONAL) --------------------- */
/* AAC decode for M4B/M4A. Loaded at runtime like ALSA; if the lib is absent,
 * M4B books report player_format_unsupported()=1 (MP3 unaffected). API verified
 * on-device against libfdk-aac.so.2.0.1 (probe_aac.c). */
typedef void *HANDLE_AACDECODER;
typedef int   AAC_DECODER_ERROR;
/* Prefix of fdk-aac's CStreamInfo — only the three INT fields we read. */
typedef struct {
    int sampleRate;
    int frameSize;
    int numChannels;
    void *pChannelType;
    void *pChannelIndices;
} CStreamInfo;

static void *g_fdkaac_lib = NULL;
static HANDLE_AACDECODER (*x_aac_Open)(int, int);
static AAC_DECODER_ERROR (*x_aac_ConfigRaw)(HANDLE_AACDECODER, uint8_t **, const unsigned int *);
static AAC_DECODER_ERROR (*x_aac_Fill)(HANDLE_AACDECODER, uint8_t **, const unsigned int *, unsigned int *);
static AAC_DECODER_ERROR (*x_aac_DecodeFrame)(HANDLE_AACDECODER, int16_t *, const int, const unsigned int);
static CStreamInfo *(*x_aac_GetStreamInfo)(HANDLE_AACDECODER);
static void (*x_aac_Close)(HANDLE_AACDECODER);

#define AAC_TT_MP4_RAW 0   /* "as is" access units, no sync layer */
#define AACDEC_INTR    1u  /* flag: signal a clean restart on next decode */

/* ALSA constants (stable across alsa-lib) */
#define A_STREAM_PLAYBACK 0
#define A_FMT_S16_LE       2
#define A_ACCESS_RW_INT    3

/* SYM: pass the literal symbol name explicitly (the pointer is x_-prefixed
 * to avoid any name clash; the macro must NOT stringify the pointer name). */
#define SYM(h, ptr, sym, type) ptr = (type)dlsym(h, sym)

static int load_libs(void) {
    /* MP3 decode is via minimp3_ex (compiled in) — nothing to dlopen for it.
     * Only ALSA output is loaded at runtime. */
    g_alsa_lib = dlopen("libasound.so", RTLD_LAZY);
    if (!g_alsa_lib) g_alsa_lib = dlopen("libasound.so.2", RTLD_LAZY);
    if (!g_alsa_lib) { plog("dlopen libasound failed: %s", dlerror()); return -1; }

    SYM(g_alsa_lib, x_snd_pcm_open, "snd_pcm_open", int (*)(void **, const char *, int, int));
    SYM(g_alsa_lib, x_snd_pcm_close, "snd_pcm_close", int (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_writei, "snd_pcm_writei", long (*)(void *, const void *, unsigned long));
    SYM(g_alsa_lib, x_snd_pcm_prepare, "snd_pcm_prepare", int (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_drop, "snd_pcm_drop", int (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_recover, "snd_pcm_recover", int (*)(void *, int, int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_malloc, "snd_pcm_hw_params_malloc", int (*)(void **));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_free, "snd_pcm_hw_params_free", void (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_any, "snd_pcm_hw_params_any", int (*)(void *, void *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_access, "snd_pcm_hw_params_set_access", int (*)(void *, void *, int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_format, "snd_pcm_hw_params_set_format", int (*)(void *, void *, int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_channels, "snd_pcm_hw_params_set_channels", int (*)(void *, void *, unsigned int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_rate_near, "snd_pcm_hw_params_set_rate_near", int (*)(void *, void *, unsigned int *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_buffer_time_near, "snd_pcm_hw_params_set_buffer_time_near", int (*)(void *, void *, unsigned int *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_period_time_near, "snd_pcm_hw_params_set_period_time_near", int (*)(void *, void *, unsigned int *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_get_period_size, "snd_pcm_hw_params_get_period_size", int (*)(void *, unsigned long *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params, "snd_pcm_hw_params", int (*)(void *, void *));
    SYM(g_alsa_lib, x_snd_pcm_sw_params_malloc, "snd_pcm_sw_params_malloc", int (*)(void **));
    SYM(g_alsa_lib, x_snd_pcm_sw_params_free, "snd_pcm_sw_params_free", void (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_sw_params_current, "snd_pcm_sw_params_current", int (*)(void *, void *));
    SYM(g_alsa_lib, x_snd_pcm_sw_params_set_start_threshold, "snd_pcm_sw_params_set_start_threshold", int (*)(void *, void *, unsigned long));
    SYM(g_alsa_lib, x_snd_pcm_sw_params_set_avail_min, "snd_pcm_sw_params_set_avail_min", int (*)(void *, void *, unsigned long));
    SYM(g_alsa_lib, x_snd_pcm_sw_params, "snd_pcm_sw_params", int (*)(void *, void *));
    /* Mixer syms are optional (volume buttons degrade gracefully if absent). */
    SYM(g_alsa_lib, x_snd_mixer_open, "snd_mixer_open", void *(*)(void **, int));
    SYM(g_alsa_lib, x_snd_mixer_attach, "snd_mixer_attach", int (*)(void *, const char *));
    SYM(g_alsa_lib, x_snd_mixer_load, "snd_mixer_load", int (*)(void *));
    SYM(g_alsa_lib, x_snd_mixer_close, "snd_mixer_close", void (*)(void *));
    SYM(g_alsa_lib, x_snd_mixer_selem_register, "snd_mixer_selem_register", int (*)(void *, void *, void **));
    SYM(g_alsa_lib, x_snd_mixer_first_elem, "snd_mixer_first_elem", void *(*)(void *));
    SYM(g_alsa_lib, x_snd_mixer_elem_next, "snd_mixer_elem_next", void *(*)(void *));
    SYM(g_alsa_lib, x_snd_mixer_selem_get_name, "snd_mixer_selem_get_name", const char *(*)(void *));
    SYM(g_alsa_lib, x_snd_mixer_selem_has_playback_volume, "snd_mixer_selem_has_playback_volume", int (*)(void *));
    SYM(g_alsa_lib, x_snd_mixer_selem_set_playback_volume_all, "snd_mixer_selem_set_playback_volume_all", int (*)(void *, long));
    SYM(g_alsa_lib, x_snd_mixer_selem_get_playback_volume, "snd_mixer_selem_get_playback_volume", int (*)(void *, int, long *));
    SYM(g_alsa_lib, x_snd_mixer_selem_get_playback_volume_range, "snd_mixer_selem_get_playback_volume_range", int (*)(void *, long *, long *));
    if (!x_snd_pcm_open || !x_snd_pcm_close || !x_snd_pcm_writei ||
        !x_snd_pcm_prepare || !x_snd_pcm_drop ||
        !x_snd_pcm_hw_params_malloc || !x_snd_pcm_hw_params_free ||
        !x_snd_pcm_hw_params_any || !x_snd_pcm_hw_params_set_access ||
        !x_snd_pcm_hw_params_set_format || !x_snd_pcm_hw_params_set_channels ||
        !x_snd_pcm_hw_params_set_rate_near ||
        !x_snd_pcm_hw_params_set_buffer_time_near ||
        !x_snd_pcm_hw_params_set_period_time_near ||
        !x_snd_pcm_hw_params_get_period_size || !x_snd_pcm_hw_params ||
        !x_snd_pcm_sw_params_malloc || !x_snd_pcm_sw_params_free ||
        !x_snd_pcm_sw_params_current ||
        !x_snd_pcm_sw_params_set_start_threshold ||
        !x_snd_pcm_sw_params_set_avail_min || !x_snd_pcm_sw_params) {
        plog("missing alsa symbols"); return -1;
    }

    /* fdk-aac is OPTIONAL (M4B support). Missing lib => M4B->fmt_unsupported,
     * MP3 unaffected. Try the symlink first, then the versioned soname. */
    g_fdkaac_lib = dlopen("libfdk-aac.so", RTLD_LAZY);
    if (!g_fdkaac_lib) g_fdkaac_lib = dlopen("libfdk-aac.so.2", RTLD_LAZY);
    if (g_fdkaac_lib) {
        SYM(g_fdkaac_lib, x_aac_Open,          "aacDecoder_Open",          HANDLE_AACDECODER (*)(int, int));
        SYM(g_fdkaac_lib, x_aac_ConfigRaw,     "aacDecoder_ConfigRaw",     AAC_DECODER_ERROR (*)(HANDLE_AACDECODER, uint8_t **, const unsigned int *));
        SYM(g_fdkaac_lib, x_aac_Fill,          "aacDecoder_Fill",          AAC_DECODER_ERROR (*)(HANDLE_AACDECODER, uint8_t **, const unsigned int *, unsigned int *));
        SYM(g_fdkaac_lib, x_aac_DecodeFrame,   "aacDecoder_DecodeFrame",   AAC_DECODER_ERROR (*)(HANDLE_AACDECODER, int16_t *, const int, const unsigned int));
        SYM(g_fdkaac_lib, x_aac_GetStreamInfo, "aacDecoder_GetStreamInfo", CStreamInfo *(*)(HANDLE_AACDECODER));
        SYM(g_fdkaac_lib, x_aac_Close,         "aacDecoder_Close",         void (*)(HANDLE_AACDECODER));
        if (!x_aac_Open || !x_aac_DecodeFrame || !x_aac_ConfigRaw ||
            !x_aac_Fill || !x_aac_GetStreamInfo || !x_aac_Close) {
            plog("fdk-aac present but missing core symbols — AAC disabled");
            dlclose(g_fdkaac_lib); g_fdkaac_lib = NULL;
        } else {
            plog("fdk-aac loaded (AAC enabled)");
        }
    } else {
        plog("fdk-aac not found (M4B will report unsupported): %s", dlerror());
    }

    plog("libs loaded (minimp3_ex + alsa)");
    return 0;
}

/* ---- engine state ------------------------------------------------------- */

typedef struct {
    int track_id;
    int ordinal;
    char path[512];
    char title[256];
    int64_t duration_ms;
} ptrack_t;

enum {
    CMD_NONE = 0, CMD_PLAY, CMD_RESUME, CMD_PAUSE, CMD_STOP, CMD_SEEK,
    CMD_FF, CMD_RW, CMD_TOGGLE, CMD_QUIT,
    /* row-1 skip buttons: FF +60s / RW -30s */
};
enum { DEC_MP3 = 0, DEC_AAC = 1 };

#define CMD_QUEUE_CAP 16
typedef struct {
    int cmd;
    int book_id;
    int64_t seek_ms;
    int64_t start_ms;
} player_cmd_t;

/* Audio output sink. WIRED = the CS43131 wired DAC (plughw:0,0/hw:0,0); BT =
 * BlueALSA A2DP (the predefined `pcm.bluealsa` plug device, auto rate/format
 * conversion). Auto-detect per track-open: BT when an A2DP sink is connected,
 * else wired. force_wired is set by the mid-playback fallback so the rest of
 * the current track stays wired; cleared at the next track-open so detection
 * re-runs. bt_fell_back guards one fallback per track (no close/reopen loop). */
#define OUT_WIRED 0
#define OUT_BT    1

static struct {
    sqlite3 *db;
    pthread_t thread;
    pthread_mutex_t mu;
    int thread_alive;
    int running;

    player_cmd_t cmd_queue[CMD_QUEUE_CAP];
    int cmd_head;
    int cmd_tail;
    int cmd_count;

    volatile player_state_t state;
    volatile int book_id;
    volatile int fmt_unsupported;
    volatile int64_t position_ms;   /* book-elapsed */
    volatile int64_t total_ms;
    volatile int track_idx;
    player_snapshot_t snapshot; /* published under mu for all UI readers */

    int last_book;     /* last loaded book, for toggle-resume */

    ptrack_t tracks[256];
    int track_count;
    int64_t track_base_ms;   /* sum durations before track_idx */
    int64_t track_pos_ms;     /* within current track */

    mp3dec_ex_t dec;        /* minimp3_ex streaming decoder (open while a track is loaded) */
    mp3dec_io_t io;         /* read/seek callbacks wired to track_fd */
    int dec_open;           /* dec is open for the current track (MP3 path) */
    int track_fd;           /* fd we opened for the current track (we own it; MP3 path) */
    int media_io_error;     /* read/seek failed; never treat this as book EOF */
    int media_missing;      /* current SD media disappeared during playback */
    void *pcm;
    long rate;
    int channels;

    /* Format dispatch: DEC_MP3 uses the minimp3 fields above; DEC_AAC uses the
     * mp4/fdk-aac fields below. track_open is the format-agnostic "a track is
     * loaded" flag (replaces dec_open in the thread loop / cmd_stop). */
    int dec_fmt;            /* DEC_MP3 or DEC_AAC */
    int track_open;         /* a track is loaded (either format) */
    mp4_audio_t mp4;        /* M4B demux state (AAC path; owns its own fd) */
    void *aac;              /* aacDecoder handle (AAC path) */
    uint32_t aac_sample;    /* next AAC frame index to read */
    int aac_frame_size;     /* samples per decoded frame (from GetStreamInfo) */
    int aac_need_intr;      /* first decode_step should signal AACDEC_INTR */
    int16_t aac_pcm[8192];  /* decoded PCM (up to 4096 frames * 2ch) */
    uint8_t aac_frame[8192];/* one raw AAC access unit */

    uint64_t last_save_ms;
    uint64_t last_db_save_ms;
    int saved_book_id;
    int saved_track_ordinal;
    int saved_completed;
    int64_t saved_track_pos_ms;
    int64_t saved_book_elapsed_ms;
    int db_saved_book_id;
    int db_saved_track_ordinal;
    int db_saved_completed;
    int64_t db_saved_track_pos_ms;
    int64_t db_saved_book_elapsed_ms;
    char cur_title[256];

    int64_t sleep_deadline_ms;   /* monotonic ms deadline, 0 = no sleep timer */

    volatile int speed_permille; /* playback speed, 1000 = 1.0x. Read by the
                                  * decode loop each chunk; set via
                                  * player_set_speed (int read = atomic on MIPS,
                                  * no torn-double worry). */
    int requested_speed_permille; /* UI request, applied by player thread */
    int speed_change_pending;

    /* WSOLA time-stretch state (pitch-preserving speed). Engaged only when
     * speed_permille != 1000; at 1.0x the decode loop passes PCM straight
     * through. Re-inited on track open and on speed change (~48 KB, no malloc). */
    wsola_t wsola;

    /* ALSA mixer (DAC hardware volume). volume_pct is 0..100 (100 = loudest).
     * The CS43131 DAC uses register value 0 = 0 dB (max) and 255 = most
     * attenuated (quietest), so the mixer value is the ATTENUATION, i.e.
     * inverted from the percent. VOL_AT_ZERO_IS_MAX encodes that polarity;
     * flip it if a hardware probe shows the opposite. */
    void *mixer;
    void *mix_left;          /* snd_mixer_elem_t* for "Left Playback Volume" */
    void *mix_right;         /* snd_mixer_elem_t* for "Right Playback Volume" */
    long mix_min, mix_max;   /* mixer value range (0..255) */
    int volume_pct;          /* 0..100, 100 = loudest */
    int wired_volume_pct;    /* persisted DAC volume; independent from BT */
    int mixer_ok;
    int pending_volume_steps; /* accumulated by UI, consumed by player thread */
    int pending_volume_set;   /* -1 or absolute 0..100 */
    int volume_preview_pct;   /* immediate HUD value while work is queued */

    int64_t vol_save_mono_ms;   /* last monotonic ms when we actually saved */
    int   vol_last_saved;        /* persisted pct value (avoid redundant writes) */

    /* Bluetooth output (BlueALSA A2DP). output selects the active sink; the
     * BT mixer (ctl.bluealsa) is opened lazily on first BT output and closed
     * when we switch back to wired. bt_pcm_path is the org.bluealsa PCM path
     * from `bluealsa-cli list-pcms` (logging only; we open the predefined
     * `bluealsa` plug device, which auto-selects the most-recent BT sink). */
    int output;              /* OUT_WIRED or OUT_BT */
    int force_wired;         /* sticky: stay wired for the rest of this track */
    int bt_fell_back;        /* one fallback per track (no close/reopen loop) */
    char bt_pcm_path[128];
    void *bt_mixer;          /* snd_mixer_t* for ctl.bluealsa (NULL if none) */
    void *bt_mix_elem;       /* first playback-volume element in bt_mixer */
    long bt_mix_min, bt_mix_max;
    int bt_volume_pct;       /* BlueALSA/remote level, 0..100 */
    int bt_volume_valid;     /* initialized from the connected speaker */
    int bt_mixer_live;       /* mixer read is trustworthy after our first set */
    int bt_native_hold_released; /* stock BlueALSA socket was displaced */
} g_pl;

/* Set by save_progress (player thread) to tell the UI event loop that the
 * current book's progress changed, so it can refresh the cached progress + home
 * counts WITHOUT the render thread touching the DB. Polled + cleared on the
 * event thread. Plain volatile int — a stale/lost transition just means the UI
 * refreshes one tick later. Defined in ui.c. */
extern volatile int g_progress_dirty;

/* ---- minimp3_ex I/O callbacks (wired to g_pl.track_fd) ------------------ */
/* minimp3_ex reads the file through these; we own the fd. Defined after g_pl
 * so it's in scope. Signature:
 *   size_t read(void *buf, size_t size, void *user_data);
 *   int     seek(uint64_t position, void *user_data);  (absolute SEEK_SET) */
static size_t mp3_io_read(void *buf, size_t size, void *user_data) {
    (void)user_data;
    ssize_t n = read(g_pl.track_fd, buf, size);
    if (n < 0) { g_pl.media_io_error = 1; return 0; }
    return (size_t)n;
}
static int mp3_io_seek(uint64_t position, void *user_data) {
    (void)user_data;
    off_t r = lseek(g_pl.track_fd, (off_t)position, SEEK_SET);
    if (r < 0) g_pl.media_io_error = 1;
    return (r < 0) ? -1 : 0;
}

/* ---- ALSA mixer (hardware volume) -------------------------------------- */

/* CS43131 DAC: mixer value 0 = 0 dB (loudest), 255 = most attenuated (quiet).
 * i.e. the raw value is attenuation, inverted from "percent loud".
 * VOL_AT_ZERO_IS_MAX encodes that polarity; flip to 0 if a hardware probe
 * shows the opposite (higher value = louder). */
#define VOL_AT_ZERO_IS_MAX 1

static long pct_to_mix(int pct) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    long range = g_pl.mix_max - g_pl.mix_min;
#if VOL_AT_ZERO_IS_MAX
    /* 100% -> mix_min (0 = loud), 0% -> mix_max (255 = quiet) */
    return g_pl.mix_min + (long)((100 - pct) * range / 100);
#else
    /* 100% -> mix_max (loud), 0% -> mix_min (quiet) */
    return g_pl.mix_min + (long)(pct * range / 100);
#endif
}

static int mix_to_pct(long mix) {
    long range = g_pl.mix_max - g_pl.mix_min;
    if (range <= 0) range = 255;
    if (mix < g_pl.mix_min) mix = g_pl.mix_min;
    if (mix > g_pl.mix_max) mix = g_pl.mix_max;
#if VOL_AT_ZERO_IS_MAX
    int pct = (int)(100 - (mix - g_pl.mix_min) * 100 / range);
#else
    int pct = (int)((mix - g_pl.mix_min) * 100 / range);
#endif
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

/* Persist volume so it survives app restarts. Kept in /tmp (RAM, tmpfs):
 * /usr/data is chronically full; SD exFAT O_CREAT|O_TRUNC on every volume
 * step stalls the event loop for 10-50 ms under garbage-collection delays,
 * which blocks ALL input events (volume keys are handled on the same thread).
 * /tmp is tmpfs — zero fs overhead, instant writes. Survives within a session;
 * we also keep an SD fallback in case someone reboots mid-listen (rare). */
#define VOL_FILE_RAM  "/tmp/.audiobook_volume"
#define VOL_FILE_SD   "/usr/data/mnt/sd_0/Audiobooks/.audiobook_volume"
#define VOL_FILE_OLD  "/usr/data/.audiobook_volume"  /* pre-SD legacy location */
#define STOCK_USER_INI "/usr/data/user.ini"

/* The stock HiBy volume shown in the launcher is a little-endian u32 at
 * user.ini offset 0x10 (live-mapped on the R1: launcher 30 == file value 30).
 * BlueALSA 4.1.1 initializes some A2DP transports at 127/127 before the first
 * absolute-volume write even when the speaker is actually quiet. This stock
 * value is the safest known baseline for that uninitialized state. */
static int stock_volume_load(void) {
    int fd = open(STOCK_USER_INI, O_RDONLY);
    if (fd < 0) return -1;
    unsigned char b[4];
    if (lseek(fd, 0x10, SEEK_SET) < 0 || read(fd, b, sizeof(b)) != sizeof(b)) {
        close(fd);
        return -1;
    }
    close(fd);
    unsigned int v = (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
                     ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
    return v <= 100 ? (int)v : -1;
}

static int vol_load_saved(void) {
    /* Try /tmp first (RAM, tmpfs — instant). Fall back to SD for post-reboot
     * recovery when the app hasn't been run since reboot. */
    int fd = open(VOL_FILE_RAM, O_RDONLY);
    if (fd < 0) fd = open(VOL_FILE_SD, O_RDONLY);
    if (fd < 0) fd = open(VOL_FILE_OLD, O_RDONLY);  /* legacy */
    if (fd < 0) return -1;
    char b[16]; ssize_t n = read(fd, b, sizeof(b) - 1); close(fd);
    if (n <= 0) return -1;
    b[n] = '\0';
    int v = atoi(b);
    if (v < 0 || v > 100) return -1;
    return v;
}

/* Minimum interval between actual volume saves: 150 ms. Key-repeat from the
 * kernel fires every ~30-70 ms when holding a button; we want to debounce all
 * those down to at most one fs write per 150 ms. Also skip writes when the
 * value hasn't changed (user ramps past it). All volume writes go to /tmp
 * (tmpfs, zero fs overhead) — never SD or UBIFS during playback. */
#define VOL_SAVE_MIN_INTERVAL_MS  150

static uint64_t mono_ms(void);  /* forward decl — vol_save needs it; full def after player_volume_set */

static int vol_save(int pct) {
    uint64_t now = mono_ms();
    if (pct == g_pl.vol_last_saved &&
        now - g_pl.vol_save_mono_ms < VOL_SAVE_MIN_INTERVAL_MS) {
        return 0;  /* skipped — too soon or value unchanged */
    }
    g_pl.vol_save_mono_ms = now;
    g_pl.vol_last_saved = pct;

    /* The input path writes only to tmpfs. Removable-media writes here can
     * block the same event loop that handles volume and play/pause presses. */
    char b[16]; int len = snprintf(b, sizeof(b), "%d\n", pct);

    /* /tmp primary — instant, never blocks the event loop. */
    int fd = open(VOL_FILE_RAM, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, b, len); close(fd); }

    return 0;
}

static void vol_save_sd(void) {
    if (g_pl.wired_volume_pct < 0 || g_pl.wired_volume_pct > 100) return;
    char b[16];
    int len = snprintf(b, sizeof(b), "%d\n", g_pl.wired_volume_pct);
    int fd = open(VOL_FILE_SD, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, b, len); close(fd); }
}

static void mix_apply(void) {
    long v;
    int retries;

    if (g_pl.output == OUT_BT) {
        /* BlueALSA A2DP volume is linear 0=silent..max=loud. */
        if (!g_pl.bt_mixer || !g_pl.bt_mix_elem) return;
        long range = g_pl.bt_mix_max - g_pl.bt_mix_min;
        v = g_pl.bt_mix_min + (long)(g_pl.volume_pct * range / 100);
        retries = 2;
        while (retries-- >= 0) {
            if (x_snd_mixer_selem_set_playback_volume_all(g_pl.bt_mix_elem, v) >= 0) {
                g_pl.bt_mixer_live = 1;
                return;
            }
        }
        return;
    }

    if (!g_pl.mixer_ok) return;
    v = pct_to_mix(g_pl.volume_pct);

    /* Retry mixer calls in case I2C to CS43131 is temporarily blocked.
     * The event loop can stall for 0-45ms on a single mixer call under heavy
     * system load. Retrying up to 2x with short delays prevents silent volume
     * failure (UI shows change, hardware doesn't respond). */
    void *elems[3];
    elems[0] = g_pl.mix_left;
    elems[1] = g_pl.mix_right;
    elems[2] = NULL;

    for (int pass = 0; pass < 3; pass++) {
        int all_done = 1;
        for (int ch = 0; elems[ch]; ch++) {
            if (!elems[ch]) continue;
            long rc = x_snd_mixer_selem_set_playback_volume_all(elems[ch], v);
            if (rc < 0) all_done = 0;
        }
        if (all_done) return; /* All successful -- done */
        usleep(1000); /* 1ms delay between retries for I2C to settle */
    }
}



/* Open the mixer and locate the Left/Right playback volume elements. Called
 * once from player_init. Best-effort: volume buttons no-op if this fails. */
static void mixer_init(void) {
    g_pl.mixer_ok = 0;
    if (!x_snd_mixer_open || !x_snd_mixer_attach || !x_snd_mixer_load ||
        !x_snd_mixer_close || !x_snd_mixer_selem_register ||
        !x_snd_mixer_first_elem || !x_snd_mixer_elem_next ||
        !x_snd_mixer_selem_has_playback_volume ||
        !x_snd_mixer_selem_get_name ||
        !x_snd_mixer_selem_set_playback_volume_all)
        return;
    if (x_snd_mixer_open(&g_pl.mixer, 0) < 0) { g_pl.mixer = NULL; return; }
    if (x_snd_mixer_attach(g_pl.mixer, "default") < 0 ||
        x_snd_mixer_selem_register(g_pl.mixer, NULL, NULL) < 0 ||
        x_snd_mixer_load(g_pl.mixer) < 0) {
        x_snd_mixer_close(g_pl.mixer); g_pl.mixer = NULL; return;
    }
    g_pl.mix_min = 0; g_pl.mix_max = 255;
    for (void *el = x_snd_mixer_first_elem(g_pl.mixer); el;
         el = x_snd_mixer_elem_next(el)) {
        if (!x_snd_mixer_selem_has_playback_volume(el)) continue;
        const char *nm = x_snd_mixer_selem_get_name(el);
        long lo = 0, hi = 0;
        if (x_snd_mixer_selem_get_playback_volume_range)
            x_snd_mixer_selem_get_playback_volume_range(el, &lo, &hi);
        if (!strcmp(nm, "Left"))  { g_pl.mix_left = el;  g_pl.mix_min = lo; g_pl.mix_max = hi; }
        else if (!strcmp(nm, "Right")) { g_pl.mix_right = el; }
    }
    if (!g_pl.mix_left && !g_pl.mix_right) {
        x_snd_mixer_close(g_pl.mixer); g_pl.mixer = NULL; return;
    }
    /* The stock UI and the raw DAC mixer can disagree while music is idle:
     * stock may display 30% while the inactive mixer reads 0 attenuation
     * (100%). Prefer the last volume selected in Audiobooks, then use the raw
     * mixer only on first run. We still do not apply either value until audio
     * starts or the user presses a volume key, so entering the app is silent. */
    int sv = vol_load_saved();
    long current_mix = 0;
    void *read_elem = g_pl.mix_left ? g_pl.mix_left : g_pl.mix_right;
    if (sv < 0 && x_snd_mixer_selem_get_playback_volume && read_elem &&
        x_snd_mixer_selem_get_playback_volume(read_elem, 0, &current_mix) >= 0)
        sv = mix_to_pct(current_mix);
    g_pl.volume_pct = (sv >= 0) ? sv : 60;
    g_pl.wired_volume_pct = g_pl.volume_pct;
    g_pl.mixer_ok = 1;
    plog("mixer ok: Left=%p Right=%p range=%ld..%ld vol=%d%%",
         g_pl.mix_left, g_pl.mix_right, g_pl.mix_min, g_pl.mix_max, g_pl.volume_pct);
}

static void volume_set_apply(int vol) {
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    g_pl.volume_pct = vol;
    if (g_pl.output == OUT_BT) {
        g_pl.bt_volume_pct = vol;
        g_pl.bt_volume_valid = 1;
    } else {
        g_pl.wired_volume_pct = vol;
    }
    /* Diag: measure where a volume press spends time. A multi-second mix or
     * save stall blocks the event thread -> delayed/dead keys (vol + playpause). */
    uint64_t t0 = mono_ms();
    mix_apply();
    uint64_t t1 = mono_ms();
    if (g_pl.output != OUT_BT) vol_save(vol);
    uint64_t t2 = mono_ms();
    plog("vol_set pct=%d out=%d mix=%llums save=%llums", vol, g_pl.output,
         (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1));
}

static void volume_step_apply(int dir) {
    if (g_pl.output == OUT_BT && g_pl.bt_mixer && g_pl.bt_mix_elem) {
        /* BlueALSA is a normal linear 0..max control, unlike the wired DAC's
         * inverted attenuation register. Start from the live mixer value when
         * possible so a speaker-side volume change cannot make our next press
         * jump. About 50 steps end-to-end matches the wired control. */
        long range = g_pl.bt_mix_max - g_pl.bt_mix_min;
        if (range <= 0) range = 127;
        long cur = g_pl.bt_mix_min +
            (long)(g_pl.volume_pct * range / 100);
        if (g_pl.bt_mixer_live && x_snd_mixer_selem_get_playback_volume)
            x_snd_mixer_selem_get_playback_volume(g_pl.bt_mix_elem, 0, &cur);
        long step = (range + 49) / 50;
        if (step < 1) step = 1;
        long next = cur + (dir > 0 ? step : -step);
        if (next < g_pl.bt_mix_min) next = g_pl.bt_mix_min;
        if (next > g_pl.bt_mix_max) next = g_pl.bt_mix_max;
        int pct = (int)((next - g_pl.bt_mix_min) * 100 / range);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        volume_set_apply(pct);
        return;
    }

    /* Step in RAW MIXER UNITS for fine, gradual control. The CS43131 DAC
     * volume register (mix_min..mix_max, 0=loudest) is the actual attenuation
     * control; the old 10%-per-press stepped it ~25 codes at once (~12 dB if
     * the register is 0.5 dB/code) — the "jumps are pretty large" complaint.
     * Stepping the register by a small fixed count gives even, gradual changes
     * whether the register is dB- or amplitude-linear. ~5 units/press
     * (~51 steps end-to-end; ~2-2.5 dB/press at 0.5 dB/code). */
    const long MIX_STEP = 5;
    long cur_mix = pct_to_mix(g_pl.volume_pct);
    long range = g_pl.mix_max - g_pl.mix_min;
    if (range <= 0) range = 255;
    /* up (louder) = DEcrease the register (0=loudest); down = increase it */
    long new_mix = cur_mix + ((dir > 0) ? -MIX_STEP : MIX_STEP);
    if (new_mix < g_pl.mix_min) new_mix = g_pl.mix_min;
    if (new_mix > g_pl.mix_max) new_mix = g_pl.mix_max;
    /* derive pct back (linear reverse of pct_to_mix) for save/display */
#if VOL_AT_ZERO_IS_MAX
    int new_pct = (int)(100 - (new_mix - g_pl.mix_min) * 100 / range);
#else
    int new_pct = (int)((new_mix - g_pl.mix_min) * 100 / range);
#endif
    if (new_pct < 0) new_pct = 0;
    if (new_pct > 100) new_pct = 100;
    volume_set_apply(new_pct);
}

void player_volume_set(int vol) {
    if (!g_pl.thread_alive) return;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    pthread_mutex_lock(&g_pl.mu);
    g_pl.pending_volume_set = vol;
    g_pl.pending_volume_steps = 0;
    g_pl.volume_preview_pct = vol;
    pthread_mutex_unlock(&g_pl.mu);
}

void player_volume_step(int dir) {
    if (!g_pl.thread_alive || dir == 0) return;
    dir = dir > 0 ? 1 : -1;
    pthread_mutex_lock(&g_pl.mu);
    int next = g_pl.pending_volume_steps + dir;
    if (next > 16) next = 16;
    if (next < -16) next = -16;
    g_pl.pending_volume_steps = next;
    /* Immediate visual estimate; the player thread replaces it with the exact
     * mixer result as soon as the queued step is applied. */
    int preview = g_pl.volume_preview_pct + dir * 2;
    if (preview < 0) preview = 0;
    if (preview > 100) preview = 100;
    g_pl.volume_preview_pct = preview;
    pthread_mutex_unlock(&g_pl.mu);
}

/* Time helper for volume debounce + sleep timer checks. */
static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int player_volume(void) {
    if (!g_pl.thread_alive || !g_pl.mixer_ok) return -1;
    pthread_mutex_lock(&g_pl.mu);
    int pct = g_pl.volume_preview_pct;
    pthread_mutex_unlock(&g_pl.mu);
    return pct;
}

/* ---- track listing ----------------------------------------------------- */

static int track_collect_cb(const audiobook_track_t *t, void *ctx) {
    int *n = (int *)ctx;
    if (*n >= 256) return 1;
    ptrack_t *p = &g_pl.tracks[*n];
    p->track_id = t->track_id;
    p->ordinal = t->ordinal;
    strncpy(p->path, t->path, sizeof(p->path) - 1); p->path[sizeof(p->path)-1] = '\0';
    strncpy(p->title, t->title[0] ? t->title : "Track", sizeof(p->title) - 1);
    p->title[sizeof(p->title)-1] = '\0';
    p->duration_ms = t->duration_ms;
    (*n)++;
    return 0;
}

static int load_book_tracks(int book_id) {
    g_pl.track_count = 0;
    audiobook_get_tracks(g_pl.db, book_id, track_collect_cb, &g_pl.track_count);
    return g_pl.track_count;
}

/* ---- SD-primary position store (see posstore.h) ----------------------- */

static void pos_dir_ensure(void) {
    /* Idempotent: ignore the EEXIST case (mkdir returns -1 either way, we
     * don't care which). If the SD is gone or read-only, the open() in
     * pos_save_sd simply fails and position falls back to the library.db
     * mirror — never silently lost mid-save. */
    (void)mkdir(POS_DIR, 0777);
}

void pos_save_sd(int book_id, int track_ordinal, int64_t track_pos_ms,
                 int64_t book_elapsed_ms, int completed) {
    if (book_id <= 0) return;
    pos_dir_ensure();
    char path[160], tmp[160];
    snprintf(path, sizeof path, "%s/%d.pos", POS_DIR, book_id);
    snprintf(tmp,  sizeof tmp,  "%s/%d.pos.tmp", POS_DIR, book_id);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    /* track_ordinal (1-based) + within-track pos + book-elapsed + completed +
     * timestamp. fscanf on load skips whitespace (incl newlines) between
     * fields, so the separator choice doesn't matter. */
    fprintf(f, "%d\n%lld\n%lld\n%d\n%ld\n", track_ordinal,
            (long long)track_pos_ms, (long long)book_elapsed_ms,
            completed ? 1 : 0, (long)time(NULL));
    fclose(f);
    /* Rename within one directory is a single exFAT metadata op, so the .pos
     * is never seen half-written. On rename failure (SD pulled mid-write)
     * drop the tmp and leave the previous .pos intact. */
    if (rename(tmp, path) != 0) unlink(tmp);
}

int pos_load_sd(int book_id, int *track_ordinal, int64_t *track_pos_ms,
                int64_t *book_elapsed_ms, int *completed) {
    if (book_id <= 0) return 0;
    char path[160];
    snprintf(path, sizeof path, "%s/%d.pos", POS_DIR, book_id);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ord = 0, done = 0; long long tp = 0, be = 0; long ts = 0;
    int n = fscanf(f, "%d %lld %lld %d %ld", &ord, &tp, &be, &done, &ts);
    fclose(f);
    if (n < 3) return 0;   /* corrupt/truncated: ignore, fall back to library.db */
    if (track_ordinal)   *track_ordinal   = ord;
    if (track_pos_ms)    *track_pos_ms    = (int64_t)tp;
    if (book_elapsed_ms) *book_elapsed_ms = (int64_t)be;
    if (completed)       *completed       = done;
    return 1;
}

void pos_remove_sd(int book_id) {
    if (book_id <= 0) return;
    char path[160];
    snprintf(path, sizeof path, "%s/%d.pos", POS_DIR, book_id);
    unlink(path);
    snprintf(path, sizeof path, "%s/%d.pos.tmp", POS_DIR, book_id);
    unlink(path);
}

/* ---- progress persistence ---------------------------------------------- */

static void save_progress(int completed, int mirror_db) {
    if (!g_pl.db || g_pl.book_id <= 0 || g_pl.track_count == 0) return;
    int idx = g_pl.track_idx;
    if (idx < 0 || idx >= g_pl.track_count) idx = 0;
    audiobook_progress_t p;
    memset(&p, 0, sizeof(p));
    p.book_id = g_pl.book_id;
    p.track_id = g_pl.tracks[idx].track_id;
    p.track_ordinal = g_pl.tracks[idx].ordinal;
    p.position_ms = g_pl.track_pos_ms;
    p.total_book_elapsed_ms = g_pl.position_ms;
    p.playback_speed = 1.0;
    p.last_played_at = (int)time(NULL);
    p.completed = completed;
    p.completed_at = completed ? (int)time(NULL) : 0;
    p.last_saved_at = (int)time(NULL);
    int position_changed =
        g_pl.saved_book_id != p.book_id
        || g_pl.saved_track_ordinal != p.track_ordinal
        || g_pl.saved_track_pos_ms != p.position_ms
        || g_pl.saved_book_elapsed_ms != p.total_book_elapsed_ms
        || g_pl.saved_completed != p.completed;
    int db_changed =
        g_pl.db_saved_book_id != p.book_id
        || g_pl.db_saved_track_ordinal != p.track_ordinal
        || g_pl.db_saved_track_pos_ms != p.position_ms
        || g_pl.db_saved_book_elapsed_ms != p.total_book_elapsed_ms
        || g_pl.db_saved_completed != p.completed;

    /* The small SD sidecar is authoritative. Avoid rewriting identical exFAT
     * metadata when pause, quit, and stop arrive back-to-back. */
    if (position_changed) {
        pos_save_sd(g_pl.book_id, p.track_ordinal, p.position_ms,
                    p.total_book_elapsed_ms, completed);
        g_pl.saved_book_id = p.book_id;
        g_pl.saved_track_ordinal = p.track_ordinal;
        g_pl.saved_track_pos_ms = p.position_ms;
        g_pl.saved_book_elapsed_ms = p.total_book_elapsed_ms;
        g_pl.saved_completed = p.completed;
    }

    /* SQLite only mirrors list percentages and provides legacy fallback.
     * Keep it off the frequent checkpoint path to avoid journal churn. */
    if (!mirror_db || !db_changed) return;
    /* Free-space guard on the library.db (now on SD). The SD has gigabytes
     * free, but a nearly-full card could still fail a write. Skip the DB
     * mirror when free space is critically low — the SD .pos store is
     * authoritative and the list-view "%" just goes a little stale until
     * space frees up. Threshold mirrors scan.c's SCAN_MIN_FREE_BYTES (1 MB). */
    struct statvfs vfs;
    if (statvfs(AUDIOBOOK_DB_DIR, &vfs) == 0) {
        unsigned long long free_bytes = (unsigned long long)vfs.f_bavail
                                        * (unsigned long long)vfs.f_frsize;
        if (free_bytes < (unsigned long long)(1 * 1024 * 1024)) {
            return;  /* SD already saved; skip the DB mirror to avoid poisoning */
        }
    }
    /* Take the write mutex so we don't collide with a concurrent scan
     * (event thread). exFAT fcntl locks may be no-ops, so this
     * app-level mutex is the real serialization. */
    if (audiobook_db_write_trylock() != 0) {
        plog("save deferred while library refresh is writing");
        return;
    }
    int rc = audiobook_save_progress(g_pl.db, &p);
    audiobook_db_write_unlock();
    if (rc < 0) {
        /* SQLITE_BUSY (a scan holds the writer lock) or SQLITE_FULL
         * (SD full). Non-fatal: SD .pos above is the authoritative
         * copy, and the list-view "%" mirror just goes stale until the next
         * successful save. Don't fail playback over a best-effort mirror. */
        plog("save skipped (db busy/full) — SD .pos authoritative");
    } else {
        g_pl.db_saved_book_id = p.book_id;
        g_pl.db_saved_track_ordinal = p.track_ordinal;
        g_pl.db_saved_track_pos_ms = p.position_ms;
        g_pl.db_saved_book_elapsed_ms = p.total_book_elapsed_ms;
        g_pl.db_saved_completed = p.completed;
        g_pl.last_db_save_ms = mono_ms();
        g_progress_dirty = 1;
    }
}

static void periodic_progress_checkpoint(void) {
    uint64_t now = mono_ms();
    if (now - g_pl.last_save_ms < POSITION_SAVE_INTERVAL_MS) return;
    int mirror_db =
        now - g_pl.last_db_save_ms >= DB_MIRROR_INTERVAL_MS;
    save_progress(0, mirror_db);
    g_pl.last_save_ms = now;
}

/* Add a bookmark at the current playback position. Uses the same track + book
 * math as save_progress. Called from the Now Playing "Mark" button (event
 * thread). `db` is the caller's connection — the UI passes ui->db so the write
 * happens on the event thread and never touches the player thread's g_pl.db. */
int player_add_bookmark(sqlite3 *db, const char *label) {
    player_snapshot_t snap;
    player_get_snapshot(&snap);
    if (!db || snap.book_id <= 0 || snap.track_id <= 0) return -1;
    const char *lab = (label && label[0]) ? label : "Bookmark";
    plog("add bookmark book=%d track=%d pos=%lld bookpos=%lld",
         snap.book_id, snap.track_id, (long long)snap.track_position_ms,
         (long long)snap.position_ms);
    if (audiobook_db_write_trylock() != 0) return -1;
    int id = audiobook_add_bookmark(db, snap.book_id, snap.track_id,
                                    snap.track_position_ms, snap.position_ms, lab);
    audiobook_db_write_unlock();
    return id;
}

/* ---- minimp3_ex / ALSA helpers ----------------------------------------- */

static void close_mh(void) {
    if (g_pl.dec_fmt == DEC_AAC) {
        if (g_pl.aac) { x_aac_Close(g_pl.aac); g_pl.aac = NULL; }
        mp4_audio_close(&g_pl.mp4);
    } else {
        if (g_pl.dec_open) { mp3dec_ex_close(&g_pl.dec); g_pl.dec_open = 0; }
        if (g_pl.track_fd >= 0) { close(g_pl.track_fd); g_pl.track_fd = -1; }
    }
    g_pl.track_open = 0;
}
static void close_pcm(void) {
    if (g_pl.pcm) { x_snd_pcm_drop(g_pl.pcm); x_snd_pcm_close(g_pl.pcm); g_pl.pcm = NULL; }
}

/* ---- Bluetooth (BlueALSA A2DP) ----------------------------------------- */

/* Detect a connected A2DP sink via `bluealsa-cli list-pcms`. Picks the first
 * A2DP *playback* PCM (a2dpsrc/sink = the BT device is the sink/speaker). Stores
 * the org.bluealsa path into bt_pcm_path (logging only — we open the predefined
 * `bluealsa` plug device, which auto-selects the most-recent sink). Returns 1
 * if a sink was found, 0 if none/error. Runs only at track-open / fallback
 * (never in the decode/render loops). popen is bounded by the child's output. */
static int bt_detect(void) {
    g_pl.bt_pcm_path[0] = '\0';
    FILE *p = popen("bluealsa-cli list-pcms 2>/dev/null", "r");
    if (!p) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, "a2dp") && strstr(line, "/sink")) {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' '))
                line[--n] = '\0';
            snprintf(g_pl.bt_pcm_path, sizeof(g_pl.bt_pcm_path), "%s", line);
            found = 1;
            break;
        }
    }
    pclose(p);
    if (found) plog("bt detect: %s", g_pl.bt_pcm_path);
    return found;
}

/* Open ctl.bluealsa and grab the first playback-volume element. Best-effort:
 * on failure volume buttons no-op on BT. */
static void bt_mixer_open(void) {
    if (g_pl.bt_mixer) return;
    if (!x_snd_mixer_open || !x_snd_mixer_attach || !x_snd_mixer_load ||
        !x_snd_mixer_close || !x_snd_mixer_selem_register ||
        !x_snd_mixer_first_elem || !x_snd_mixer_elem_next ||
        !x_snd_mixer_selem_has_playback_volume ||
        !x_snd_mixer_selem_set_playback_volume_all)
        return;
    void *m = NULL;
    if (x_snd_mixer_open(&m, 0) < 0) return;
    if (x_snd_mixer_attach(m, "bluealsa") < 0 ||
        x_snd_mixer_selem_register(m, NULL, NULL) < 0 ||
        x_snd_mixer_load(m) < 0) {
        x_snd_mixer_close(m); return;
    }
    g_pl.bt_mix_min = 0; g_pl.bt_mix_max = 127;
    for (void *el = x_snd_mixer_first_elem(m); el; el = x_snd_mixer_elem_next(el)) {
        if (!x_snd_mixer_selem_has_playback_volume(el)) continue;
        long lo = 0, hi = 0;
        if (x_snd_mixer_selem_get_playback_volume_range)
            x_snd_mixer_selem_get_playback_volume_range(el, &lo, &hi);
        g_pl.bt_mix_elem = el;
        g_pl.bt_mix_min = lo; g_pl.bt_mix_max = hi;
        break;   /* first playback-volume element (element name varies per device) */
    }
    g_pl.bt_mixer = m;
    if (!g_pl.bt_volume_valid && g_pl.bt_mix_elem &&
        x_snd_mixer_selem_get_playback_volume) {
        long current = g_pl.bt_mix_min;
        if (x_snd_mixer_selem_get_playback_volume(g_pl.bt_mix_elem, 0,
                                                   &current) >= 0) {
            long range = g_pl.bt_mix_max - g_pl.bt_mix_min;
            if (range <= 0) range = 127;
            int raw_pct =
                (int)((current - g_pl.bt_mix_min) * 100 / range);
            int stock_pct = stock_volume_load();
            if (current >= g_pl.bt_mix_max && stock_pct >= 0) {
                /* 127/127 before our first write is BlueALSA's uninitialized
                 * cache, not necessarily the speaker's actual level. */
                g_pl.bt_volume_pct = stock_pct;
                g_pl.bt_mixer_live = 0;
                plog("bt volume cache max/uninitialized; stock baseline=%d%%",
                     stock_pct);
            } else {
                g_pl.bt_volume_pct = raw_pct;
                g_pl.bt_mixer_live = 1;
            }
            if (g_pl.bt_volume_pct < 0) g_pl.bt_volume_pct = 0;
            if (g_pl.bt_volume_pct > 100) g_pl.bt_volume_pct = 100;
            g_pl.bt_volume_valid = 1;
        }
    }
    if (g_pl.bt_volume_valid) g_pl.volume_pct = g_pl.bt_volume_pct;
    plog("bt mixer ok elem=%p range=%ld..%ld vol=%d%% valid=%d live=%d",
         g_pl.bt_mix_elem, g_pl.bt_mix_min, g_pl.bt_mix_max,
         g_pl.volume_pct, g_pl.bt_volume_valid, g_pl.bt_mixer_live);
}

static void bt_mixer_close(void) {
    if (g_pl.bt_mixer) {
        x_snd_mixer_close(g_pl.bt_mixer);
        g_pl.bt_mixer = NULL; g_pl.bt_mix_elem = NULL;
    }
    g_pl.bt_mixer_live = 0;
}

/* The stock music engine (the same hiby_player process) plays over BT by
 * opening the bluealsa A2DP sink PCM. It holds the bluealsa slave exclusively,
 * so our snd_pcm_hw_params returns -EBUSY. Since we ARE hiby_player, the stock
 * engine's bluealsa transport socket is in /proc/self/fd. We release it by
 * shutting down every AF_UNIX socket whose SO_PEERCRED peer pid is the bluealsa
 * daemon — that signals EOF to bluealsa, which releases the PCM slot, so our
 * reopen can grab it. The stock audio thread sees EPIPE on its next write and
 * stops (its main thread, which would reopen, is blocked by our hook_b). The
 * reverse transition (handing BT back to stock on exit) is handled by
 * bt_hand_back_to_stock, which does a short A2DP disconnect/reconnect so
 * bluealsa recreates the PCM and stock re-acquires + resumes. */
static pid_t find_bluealsa_pid(void) {
    DIR *d = opendir("/proc");
    if (!d) return -1;
    struct dirent *de; pid_t found = -1;
    while ((de = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        char p[64];
        snprintf(p, sizeof(p), "/proc/%s/comm", de->d_name);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        char buf[64] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        if (buf[n-1] == '\n') buf[n-1] = '\0';
        if (strcmp(buf, "bluealsa") == 0) { found = atoi(de->d_name); break; }
    }
    closedir(d);
    return found;
}

static int bt_release_native_hold(void) {
    pid_t ba = find_bluealsa_pid();
    if (ba <= 0) { plog("bt_release: bluealsa pid not found"); return 0; }
    DIR *d = opendir("/proc/self/fd");
    if (!d) return 0;
    struct dirent *de; int closed_any = 0;
    while ((de = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        int fd = atoi(de->d_name);
        int dom = -1; socklen_t dl = sizeof(dom);
        if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &dom, &dl) != 0) continue;
        if (dom != AF_UNIX) continue;
        /* SO_PEERCRED fills {pid,uid,gid}; struct ucred isn't visible without
         * _GNU_SOURCE, so declare the layout locally. */
        struct { pid_t pid; uid_t uid; gid_t gid; } uc;
        socklen_t ul = sizeof(uc);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &uc, &ul) != 0) continue;
        if (uc.pid == ba) {
            plog("bt_release: shutdown fd %d (peer bluealsa pid %d)", fd, uc.pid);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            closed_any = 1;
        }
    }
    closedir(d);
    if (!closed_any) plog("bt_release: no bluealsa-transport fd found");
    return closed_any;
}

/* Hand the bluealsa A2DP slot back to the stock music engine on audiobook exit.
 * We took it by force on entry (bt_release_native_hold), so on exit the stock
 * engine's bluealsa fd is dead and it won't re-acquire on its own — its UI shows
 * "playing" but no sound. A short A2DP disconnect/reconnect makes bluealsa
 * recreate the PCM and nudges the stock engine to reopen and RESUME the track
 * that was playing, so music plays over BT again after the audiobook exits.
 * Detached (runs after we return to the launcher; a ~2s BT blip + a disconnect
 * notification is the tradeoff). Best-effort: if the addr can't be parsed from
 * bt_pcm_path, it's a no-op (the user can restart music manually). */
static void bt_hand_back_to_stock(void) {
    if (!g_pl.bt_native_hold_released || !g_pl.bt_pcm_path[0])
        return;
    /* bt_pcm_path = /org/bluealsa/hci0/dev_98_52_3D_2C_C2_4C/a2dpsrc/sink
     * → addr 98:52:3D:2C:C2:4C */
    char addr[32] = {0};
    const char *p = strstr(g_pl.bt_pcm_path, "/dev_");
    if (!p) return;
    p += 5;  /* skip "/dev_" */
    int i = 0;
    while (*p && *p != '/' && i < (int)sizeof(addr) - 1) {
        addr[i++] = (*p == '_') ? ':' : *p;
        p++;
    }
    addr[i] = '\0';
    if (i < 17) return;  /* not a valid MAC (xx:xx:xx:xx:xx:xx = 17 chars) */
    /* Hand the BT sink back to stock, left PAUSED (not auto-resumed). Sequence
     * (all detached, so it runs after we return to the launcher):
     *   1. disconnect the A2DP sink, then reconnect it. This makes bluealsa
     *      recreate the A2DP PCM slot. The stock music engine (same
     *      hiby_player process) is left in whatever play/pause state it was
     *      in; empirically, after this disconnect/reconnect stock is PAUSED
     *      (it does NOT auto-resume) and HEALTHY — the user presses play on the
     *      launcher/speaker and stock music resumes from where it was.
     *   We do NOT inject a KEY_PLAYPAUSE here. Earlier builds injected a toggle
     *   to force stock to PAUSED after an assumed auto-resume, but stock does
     *   not auto-resume on reconnect — so the toggle was the only thing
     *   STARTING the music (paused -> playing). Removing it leaves stock paused
     *   (the desired state) and avoids the state-dependent toggle entirely.
     *   Net: music comes back over BT, paused; the user presses play to resume. */
    char cmd[300];
    snprintf(cmd, sizeof(cmd),
        "( bluetoothctl disconnect %s; sleep 1; bluetoothctl connect %s ) "
        ">/dev/null 2>&1 &", addr, addr);
    system(cmd);
    g_pl.bt_native_hold_released = 0;
    plog("bt_hand_back: disconnect/reconnect %s (stock left PAUSED over BT)", addr);
}


/* Open `dev` with the same ~1s retry budget the wired path uses. Returns 1 on
 * success (pcm set), 0 on failure. */
static int open_pcm_retry(const char *dev) {
    for (int attempt = 0; attempt < 5; attempt++) {
        if (x_snd_pcm_open(&g_pl.pcm, dev, A_STREAM_PLAYBACK, 0) >= 0) return 1;
        if (attempt < 4) usleep(200000);  /* 200ms x4 = up to 0.8s */
    }
    return 0;
}

/* Configure ALSA for (rate, channels). Auto-detects BT vs wired on the first
 * open / when output changed since the last track; reconfigures an already-open
 * PCM otherwise. force_wired (set by the mid-playback fallback) forces wired.
 * On BT open or hw_params failure, falls back to wired. Returns 0 on success. */
static int setup_alsa(long rate, int channels) {
    int want_bt = g_pl.force_wired ? 0 : bt_detect();
    int want_out = want_bt ? OUT_BT : OUT_WIRED;

    /* Per-track re-evaluation: if a PCM is open on the *other* output (e.g. BT
     * connected/disconnected between tracks), drop it and reopen on the new
     * one. Leaving the BT mixer open across a switch to wired is wasteful and
     * holds the BT control, so close it too. */
    if (g_pl.pcm && g_pl.output != want_out) {
        close_pcm();
        if (g_pl.output == OUT_BT) bt_mixer_close();
    }

    if (!g_pl.pcm) {
        int opened = 0;
        if (want_out == OUT_BT) {
            opened = open_pcm_retry("bluealsa");
            if (!opened) {
                plog("snd_pcm_open bluealsa failed -> wired fallback");
                g_pl.bt_pcm_path[0] = '\0';
                want_out = OUT_WIRED;
            } else {
                plog("alsa pcm opened (bluealsa)");
            }
        }
        if (!opened) {
            const char *devs[] = { "plughw:0,0", "hw:0,0" };
            for (int d = 0; d < 2 && !opened; d++) {
                opened = open_pcm_retry(devs[d]);
                if (!opened) plog("snd_pcm_open %s failed after retries, trying next", devs[d]);
            }
            if (!opened) { plog("snd_pcm_open failed (all devices)"); return -1; }
            plog("alsa pcm opened (wired)");
        }
    } else {
        x_snd_pcm_drop(g_pl.pcm);
    }
    g_pl.output = want_out;
    if (want_out == OUT_BT) {
        if (!g_pl.bt_mixer) bt_mixer_open();
        else if (g_pl.bt_volume_valid) g_pl.volume_pct = g_pl.bt_volume_pct;
    } else {
        g_pl.volume_pct = g_pl.wired_volume_pct;
    }

    /* Apply hw params. For BT (bluealsa plug) the slave PCM is opened lazily
     * here; if the stock music engine (same hiby_player process) still holds
     * the bluealsa slave from a recent play, snd_pcm_hw_params returns -EBUSY
     * (-16). On the first EBUSY we actively release the stock engine's bluealsa
     * transport (bt_release_native_hold), then retry a few times with a fresh
     * plug open while bluealsa frees the slot. Only fall back to wired if it
     * stays busy. (aplay with these same params succeeds once the slave is
     * free, so this is contention, not a constraint rejection.) */
    int r = -1;
    unsigned long period = 0;
    unsigned int want_rate = (unsigned int)rate;
    int released_hold = 0;
    for (int hp_attempt = 0; hp_attempt < 5; hp_attempt++) {
        if (hp_attempt > 0 && want_out == OUT_BT) {
            /* Reopen the plug so the slave-open retries after the holder frees
             * it. (Just re-issuing hw_params on a failed plug PCM keeps the old
             * slave in error state.) */
            close_pcm();
            if (!open_pcm_retry("bluealsa")) { r = -16; usleep(200000); continue; }
        }
        void *hw = NULL;
        int e_m = x_snd_pcm_hw_params_malloc(&hw);
        if (e_m < 0 || !hw) {
            plog("snd_pcm_hw_params_malloc failed: %d", e_m);
            close_pcm();
            return -1;
        }
        int e_a = x_snd_pcm_hw_params_any(g_pl.pcm, hw);
        int e_ac = x_snd_pcm_hw_params_set_access(g_pl.pcm, hw, A_ACCESS_RW_INT);
        int e_f = x_snd_pcm_hw_params_set_format(g_pl.pcm, hw, A_FMT_S16_LE);
        int e_ch = x_snd_pcm_hw_params_set_channels(g_pl.pcm, hw, (unsigned int)channels);
        want_rate = (unsigned int)rate; int dir = 0;
        int e_r = x_snd_pcm_hw_params_set_rate_near(g_pl.pcm, hw, &want_rate, &dir);
        unsigned int btime = 500000, bdir = 0;   /* 500ms buffer */
        int e_b = x_snd_pcm_hw_params_set_buffer_time_near(g_pl.pcm, hw, &btime, (int *)&bdir);
        unsigned int ptime = 100000, pdir = 0;   /* 100ms period */
        int e_p = x_snd_pcm_hw_params_set_period_time_near(g_pl.pcm, hw, &ptime, (int *)&pdir);
        r = x_snd_pcm_hw_params(g_pl.pcm, hw);
        if (r == 0) {
            int gpdir = 0;
            x_snd_pcm_hw_params_get_period_size(hw, &period, &gpdir);
            x_snd_pcm_hw_params_free(hw);
            break;
        }
        /* DIAG: which set_* failed, or did all succeed and only hw_params reject?
         * <0 = that call errored (params left partial -> hw_params -22). All >=0
         * and r=-22 = constraint commit rejected the buffer/period combo (the
         * buffer-then-period order can leave period_time_near unable to divide
         * the already-committed buffer_size on some slaves). */
        plog("hw_params diag: malloc=%d any=%d acc=%d fmt=%d ch=%d(rate=%ld->%u) rate=%d buf=%d(%u) per=%d(%u) -> hw=%d",
             e_m, e_a, e_ac, e_f, e_ch, rate, want_rate, e_r, e_b, btime, e_p, ptime, r);
        x_snd_pcm_hw_params_free(hw);
        if (want_out != OUT_BT) break;          /* wired: no retry, no fallback */
        if (r != -16 && r != -11) break;         /* only retry EBUSY/EAGAIN */
        /* First EBUSY: the stock music engine is holding bluealsa — release its
         * transport so we can take over. Done once per setup_alsa call. */
        if (!released_hold) {
            close_pcm();  /* drop our (failed) plug first */
            if (bt_release_native_hold()) g_pl.bt_native_hold_released = 1;
            released_hold = 1;
            usleep(400000);  /* give bluealsa a moment to free the slot */
            continue;
        }
        usleep(300000);                          /* 0.3s between further retries */
    }
    if (r < 0) {
        /* A2DP stayed busy or rejected params; fall back to wired once. */
        if (want_out == OUT_BT && !g_pl.force_wired) {
            plog("BT hw_params failed (%d) -> wired fallback", r);
            close_pcm(); bt_mixer_close();
            g_pl.force_wired = 1; g_pl.bt_fell_back = 1;
            return setup_alsa(rate, channels);  /* re-enters with force_wired=1 */
        }
        return -1;
    }

    void *sw = NULL;
    int sw_alloc = x_snd_pcm_sw_params_malloc(&sw);
    if (sw_alloc >= 0 && sw) {
        x_snd_pcm_sw_params_current(g_pl.pcm, sw);
        x_snd_pcm_sw_params_set_start_threshold(g_pl.pcm, sw, 1);
        x_snd_pcm_sw_params_set_avail_min(g_pl.pcm, sw, period);
        r = x_snd_pcm_sw_params(g_pl.pcm, sw);
        if (r < 0) plog("snd_pcm_sw_params failed: %d (non-fatal)", r);
        x_snd_pcm_sw_params_free(sw);
    } else {
        plog("snd_pcm_sw_params_malloc failed: %d (using defaults)", sw_alloc);
    }

    x_snd_pcm_prepare(g_pl.pcm);
    g_pl.rate = want_rate;
    g_pl.channels = channels;
    return 0;
}

/* Write `frames` to the PCM, retrying once on -EPIPE/-EIO. If the retry also
 * fails AND we're on BT (and haven't already fallen back this track), the A2DP
 * transport dropped: close the BT PCM, force wired, reopen, and re-issue the
 * write so playback continues from the next chunk (position is unchanged — we
 * keep track_pos_ms, so this is a brief glitch, not a jump). Used by all three
 * decode write sites (AAC, MP3 wsola, MP3 direct). Returns frames written (>0)
 * or a negative snd_pcm error. */
static long pcm_write_or_fallback(const void *buf, unsigned long frames) {
    long wr = x_snd_pcm_writei(g_pl.pcm, buf, frames);
    if (wr < 0) {
        if (x_snd_pcm_recover) x_snd_pcm_recover(g_pl.pcm, (int)wr, 1);
        else x_snd_pcm_prepare(g_pl.pcm);
        plog("alsa writei %ld -> recover", wr);
        wr = x_snd_pcm_writei(g_pl.pcm, buf, frames);
    }
    if (wr < 0 && g_pl.output == OUT_BT && !g_pl.bt_fell_back) {
        plog("BT write failed (%ld) -> wired fallback", wr);
        close_pcm(); bt_mixer_close();
        g_pl.force_wired = 1; g_pl.bt_fell_back = 1; g_pl.output = OUT_WIRED;
        if (setup_alsa(g_pl.rate, g_pl.channels) == 0) {
            mix_apply();   /* re-apply volume on the wired DAC */
            wr = x_snd_pcm_writei(g_pl.pcm, buf, frames);
        }
    }
    return wr;
}

/* Compute a byte offset for a resume position (used with MP3D_SEEK_TO_BYTE).
 * We never use SEEK_TO_SAMPLE: it builds the FULL frame index (~14.5MB for a
 * 6.6h file + a 193MB scan) → OOM on this 56MB-RAM device.
 *
 * For VBR files with a VBR (Xing/Info) tag, minimp3 sets dec.samples to the
 * TRUE total even with MP3D_DO_NOT_SCAN, so byte = seek_ms * file_size /
 * real_duration_ms is accurate — it uses the file's true average bitrate. The
 * first frame's bitrate_kbps undershoots VBR (this file: 56 kbps first-frame
 * vs ~128 kbps avg) and resumed ~47s early; the file-size/duration estimate
 * fixes that. For CBR / no VBR tag (dec.samples==0), fall back to the first
 * frame's bitrate (byte = seek_ms * bitrate_kbps/8), which is exact for CBR.
 * mp3dec_ex_seek(SEEK_TO_BYTE) then syncs to the next frame after the byte
 * (~26ms accurate) — fine for audiobook resume. */
static uint64_t seek_byte_target(int64_t seek_ms) {
    if (seek_ms <= 0) return 0;
    if (g_pl.dec.samples > 0 && g_pl.channels > 0 && g_pl.rate > 0) {
        struct stat st;
        if (fstat(g_pl.track_fd, &st) == 0 && st.st_size > 0) {
            double real_dur_ms = (double)g_pl.dec.samples
                                 / (double)g_pl.channels
                                 / (double)g_pl.rate * 1000.0;
            if (real_dur_ms > 0)
                return (uint64_t)((double)seek_ms
                                  * (double)st.st_size / real_dur_ms);
        }
    }
    /* CBR / no VBR tag: first-frame bitrate (bytes/ms = bitrate_kbps / 8). */
    return (uint64_t)((double)seek_ms * g_pl.dec.info.bitrate_kbps / 8.0);
}

/* Open an M4B/AAC track: mp4 demux + fdk-aac. Priming-decodes frame 0 to read
 * the true OUTPUT rate/ch from GetStreamInfo (HE-AAC/SBR can double the ASC
 * base rate), then sets up ALSA and positions aac_sample at the resume frame.
 * AAC seek = mp4_audio_seek_sample (stts ms->frame) — no per-sample index, so
 * no OOM risk (unlike the MP3 SEEK_TO_SAMPLE trap). */
static int open_track_aac(int idx, int64_t seek_ms) {
    g_pl.media_io_error = 0;
    g_pl.media_missing = 0;
    /* Fast path: same track already demuxed -> reset the decoder + re-seek.
     * (Re-seeking fdk-aac internally is fiddly; reopening the handle is cheap
     * and keeps the moov parsed in g_pl.mp4, so no ~3MB moov re-read.) */
    if (g_pl.track_open && g_pl.aac && g_pl.track_idx == idx) {
        x_aac_Close(g_pl.aac);
        g_pl.aac = x_aac_Open(AAC_TT_MP4_RAW, 1);
        if (!g_pl.aac) { plog("aac reopen failed"); close_mh(); return -1; }
        uint8_t *asc[1] = { g_pl.mp4.asc };
        unsigned int ascLen[1] = { (unsigned)g_pl.mp4.asc_len };
        x_aac_ConfigRaw(g_pl.aac, asc, ascLen);
        g_pl.aac_sample = (seek_ms > 0) ? mp4_audio_seek_sample(&g_pl.mp4, seek_ms) : 0;
        g_pl.aac_need_intr = 1;  /* fresh decoder: first decode signals restart */
        if (g_pl.pcm) { x_snd_pcm_drop(g_pl.pcm); x_snd_pcm_prepare(g_pl.pcm); }
        int64_t base = 0;
        for (int i = 0; i < idx; i++) base += g_pl.tracks[i].duration_ms;
        g_pl.track_base_ms = base;
        g_pl.track_pos_ms = seek_ms;
        g_pl.position_ms = base + seek_ms;
        plog("re-seek aac track %d @%lld -> sample %u", idx, (long long)seek_ms, g_pl.aac_sample);
        return 0;
    }

    close_mh();
    /* New track: re-evaluate BT vs wired (clear the mid-playback fallback so
     * detection runs again — the fallback was sticky only for the prior track). */
    g_pl.force_wired = 0; g_pl.bt_fell_back = 0;
    if (mp4_audio_open(g_pl.tracks[idx].path, &g_pl.mp4)) {
        plog("mp4_audio_open failed: %s", g_pl.tracks[idx].path);
        return -1;
    }
    g_pl.aac = x_aac_Open(AAC_TT_MP4_RAW, 1);
    if (!g_pl.aac) { plog("aacDecoder_Open failed"); mp4_audio_close(&g_pl.mp4); return -1; }
    uint8_t *asc[1] = { g_pl.mp4.asc };
    unsigned int ascLen[1] = { (unsigned)g_pl.mp4.asc_len };
    x_aac_ConfigRaw(g_pl.aac, asc, ascLen);

    /* Priming: decode up to a few frames until GetStreamInfo returns valid
     * rate/ch. The FIRST decode of a fresh handle (AACDEC_INTR) errors once
     * (priming); GetStreamInfo is only populated after the first SUCCESSFUL
     * decode — so a single priming decode is not enough (that was the
     * "streaminfo invalid" failure on the first flashed build). The loop reads
     * frames 0,1,2,... (intr on the first) and stops once rate/ch are known. */
    long rate = 0; int ch = 0, frameSize = 1024;
    for (int prim = 0; prim < 4 && (rate <= 0 || ch <= 0); prim++) {
        int fsz = mp4_audio_read_sample(&g_pl.mp4, (uint32_t)prim,
                                        g_pl.aac_frame, sizeof(g_pl.aac_frame));
        if (fsz <= 0) break;
        uint8_t *pBuf[1] = { g_pl.aac_frame };
        unsigned int pSize[1] = { (unsigned)fsz };
        unsigned int bv = (unsigned)fsz;
        x_aac_Fill(g_pl.aac, pBuf, pSize, &bv);
        unsigned int flags = (prim == 0) ? AACDEC_INTR : 0u;
        x_aac_DecodeFrame(g_pl.aac, g_pl.aac_pcm,
                          (int)(sizeof(g_pl.aac_pcm) / sizeof(int16_t)), flags);
        CStreamInfo *si = x_aac_GetStreamInfo(g_pl.aac);
        if (si) { rate = si->sampleRate; ch = si->numChannels; frameSize = si->frameSize; }
    }
    if (rate <= 0 || ch <= 0) { plog("aac streaminfo invalid (rate/ch)"); close_mh(); return -1; }
    if (frameSize <= 0) frameSize = 1024;
    g_pl.rate = rate; g_pl.channels = ch; g_pl.aac_frame_size = frameSize;

    if (setup_alsa(rate, ch) < 0) { plog("setup_alsa failed (aac)"); close_mh(); close_pcm(); return -1; }

    /* (Re)init WSOLA for this track's rate/channels + current speed. At 1.0x it
     * stays unused (decode_step_aac passes PCM through), but init is cheap and
     * keeps it ready for a mid-track speed change. */
    wsola_init(&g_pl.wsola, rate, ch, g_pl.speed_permille);

    /* Priming decoded frames 0,1 into the handle to discover streaminfo, leaving
     * a dirty bitreservoir. Re-open a FRESH handle for actual decode so frame 0
     * (or the seek target) starts from a clean state — matches the on-device
     * probe, which used a fresh handle for its seek test. aac_need_intr=1 makes
     * decode_step's first DecodeFrame signal AACDEC_INTR (clean restart). */
    x_aac_Close(g_pl.aac);
    g_pl.aac = x_aac_Open(AAC_TT_MP4_RAW, 1);
    if (!g_pl.aac) { plog("aacDecoder_Open(2nd) failed"); close_mh(); close_pcm(); return -1; }
    {
        uint8_t *a2[1] = { g_pl.mp4.asc };
        unsigned int al2[1] = { (unsigned)g_pl.mp4.asc_len };
        x_aac_ConfigRaw(g_pl.aac, a2, al2);
    }

    g_pl.aac_sample = (seek_ms > 0) ? mp4_audio_seek_sample(&g_pl.mp4, seek_ms) : 0;
    g_pl.aac_need_intr = 1;  /* fresh decoder: first real decode signals restart */
    g_pl.track_open = 1;
    g_pl.track_idx = idx;
    g_pl.track_pos_ms = seek_ms;
    int64_t base = 0;
    for (int i = 0; i < idx; i++) base += g_pl.tracks[i].duration_ms;
    g_pl.track_base_ms = base;
    g_pl.position_ms = base + seek_ms;
    strncpy(g_pl.cur_title, g_pl.tracks[idx].title, sizeof(g_pl.cur_title) - 1);
    g_pl.cur_title[sizeof(g_pl.cur_title) - 1] = '\0';
    plog("opened aac track %d (%s) hz=%ld ch=%d frameSize=%d frames=%u seek=%lld",
         idx, g_pl.tracks[idx].title, rate, ch, frameSize, g_pl.mp4.sample_count,
         (long long)seek_ms);
    return 0;
}

/* Open track idx and seek seek_ms into it. Returns 0 on success. */
static int open_track(int idx, int64_t seek_ms) {
    if (g_pl.dec_fmt == DEC_AAC) return open_track_aac(idx, seek_ms);
    g_pl.media_io_error = 0;
    g_pl.media_missing = 0;
    /* Fast path: same track already open -> just re-seek the live decoder.
     * Byte-level seek (SEEK_TO_BYTE): no frame index, instant, ~0 memory. */
    if (g_pl.dec_open && g_pl.track_fd >= 0 && g_pl.track_idx == idx) {
        uint64_t target = seek_byte_target(seek_ms);
        int sr = mp3dec_ex_seek(&g_pl.dec, target);
        /* On Bluetooth, drop+prepare is NOT enough: after a pause the bluealsa
         * A2DP/LDAC encoder reservoir is left in a stale state, so the
         * restarted stream plays as garbage (verified on-device — wired
         * drop+prepare resumes clean; only a BT pause->restart garbles).
         * Re-open the bluealsa PCM fresh — the same path the first (clean)
         * play used — so the LDAC stream starts clean. setup_alsa only
         * reopens when the output CHANGES, so close_pcm() first to force a
         * fresh open even when staying on BT. Wired keeps the cheap
         * drop+prepare (it resumes clean). */
        if (g_pl.output == OUT_BT) {
            close_pcm();
            bt_mixer_close();
            if (setup_alsa(g_pl.rate, g_pl.channels) < 0) {
                plog("re-seek: bluealsa reopen failed");
                close_mh(); g_pl.state = PLAYER_STOPPED; return -1;
            }
            mix_apply();
        } else if (g_pl.pcm) {
            x_snd_pcm_drop(g_pl.pcm); x_snd_pcm_prepare(g_pl.pcm);
        }
        int64_t base = 0;
        for (int i = 0; i < idx; i++) base += g_pl.tracks[i].duration_ms;
        g_pl.track_base_ms = base;
        g_pl.track_pos_ms = seek_ms;
        g_pl.position_ms = base + seek_ms;
        plog("re-seek track %d @%lld -> %d (rescan skipped)", idx, (long long)seek_ms, sr);
        return 0;
    }

    close_mh();
    /* New track: re-evaluate BT vs wired (clear the mid-playback fallback so
     * detection runs again — the fallback was sticky only for the prior track). */
    g_pl.force_wired = 0; g_pl.bt_fell_back = 0;
    g_pl.track_fd = open(g_pl.tracks[idx].path, O_RDONLY);
    if (g_pl.track_fd < 0) { plog("open failed: %s", g_pl.tracks[idx].path); return -1; }
    memset(&g_pl.dec, 0, sizeof(g_pl.dec));
    memset(&g_pl.io, 0, sizeof(g_pl.io));
    g_pl.io.read = mp3_io_read;
    g_pl.io.seek = mp3_io_seek;
    /* SEEK_TO_BYTE: byte-level seek (NO frame index). SEEK_TO_SAMPLE would
     * build the FULL frame index on the first non-zero seek — ~14.5MB for a
     * 6.6h file (910K frames × 16B) plus a full 193MB scan — which OOMs this
     * 56MB-RAM device (~20MB free, hiby_player ~15MB RSS) and freezes the
     * player. SEEK_TO_BYTE just lseeks to an estimated offset and syncs to
     * the next frame: instant, ~0 memory, ~26ms accurate (fine for resume).
     * DO_NOT_SCAN keeps open fast (duration comes from the tag scanner). */
    int flags = MP3D_SEEK_TO_BYTE | MP3D_DO_NOT_SCAN;
    int r = mp3dec_ex_open_cb(&g_pl.dec, &g_pl.io, flags);
    if (r) { plog("mp3dec_ex_open_cb failed: %d", r); close(g_pl.track_fd); g_pl.track_fd = -1; return -1; }
    long rate = (long)g_pl.dec.info.hz;
    int ch = g_pl.dec.info.channels;
    if (rate <= 0 || ch <= 0) { plog("minimp3 bad format hz=%ld ch=%d", rate, ch); close_mh(); return -1; }
    g_pl.rate = rate; g_pl.channels = ch; g_pl.dec_open = 1; g_pl.track_open = 1;

    if (setup_alsa(rate, ch) < 0) { plog("setup_alsa failed"); close_mh(); close_pcm(); return -1; }

    /* (Re)init WSOLA for this track's rate/channels + current speed (see AAC). */
    wsola_init(&g_pl.wsola, rate, ch, g_pl.speed_permille);

    /* seek to resume position as a byte offset (no index build). */
    if (seek_ms > 0) {
        uint64_t target = seek_byte_target(seek_ms);
        int sr = mp3dec_ex_seek(&g_pl.dec, target);
        plog("seek track %d @%lldms -> byte %llu (dur_via=%s) -> %d",
             idx, (long long)seek_ms, (unsigned long long)target,
             (g_pl.dec.samples > 0 ? "vbr-tag" : "bitrate"), sr);
    }
    g_pl.track_idx = idx;
    g_pl.track_pos_ms = seek_ms;
    int64_t base = 0;
    for (int i = 0; i < idx; i++) base += g_pl.tracks[i].duration_ms;
    g_pl.track_base_ms = base;
    g_pl.position_ms = base + seek_ms;
    strncpy(g_pl.cur_title, g_pl.tracks[idx].title, sizeof(g_pl.cur_title)-1);
    g_pl.cur_title[sizeof(g_pl.cur_title)-1] = '\0';
    plog("opened track %d (%s) hz=%ld ch=%d seek=%lld",
         idx, g_pl.tracks[idx].title, rate, ch, (long long)seek_ms);
    return 0;
}

/* ---- command handling -------------------------------------------------- */

static int book_is_playable(int book_id) {
    if (g_pl.track_count == 0 || g_pl.book_id != book_id) {
        if (load_book_tracks(book_id) <= 0) return 0;
    }
    if (g_pl.track_count == 0) return 0;
    int t = audio_file_type(g_pl.tracks[0].path);
    if (t == AUDIO_EXT_MP3) { g_pl.dec_fmt = DEC_MP3; return 1; }
    /* M4B/M4A are MP4 containers → mp4_audio demux + fdk-aac. (Raw .aac ADTS
     * is NOT handled by the MP4 demux.) Requires the optional fdk-aac lib. */
    if ((t == AUDIO_EXT_M4B || t == AUDIO_EXT_M4A) && g_fdkaac_lib) {
        g_pl.dec_fmt = DEC_AAC; return 1;
    }
    return 0;
}

/* Smart rewind on resume: seek this many ms before the saved position so the
 * listener eases back into context. Only applies to a resume from saved
 * progress (not bookmark/chapter jumps, which are absolute). Clamped at the
 * start of the saved track (no cross-track back-walk; M4B is one long track and
 * MP3 files are long, so this rarely matters). */
#define RESUME_REWIND_MS 5000

static void cmd_play(int book_id, int64_t start_ms) {
    /* Drop a duplicate RESUME of the book that's already playing. This is the
     * close of the resume race: the user taps a book to play (CMD_PLAY -1),
     * but the player thread is still in its 20ms idle usleep so g_pl.state is
     * PLAYER_STOPPED when an AVRCP/keypress arrives in that window — the BT
     * speaker auto-sends an AVRCP PLAY the instant we take the A2DP slot, and
     * player_toggle's STOPPED branch then submits a SECOND CMD_PLAY(-1) for
     * the same book. Both then run: the first resumes at the saved spot, the
     * second re-opens + re-seeks the same track and starts a fresh decode into
     * the SAME bluealsa PCM — two decode streams on one PCM = garbled/doubled
     * audio. (Passing -1 on both already prevented position loss; this guard
     * prevents the audible double-play itself.) Only a resume (-1) of the
     * currently-PLAYING same book is dropped — a seek (start_ms>=0), a play of
     * a different book, or any play while not playing still proceeds. */
    if (start_ms < 0 && g_pl.state == PLAYER_PLAYING && book_id == g_pl.book_id) {
        plog("PLAY book %d @%lld dropped (already playing this book, resume)", book_id, (long long)start_ms);
        return;
    }
    g_pl.book_id = book_id;
    g_pl.last_book = book_id;
    g_pl.fmt_unsupported = 0;
    if (load_book_tracks(book_id) <= 0) { plog("no tracks for book %d", book_id); g_pl.state = PLAYER_STOPPED; return; }
    if (!book_is_playable(book_id)) {
        plog("book %d format unsupported (no fdk-aac or unknown type)", book_id);
        g_pl.fmt_unsupported = 1;
        g_pl.state = PLAYER_STOPPED;
        return;
    }
    audiobook_book_t b;
    g_pl.total_ms = (audiobook_get_book(g_pl.db, book_id, &b) > 0) ? b.total_duration_ms : 0;

    int start_idx = 0;
    int64_t into = 0;
    if (start_ms < 0) {
        /* resume from saved progress. The SD .pos is authoritative (a full
         * /usr/data can never lose it); fall back to library.db only for
         * positions saved by older builds (pre-2.0.9) that wrote library.db
         * alone, so existing listeners keep their place across the upgrade. */
        int sd_ord = 0, sd_done = 0;
        int64_t sd_pos = 0, sd_book = 0;
        if (pos_load_sd(book_id, &sd_ord, &sd_pos, &sd_book, &sd_done) > 0) {
            start_idx = sd_ord - 1;
            if (start_idx < 0) start_idx = 0;
            if (start_idx >= g_pl.track_count) start_idx = g_pl.track_count - 1;
            into = sd_pos;
            if (into < 0) into = 0;
            /* Smart rewind: ease back in a few seconds before the saved spot. */
            if (into > RESUME_REWIND_MS) into -= RESUME_REWIND_MS; else into = 0;
            plog("resume(SD) book %d track %d @%lldms (rewound %dms)",
                 book_id, start_idx, (long long)into, RESUME_REWIND_MS);
        } else {
            audiobook_progress_t p;
            if (audiobook_get_progress(g_pl.db, book_id, &p) > 0) {
                start_idx = p.track_ordinal - 1;
                if (start_idx < 0) start_idx = 0;
                if (start_idx >= g_pl.track_count) start_idx = g_pl.track_count - 1;
                into = p.position_ms;
                if (into < 0) into = 0;
                if (into > RESUME_REWIND_MS) into -= RESUME_REWIND_MS; else into = 0;
                plog("resume(db) book %d track %d @%lldms (rewound %dms)",
                     book_id, start_idx, (long long)into, RESUME_REWIND_MS);
            }
        }
    } else {
        /* absolute book position: find the track containing it */
        int64_t acc = 0;
        for (int i = 0; i < g_pl.track_count; i++) {
            int64_t d = g_pl.tracks[i].duration_ms;
            if (start_ms < acc + d || i == g_pl.track_count - 1) {
                start_idx = i; into = start_ms - acc;
                if (into < 0) into = 0;
                break;
            }
            acc += d;
        }
    }
    if (start_idx >= g_pl.track_count) start_idx = 0;

    if (open_track(start_idx, into) < 0) { g_pl.state = PLAYER_STOPPED; return; }
    g_pl.state = PLAYER_PLAYING;
    g_pl.last_save_ms = mono_ms();
    g_pl.last_db_save_ms = g_pl.last_save_ms;
    plog("PLAY book %d @%lld total=%lldms", book_id, (long long)start_ms, (long long)g_pl.total_ms);
}

static void cmd_resume(void) {
    if (g_pl.state != PLAYER_PAUSED) return;

    /* BlueALSA's plug/encoder state is not clean after snd_pcm_drop(). A plain
     * prepare resumes with garbled audio on the A2DP sink, the same failure we
     * already handle in the in-place seek path. Pause closes the BT PCM, so
     * reopen and configure a fresh stream here while leaving the decoder at
     * the exact paused sample. Wired output keeps the cheap prepare path. */
    if (!g_pl.pcm) {
        if (setup_alsa(g_pl.rate, g_pl.channels) < 0) {
            plog("RESUME output reopen failed @%lldms",
                 (long long)g_pl.position_ms);
            return;  /* remain paused; a later press can retry */
        }
        mix_apply();
        plog("RESUME output reopened (%s)",
             g_pl.output == OUT_BT ? "bluealsa" : "wired");
    } else {
        x_snd_pcm_prepare(g_pl.pcm);
    }
    g_pl.state = PLAYER_PLAYING;
    g_pl.last_save_ms = mono_ms();
    plog("RESUME @%lldms", (long long)g_pl.position_ms);
}

static void cmd_pause(void) {
    if (g_pl.state != PLAYER_PLAYING) return;
    if (g_pl.pcm) {
        if (g_pl.output == OUT_BT) {
            /* Fully close the A2DP stream. drop+prepare leaves BlueALSA's
             * encoder/resampler state stale and the next audio is garbled. */
            close_pcm();
            bt_mixer_close();
            plog("PAUSE closed bluealsa PCM for clean resume");
        } else {
            x_snd_pcm_drop(g_pl.pcm);
        }
    }
    g_pl.state = PLAYER_PAUSED;
    save_progress(0, 1);
    plog("PAUSE @%lldms", (long long)g_pl.position_ms);
}

static void cmd_stop(void) {
    if ((g_pl.track_open || g_pl.pcm) && g_pl.book_id > 0)
        save_progress(0, 1);
    close_mh(); close_pcm();
    g_pl.state = PLAYER_STOPPED;
    plog("STOP");
}

static void cmd_seek(int64_t ms) {
    if (g_pl.track_count == 0 || ms < 0) return;
    int64_t acc = 0; int idx = 0; int64_t into = 0;
    for (int i = 0; i < g_pl.track_count; i++) {
        int64_t d = g_pl.tracks[i].duration_ms;
        if (ms < acc + d || i == g_pl.track_count - 1) { idx = i; into = ms - acc; if (into < 0) into = 0; break; }
        acc += d;
    }
    if (open_track(idx, into) < 0) { g_pl.state = PLAYER_STOPPED; return; }
    g_pl.state = PLAYER_PLAYING;
    plog("SEEK %lldms -> track %d @%lld", (long long)ms, idx, (long long)into);
}

/* Row-1 skip buttons: relative time skip within the current book.
 *   dir > 0  -> fast-forward SKIP_FF_MS (Next, +60s)
 *   dir <= 0 -> rewind      SKIP_RW_MS (Prev, -30s)
 * Computed against the current book-elapsed position, clamped to [0, total_ms],
 * then handed to cmd_seek which resolves book-ms -> (track, into) and re-opens.
 * Replaces the old track-skip behavior (Next = next file, Prev = previous file
 * or restart track 0), which was useless on single-file books and offered no
 * quick "I missed that sentence" jump. Runs on the player thread; position_ms
 * and total_ms are written on this same thread (decode_step / open_book), so no
 * mutex is needed here -- same as cmd_seek. */
#define SKIP_FF_MS  60000   /* Next = +60s */
#define SKIP_RW_MS  30000   /* Prev = -30s */
static void cmd_skip(int dir) {
    if (g_pl.track_count == 0) return;
    int64_t delta = (dir > 0) ? SKIP_FF_MS : -SKIP_RW_MS;
    int64_t target = g_pl.position_ms + delta;
    if (target < 0) target = 0;
    /* Guard total_ms > 0 so a (defensive) unknown-duration book still skips
     * forward instead of clamping to 0. Skip past the end lands at total and
     * the decode loop hits natural EOF -> book finishes. */
    if (g_pl.total_ms > 0 && target > g_pl.total_ms) target = g_pl.total_ms;
    plog("SKIP %s%lldms -> %lld (pos=%lld total=%lld)",
         dir > 0 ? "+" : "-", (long long)delta, (long long)target,
         (long long)g_pl.position_ms, (long long)g_pl.total_ms);
    cmd_seek(target);   /* sets state = PLAYER_PLAYING; re-opens track */
}

/* ---- decode one chunk -------------------------------------------------- */

/* mp3d_sample_t is int16_t (S16). Buffer holds PCM samples (interleaved
 * across channels); ALSA writei takes frames = samples / channels. */
#define PCM_BUF_SAMPLES 16384

/* NO SOFTWARE GAIN — clean passthrough (PROVEN 2026-07-18).
 *
 * History: audiobooks are mastered ~10-14 dB quieter than music, and this
 * device's DAC mixer ("Left/Right Playback Volume", 0=loudest) can only
 * ATTENUATE, so the no-gain decode is "very quiet" at hw max. Three attempts
 * to add loudness in software ALL produced "too-loud" distortion that got
 * WORSE with more gain (tanh +11 dB -> brick-wall limiter +11 dB -> parabolic
 * speech-level boost AB_GAIN 4.5). The distortion scaling with gain, audible
 * as clipping, is the device's ANALOG output stage railing — the quiet source
 * sits ~-18 dBFS (probe RMS 3852); pushing the average level up drives the amp
 * past its rail. No digital nonlinearity fixes that. The Output Port Switch
 * (numid=7, 0-5) gives NO extra clean headroom: only port 0 produces sound,
 * ports 1-5 are silent. So the only CLEAN path is to NOT push the level:
 * passthrough, accept "very quiet", use max hardware volume. The no-gain
 * build (d4c7ee3d) was confirmed clean (user: "very quiet", no distortion).
 * This build = that clean passthrough + the accurate VBR resume fix
 * (seek_byte_target) the no-gain backup lacked. If louder is ever required,
 * the fix must be analog (a higher-gain output / external amp), not software.
 *
 * IMPORTANT: do NOT change the Output Port Switch (numid=7) during playback —
 * it drops the CS43131's I2S lock and returning to 0 does NOT re-lock it (the
 * open PCM stream never restarts). Recovery = pause then resume (re-prepares
 * the ALSA stream) or stop+play (re-opens it). */

/* ---- playback speed (WSOLA time-stretch, pitch preserved) --------------- */

void player_set_speed(int permille) {
    if (!g_pl.thread_alive) return;
    if (permille < 800) permille = 800;
    if (permille > 2000) permille = 2000;
    pthread_mutex_lock(&g_pl.mu);
    g_pl.requested_speed_permille = permille;
    g_pl.speed_change_pending = 1;
    pthread_mutex_unlock(&g_pl.mu);
}

int player_get_speed(void) {
    if (!g_pl.thread_alive)
        return g_pl.speed_permille ? g_pl.speed_permille : 1000;
    int s;
    pthread_mutex_lock(&g_pl.mu);
    s = g_pl.speed_change_pending ? g_pl.requested_speed_permille
                                  : g_pl.speed_permille;
    pthread_mutex_unlock(&g_pl.mu);
    return s ? s : 1000;
}

static int current_media_lost(void) {
    if (g_pl.media_io_error) return 1;
    if (g_pl.track_idx < 0 || g_pl.track_idx >= g_pl.track_count) return 0;
    return access(g_pl.tracks[g_pl.track_idx].path, R_OK) != 0;
}

/* A vanished SD card can look exactly like EOF to a streaming decoder. Keep
 * the most recently saved position authoritative and never mark the book
 * complete in that case. The next explicit Play reopens the media normally
 * after the card is available again. */
static void stop_for_media_loss(void) {
    plog("media unavailable during playback; preserving saved progress");
    g_pl.media_missing = 1;
    close_mh();
    close_pcm();
    g_pl.state = PLAYER_STOPPED;
}

static void decode_step_aac(void) {
    if (g_pl.aac_sample >= g_pl.mp4.sample_count) {
        if (current_media_lost()) { stop_for_media_loss(); return; }
        int idx = g_pl.track_idx + 1;
        if (idx >= g_pl.track_count) {
            plog("book finished (aac)");
            save_progress(1, 1);
            close_mh(); close_pcm();
            g_pl.state = PLAYER_STOPPED;
            return;
        }
        if (open_track(idx, 0) < 0) g_pl.state = PLAYER_STOPPED;
        return;
    }
    int fsz = mp4_audio_read_sample(&g_pl.mp4, g_pl.aac_sample,
                                    g_pl.aac_frame, sizeof(g_pl.aac_frame));
    if (fsz <= 0) {
        if (current_media_lost()) { stop_for_media_loss(); return; }
        /* A valid short read at the boundary is treated as end of track. */
        plog("aac read_sample(%u) -> %d", g_pl.aac_sample, fsz);
        int idx = g_pl.track_idx + 1;
        if (idx >= g_pl.track_count) {
            save_progress(1, 1); close_mh(); close_pcm();
            g_pl.state = PLAYER_STOPPED; return;
        }
        if (open_track(idx, 0) < 0) g_pl.state = PLAYER_STOPPED;
        return;
    }
    uint8_t *pBuf[1] = { g_pl.aac_frame };
    unsigned int pSize[1] = { (unsigned)fsz };
    unsigned int bv = (unsigned)fsz;
    x_aac_Fill(g_pl.aac, pBuf, pSize, &bv);
    unsigned int flags = g_pl.aac_need_intr ? AACDEC_INTR : 0u;
    AAC_DECODER_ERROR e = x_aac_DecodeFrame(g_pl.aac, g_pl.aac_pcm,
                                            (int)(sizeof(g_pl.aac_pcm) / sizeof(int16_t)), flags);
    g_pl.aac_need_intr = 0;
    g_pl.aac_sample++;
    if (e) return;  /* priming/sync error: skip, no ALSA write, no position advance */

    int frameSize = g_pl.aac_frame_size;
    if (frameSize <= 0) frameSize = 1024;
    int ch = g_pl.channels; if (ch < 1) ch = 1;
    int spd = g_pl.speed_permille;

    /* Speed: at 1.0x, clean passthrough (no gain — see the analog-clipping note
     * above decode_step). At other speeds, WSOLA time-stretches (pitch
     * PRESERVED, unlike the old linear resampler): feed the decoded frame, then
     * drain time-stretched PCM to ALSA in a loop. Position advances by CONTENT
     * time (output written * speed), so book-elapsed position stays correct at
     * any speed. The decode thread outruns realtime while WSOLA primes (~2
     * frames), so the ~40 ms latency does not underrun ALSA. */
    long wr_total = 0;
    if (spd && spd != 1000) {
        static short rsbuf[8192];
        wsola_feed(&g_pl.wsola, g_pl.aac_pcm, frameSize);
        for (;;) {
            int of = wsola_drain(&g_pl.wsola, rsbuf,
                                 (int)(sizeof(rsbuf) / sizeof(short) / ch));
            if (of <= 0) break;
            long wr = pcm_write_or_fallback(rsbuf, (unsigned long)of);
            if (wr > 0) wr_total += wr;
        }
    } else {
        long wr = pcm_write_or_fallback(g_pl.aac_pcm, (unsigned long)frameSize);
        if (wr > 0) wr_total = wr;
    }
    if (wr_total > 0) {
        double speed = (spd && spd != 1000) ? (double)spd / 1000.0 : 1.0;
        int64_t content_frames = (int64_t)((double)wr_total * speed + 0.5);
        int64_t advanced = content_frames * 1000 / g_pl.rate;
        g_pl.track_pos_ms += advanced;
        g_pl.position_ms = g_pl.track_base_ms + g_pl.track_pos_ms;
    }
    periodic_progress_checkpoint();
}

static void decode_step(void) {
    if (g_pl.dec_fmt == DEC_AAC) { decode_step_aac(); return; }
    /* MP3 path */
    static short buf[PCM_BUF_SAMPLES];
    size_t got = mp3dec_ex_read(&g_pl.dec, buf, PCM_BUF_SAMPLES);
    if (got == 0) {
        if (current_media_lost()) { stop_for_media_loss(); return; }
        /* track ended -> next */
        int idx = g_pl.track_idx + 1;
        if (idx >= g_pl.track_count) {
            plog("book finished");
            save_progress(1, 1);
            close_mh(); close_pcm();
            g_pl.state = PLAYER_STOPPED;
            return;
        }
        if (open_track(idx, 0) < 0) { g_pl.state = PLAYER_STOPPED; }
        return;
    }

    int ch = g_pl.channels;
    unsigned long frames = (ch > 0) ? (unsigned long)(got / ch) : 0;
    if (frames == 0) return;
    int spd = g_pl.speed_permille;

    /* Clean passthrough at 1.0x: write the decoded S16 straight to ALSA. No
     * gain, no limiting — see the comment above decode_step for why any digital
     * gain clips the analog output stage. buf is int16 from minimp3 (already in
     * [-32768,32767], no overflow possible).
     *
     * Speed != 1.0x: WSOLA time-stretches (pitch PRESERVED, unlike the old
     * linear resampler) — feed the decoded chunk, drain to ALSA in a loop.
     * Position advances by CONTENT time (output written * speed), so
     * book-elapsed position is correct at any speed. */
    long wr_total = 0;
    if (spd && spd != 1000) {
        static short rsbuf[PCM_BUF_SAMPLES];
        /* Feed the decoded chunk in sub-chunks, draining after each, so the
         * WSOLA input ring never has to hold a whole 16K-sample MP3 chunk at
         * once. Feeding the whole chunk in one wsola_feed would overflow the
         * ring and drop content (audible as cut-off speech at 1.5x). SUB is
         * well under the ring capacity minus the max `need` look-ahead. */
        const int SUB = 4096;
        int fed = 0;
        while (fed < (int)frames) {
            int n = (int)frames - fed;
            if (n > SUB) n = SUB;
            wsola_feed(&g_pl.wsola, buf + fed * ch, n);
            fed += n;
            for (;;) {
                int of = wsola_drain(&g_pl.wsola, rsbuf,
                                     (int)(sizeof(rsbuf) / sizeof(short) / ch));
                if (of <= 0) break;
                long wr = pcm_write_or_fallback(rsbuf, (unsigned long)of);
                if (wr > 0) wr_total += wr;
            }
        }
    } else {
        long wr = pcm_write_or_fallback(buf, frames);
        if (wr > 0) wr_total = wr;
    }
    if (wr_total > 0) {
        double speed = (spd && spd != 1000) ? (double)spd / 1000.0 : 1.0;
        int64_t content_frames = (int64_t)((double)wr_total * speed + 0.5);
        int64_t advanced = content_frames * 1000 / g_pl.rate;
        g_pl.track_pos_ms += advanced;
        g_pl.position_ms = g_pl.track_base_ms + g_pl.track_pos_ms;
    }

    periodic_progress_checkpoint();
}

/* ---- thread ------------------------------------------------------------ */

static void publish_snapshot(void) {
    pthread_mutex_lock(&g_pl.mu);
    g_pl.snapshot.state = g_pl.state;
    g_pl.snapshot.book_id = g_pl.book_id;
    g_pl.snapshot.format_unsupported = g_pl.fmt_unsupported;
    g_pl.snapshot.media_missing = g_pl.media_missing;
    g_pl.snapshot.track_index = g_pl.track_idx;
    g_pl.snapshot.track_id = (g_pl.track_idx >= 0 &&
                              g_pl.track_idx < g_pl.track_count)
        ? g_pl.tracks[g_pl.track_idx].track_id : 0;
    g_pl.snapshot.track_position_ms = g_pl.track_pos_ms;
    g_pl.snapshot.position_ms = g_pl.position_ms;
    g_pl.snapshot.total_ms = g_pl.total_ms;
    strncpy(g_pl.snapshot.track_title, g_pl.cur_title,
            sizeof(g_pl.snapshot.track_title) - 1);
    g_pl.snapshot.track_title[sizeof(g_pl.snapshot.track_title) - 1] = '\0';
    pthread_mutex_unlock(&g_pl.mu);
}

static void *player_thread(void *arg) {
    (void)arg;
    plog("thread start");
    publish_snapshot();
    while (g_pl.running) {
        player_cmd_t pcmd;
        memset(&pcmd, 0, sizeof(pcmd));
        int speed_change = 0, requested_speed = 0;
        int volume_set = -1, volume_steps = 0;
        pthread_mutex_lock(&g_pl.mu);
        if (g_pl.cmd_count > 0) {
            pcmd = g_pl.cmd_queue[g_pl.cmd_head];
            g_pl.cmd_head = (g_pl.cmd_head + 1) % CMD_QUEUE_CAP;
            g_pl.cmd_count--;
        }
        if (g_pl.speed_change_pending) {
            requested_speed = g_pl.requested_speed_permille;
            g_pl.speed_change_pending = 0;
            speed_change = 1;
        }
        volume_set = g_pl.pending_volume_set;
        g_pl.pending_volume_set = -1;
        volume_steps = g_pl.pending_volume_steps;
        g_pl.pending_volume_steps = 0;
        pthread_mutex_unlock(&g_pl.mu);

        if (speed_change) {
            pthread_mutex_lock(&g_pl.mu);
            g_pl.speed_permille = requested_speed;
            pthread_mutex_unlock(&g_pl.mu);
            plog("speed set %d.%03dx", requested_speed / 1000,
                 requested_speed % 1000);
            /* WSOLA and the player DB belong to this thread. Applying the
             * change here avoids racing decode with a UI-thread reinit or
             * sharing the THREADSAFE=2 SQLite connection across threads. */
            if (g_pl.track_open && g_pl.rate > 0 && g_pl.channels > 0)
                wsola_init(&g_pl.wsola, g_pl.rate, g_pl.channels,
                           requested_speed);
            if (g_pl.db) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", requested_speed);
                if (audiobook_db_write_trylock() == 0) {
                    audiobook_set_setting(g_pl.db, "playback_speed", buf);
                    audiobook_db_write_unlock();
                }
            }
        }

        int volume_changed = (volume_set >= 0 || volume_steps != 0);
        if (volume_set >= 0) volume_set_apply(volume_set);
        while (volume_steps != 0) {
            int dir = volume_steps > 0 ? 1 : -1;
            volume_step_apply(dir);
            volume_steps -= dir;
        }
        if (volume_changed) {
            pthread_mutex_lock(&g_pl.mu);
            if (g_pl.pending_volume_set < 0 && g_pl.pending_volume_steps == 0)
                g_pl.volume_preview_pct = g_pl.volume_pct;
            pthread_mutex_unlock(&g_pl.mu);
        }

        switch (pcmd.cmd) {
            case CMD_PLAY:   cmd_play(pcmd.book_id, pcmd.start_ms); break;
            case CMD_RESUME: cmd_resume(); break;
            case CMD_PAUSE:  cmd_pause(); break;
            case CMD_STOP:   cmd_stop(); break;
            case CMD_SEEK:   cmd_seek(pcmd.seek_ms); break;
            case CMD_FF:     cmd_skip(1); break;
            case CMD_RW:     cmd_skip(-1); break;
            case CMD_TOGGLE:
                if (g_pl.state == PLAYER_PLAYING) cmd_pause();
                else if (g_pl.state == PLAYER_PAUSED) cmd_resume();
                else if (g_pl.last_book > 0) cmd_play(g_pl.last_book, -1);
                break;
            case CMD_QUIT:
                /* Persist the exact final position before the thread exits.
                 * Without this, the last place is only as fresh as the most
                 * recent periodic save (throttled to every 15s in decode_step),
                 * so up to 15s is silently lost on every exit — and if the saved
                 * .pos was already missing/stale, the next session resumes ~5s
                 * in and the periodic save then overwrites it with ~0, which
                 * looks like "the resume reset to the beginning". Saving here
                 * makes exit authoritative. Safe: save_progress no-ops when no
                 * book is loaded, and g_pl.db is still open (audiobook_db_close
                 * runs in player_shutdown AFTER this thread exits). */
                save_progress(0, 1);
                g_pl.running = 0;
                break;
        }

        if (!g_pl.running) {
            publish_snapshot();
            break;
        }

        if (g_pl.state == PLAYER_PLAYING && g_pl.track_open && g_pl.pcm) {
            /* Sleep timer: if armed and the deadline has passed, pause and
             * clear. Checked/cleared under the mutex (UI may re-arm concurrently). */
            int sleep_fire = 0;
            pthread_mutex_lock(&g_pl.mu);
            if (g_pl.sleep_deadline_ms > 0 &&
                (int64_t)mono_ms() >= g_pl.sleep_deadline_ms) {
                g_pl.sleep_deadline_ms = 0;
                sleep_fire = 1;
            }
            pthread_mutex_unlock(&g_pl.mu);
            if (sleep_fire) {
                plog("sleep timer expired -> pause");
                cmd_pause();
            } else {
                decode_step();
            }
        } else {
            usleep(20000);
        }
        publish_snapshot();
    }
    cmd_stop();
    publish_snapshot();
    plog("thread exit");
    return NULL;
}

static void submit_full(int cmd, int book_id, int64_t seek_ms,
                        int64_t start_ms) {
    if (!g_pl.thread_alive) return;
    int dropped = 0;
    pthread_mutex_lock(&g_pl.mu);
    if (cmd == CMD_QUIT) {
        /* Shutdown must never wait behind stale user input. */
        g_pl.cmd_head = g_pl.cmd_tail = g_pl.cmd_count = 0;
    } else if (g_pl.cmd_count == CMD_QUEUE_CAP) {
        g_pl.cmd_head = (g_pl.cmd_head + 1) % CMD_QUEUE_CAP;
        g_pl.cmd_count--;
        dropped = 1;
    }
    player_cmd_t *dst = &g_pl.cmd_queue[g_pl.cmd_tail];
    dst->cmd = cmd;
    dst->book_id = book_id;
    dst->seek_ms = seek_ms;
    dst->start_ms = start_ms;
    g_pl.cmd_tail = (g_pl.cmd_tail + 1) % CMD_QUEUE_CAP;
    g_pl.cmd_count++;
    pthread_mutex_unlock(&g_pl.mu);
    if (dropped) plog("command queue full; dropped oldest command");
}

static void submit(int cmd, int book_id, int64_t seek_ms) {
    submit_full(cmd, book_id, seek_ms, 0);
}

static void submit_play(int book_id, int64_t start_ms) {
    submit_full(CMD_PLAY, book_id, 0, start_ms);
}

/* ---- public API -------------------------------------------------------- */

int player_init(void) {
    if (g_pl.thread_alive) return 0;
    memset(&g_pl, 0, sizeof(g_pl));
    g_pl.track_fd = -1;
    g_pl.speed_permille = 1000;
    g_pl.requested_speed_permille = 1000;
    /* Volume debounce init: vol_last_saved=-1 means "first save always writes" */
    g_pl.vol_last_saved = -1;
    /* Open the player's OWN library DB connection. The build is
     * -DSQLITE_THREADSAFE=2, so this connection is touched ONLY by the player
     * thread (the UI/event thread has its own, ui->db). This kills the data
     * race that sharing one sqlite3* across the render/player/event threads
     * caused under THREADSAFE=0. The DB is journaled on SD, so the player's
     * writes never block the UI's reads; the only contention is scan
     * (event-thread write) vs save (player-thread write), handled by a short
     * busy_timeout + skip in save_progress. */
    if (audiobook_db_open(AUDIOBOOK_DB_PATH, &g_pl.db) < 0) {
        plog("player_init: library DB open failed — playback unavailable");
        g_pl.db = NULL;
        return -1;
    }
    /* Short busy_timeout: a scan holds the writer lock for its whole
     * transaction, so a save during a scan would otherwise block the decode
     * loop (audio glitch) for the full 5s default. 300ms gives a genuine
     * contender a fair shot, then save_progress skips (SD .pos is
     * authoritative) — bounded glitch, no data loss. */
    sqlite3_busy_timeout(g_pl.db, 300);
    /* Restore last-used playback speed (persisted by player_set_speed). */
    {
        char sbuf[16];
        if (audiobook_get_setting(g_pl.db, "playback_speed", sbuf, sizeof(sbuf)) == 0) {
            int v = atoi(sbuf);
            if (v >= 800 && v <= 2000) {
                g_pl.speed_permille = v;
                g_pl.requested_speed_permille = v;
            }
        }
    }
    if (load_libs() < 0) {
        if (g_fdkaac_lib) { dlclose(g_fdkaac_lib); g_fdkaac_lib = NULL; }
        if (g_alsa_lib) { dlclose(g_alsa_lib); g_alsa_lib = NULL; }
        audiobook_db_close(g_pl.db); g_pl.db = NULL;
        return -1;
    }
    mixer_init();  /* best-effort; volume buttons no-op if this fails */
    pthread_mutex_init(&g_pl.mu, NULL);
    g_pl.pending_volume_set = -1;
    g_pl.volume_preview_pct = g_pl.volume_pct;
    g_pl.running = 1;
    if (pthread_create(&g_pl.thread, NULL, player_thread, NULL) != 0) {
        plog("pthread_create failed");
        if (g_pl.mixer) { x_snd_mixer_close(g_pl.mixer); g_pl.mixer = NULL; }
        if (g_fdkaac_lib) { dlclose(g_fdkaac_lib); g_fdkaac_lib = NULL; }
        if (g_alsa_lib) { dlclose(g_alsa_lib); g_alsa_lib = NULL; }
        audiobook_db_close(g_pl.db); g_pl.db = NULL;
        pthread_mutex_destroy(&g_pl.mu);
        return -1;
    }
    g_pl.thread_alive = 1;
    return 0;
}

void player_shutdown(void) {
    if (!g_pl.thread_alive) return;
    submit(CMD_QUIT, 0, 0);
    pthread_join(g_pl.thread, NULL);
    g_pl.thread_alive = 0;
    /* Release the current track + ALSA PCM. CRITICAL: CMD_QUIT just sets
     * running=0 and the thread exits with g_pl.pcm still open — if we don't
     * close it here, we LEAK the output device (the wired DAC, or the bluealsa
     * A2DP slot), so the stock music app can't re-acquire it after we exit
     * (the "music won't play again after audiobook" symptom). Must run BEFORE
     * the dlclose()s below: close_pcm uses the alsa dlsyms and close_mh uses
     * the fdk-aac dlsym (AAC path). */
    close_mh();
    close_pcm();
    vol_save_sd();
    if (g_pl.mixer) { x_snd_mixer_close(g_pl.mixer); g_pl.mixer = NULL; }
    bt_mixer_close();
    if (g_alsa_lib) dlclose(g_alsa_lib);
    g_alsa_lib = NULL;
    if (g_fdkaac_lib) dlclose(g_fdkaac_lib);
    g_fdkaac_lib = NULL;
    /* We took the bluealsa slot from the stock music engine by force on entry
     * (bt_release_native_hold), so on exit the stock engine's bluealsa fd is
     * dead and it can't re-acquire on its own. close_pcm() (above) frees the
     * slot, but that alone leaves stock stuck ("playing", no sound). A short
     * A2DP disconnect/reconnect (bt_hand_back_to_stock, detached) makes bluealsa
     * recreate the PCM and nudges stock to reopen + resume the track that was
     * playing — so music plays over BT again after the audiobook exits. The
     * tradeoff is a ~2s BT blip + a disconnect notification + the song resuming
     * (the user can pause it). This is the proven v2.0.8 behavior; the user
     * accepts the auto-resume since stock music can't be manually stopped on
     * this firmware (only paused, which doesn't free the slot). */
    bt_hand_back_to_stock();
    /* The player owns its own DB connection (opened in player_init); close it
     * now that the thread has exited and no one else uses it. (The UI closes
     * its own ui->db separately in ui_run.) */
    audiobook_db_close(g_pl.db);
    g_pl.db = NULL;
    pthread_mutex_destroy(&g_pl.mu);
}

int player_play_book(int book_id, int resume) {
    if (!g_pl.thread_alive) return -1;
    submit_play(book_id, resume ? -1 : 0);
    return 0;
}

int player_play_book_from(int book_id, int64_t book_ms) {
    if (!g_pl.thread_alive) return -1;
    submit_play(book_id, book_ms);
    return 0;
}

void player_toggle(void) {
    if (!g_pl.thread_alive) return;
    submit(CMD_TOGGLE, 0, 0);
    /* STOPPED (e.g. after a track-advance open_track failure, or the user
     * stopped, or the book finished): pressing play/pause RESUMES from the
     * saved SD position — NOT from the beginning. Passing 0 here (start_ms=0)
     * restarted the book from 0 and the periodic save then overwrote the
     * saved place with ~0, which is exactly the "resume reset to the
     * beginning" symptom. -1 = resume from saved progress (cmd_play). */
}

void player_pause(void) {
    if (g_pl.thread_alive) submit(CMD_PAUSE, 0, 0);
}

void player_stop(void) {
    submit(CMD_STOP, 0, 0);
}

void player_seek_book_ms(int64_t ms) {
    if (g_pl.thread_alive) submit(CMD_SEEK, 0, ms);
}

int player_ff(void) {
    if (!g_pl.thread_alive) return -1;
    submit(CMD_FF, 0, 0);
    return 0;
}

int player_rw(void) {
    if (!g_pl.thread_alive) return -1;
    submit(CMD_RW, 0, 0);
    return 0;
}

void player_set_sleep_minutes(int minutes) {
    if (!g_pl.thread_alive) return;
    pthread_mutex_lock(&g_pl.mu);
    g_pl.sleep_deadline_ms = (minutes > 0)
        ? (int64_t)mono_ms() + (int64_t)minutes * 60000
        : 0;
    pthread_mutex_unlock(&g_pl.mu);
    plog("sleep timer set: %d min (deadline=%lld)", minutes,
         (long long)g_pl.sleep_deadline_ms);
}

int64_t player_sleep_remaining_ms(void) {
    if (!g_pl.thread_alive) return -1;
    int64_t dl;
    pthread_mutex_lock(&g_pl.mu);
    dl = g_pl.sleep_deadline_ms;
    pthread_mutex_unlock(&g_pl.mu);
    if (dl <= 0) return -1;
    int64_t rem = dl - (int64_t)mono_ms();
    return rem < 0 ? 0 : rem;
}

void player_get_snapshot(player_snapshot_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!g_pl.thread_alive) return;
    pthread_mutex_lock(&g_pl.mu);
    *out = g_pl.snapshot;
    pthread_mutex_unlock(&g_pl.mu);
}

player_state_t player_state(void) {
    player_snapshot_t s; player_get_snapshot(&s); return s.state;
}
int player_current_book(void) {
    player_snapshot_t s; player_get_snapshot(&s); return s.book_id;
}
int64_t player_position_ms(void) {
    player_snapshot_t s; player_get_snapshot(&s); return s.position_ms;
}
int64_t player_total_ms(void) {
    player_snapshot_t s; player_get_snapshot(&s); return s.total_ms;
}
int player_format_unsupported(void) {
    player_snapshot_t s; player_get_snapshot(&s); return s.format_unsupported;
}
