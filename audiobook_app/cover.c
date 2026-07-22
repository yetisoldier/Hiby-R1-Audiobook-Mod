/* cover.c — cached cover-art loader using libjpeg (dlopen'd) with
 * downscale-on-decode, plus a streaming PNG path (pngdec.c, over dlopen'd
 * libz) for .png covers.
 *
 * The device's covers are large images (2400x2400 is typical). Decoding one at
 * full resolution needs a ~17 MB buffer (w*h*3), but the device has only ~16 MB
 * free while hiby_player runs — that OOMs (stb_image, which decodes 1:1, hit
 * exactly this: "outofmem"). libjpeg's scale_num/scale_denom decodes the JPEG
 * directly to 1/8 (or 1/4 / 1/2) size by skipping DCT coefficients, so the
 * decode buffer stays tiny (300x300x3 ≈ 270 KB; on-device probe measured
 * ~436 kB peak RSS for the whole decode). We then downscale to COVER_PX
 * (180x180) RGB565 and cache that. PNG covers have no scale-on-decode in any
 * library, so pngdec.c streams the IDAT over dlopen'd libz and downsamples
 * row-by-row (~150 KB peak) — same memory discipline as the JPEG path.
 *
 * libjpeg + libz are both dlopen'd OPTIONAL (same pattern as fdk-aac / ALSA):
 * missing → covers just don't render (no crash). Unsupported PNG variants
 * (interlaced, palette, non-8-bit) bail to "no cover", and progressive JPEGs
 * too big for free RAM bail via the memory cap (small/medium progressive JPEGs
 * decode fine) — cover_get returns NULL and the UI skips the cover with no gap.
 *
 * RAM: only the 64.5 KB RGB565 cover is retained (one-book cache); the
 * full-res decode is row-streamed + freed immediately.
 */
#include "cover.h"

#include "pngdec.h"    /* streaming PNG -> RGB565 (libz dlopen'd) */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <dlfcn.h>

#include "jpeglib.h"     /* structs for layout only; we dlsym the real symbols */

#include "library.h"     /* audiobook_get_book + cover_path */

/* ---- RGB888 -> RGB565 --------------------------------------------------- */
static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* ---- single-book cache -------------------------------------------------- */
static int       s_cache_book_id = -1;
static uint16_t *s_cache_buf = NULL;     /* COVER_PX * COVER_PX, malloc'd */

void cover_clear(void) {
    free(s_cache_buf);
    s_cache_buf = NULL;
    s_cache_book_id = -1;
}

/* Forward decl — defined with the thumbnail cache below. */
static void thumb_clear(void);

void cover_shutdown(void) {
    cover_clear();
    thumb_clear();
}

/* ---- libjpeg dlopen bindings (lazy, optional) --------------------------- */
typedef struct jpeg_error_mgr *(*pfn_std_error)(struct jpeg_error_mgr *);
typedef void (*pfn_CreateDecompress)(j_decompress_ptr, int, size_t);
typedef void (*pfn_destroy_decompress)(j_decompress_ptr);
typedef void (*pfn_stdio_src)(j_decompress_ptr, FILE *);
typedef int  (*pfn_read_header)(j_decompress_ptr, boolean);
typedef boolean (*pfn_start_decompress)(j_decompress_ptr);
typedef JDIMENSION (*pfn_read_scanlines)(j_decompress_ptr, JSAMPARRAY, JDIMENSION);
typedef boolean (*pfn_finish_decompress)(j_decompress_ptr);

static void *s_lib = NULL;
static pfn_std_error          x_std_error;
static pfn_CreateDecompress   x_CreateDecompress;
static pfn_destroy_decompress x_destroy_decompress;
static pfn_stdio_src          x_stdio_src;
static pfn_read_header        x_read_header;
static pfn_start_decompress   x_start_decompress;
static pfn_read_scanlines     x_read_scanlines;
static pfn_finish_decompress  x_finish_decompress;
static int s_lib_tried = 0;

static int load_libjpeg(void) {
    if (s_lib_tried) return s_lib != NULL;
    s_lib_tried = 1;
    s_lib = dlopen("//usr/lib/libjpeg.so", RTLD_NOW);
    if (!s_lib) return 0;
    #define LOAD(dst, name) do { dst = (__typeof__(dst))dlsym(s_lib, name); \
        if (!dst) { dlclose(s_lib); s_lib = NULL; return 0; } } while (0)
    LOAD(x_std_error, "jpeg_std_error");
    LOAD(x_CreateDecompress, "jpeg_CreateDecompress");
    LOAD(x_destroy_decompress, "jpeg_destroy_decompress");
    LOAD(x_stdio_src, "jpeg_stdio_src");
    LOAD(x_read_header, "jpeg_read_header");
    LOAD(x_start_decompress, "jpeg_start_decompress");
    LOAD(x_read_scanlines, "jpeg_read_scanlines");
    LOAD(x_finish_decompress, "jpeg_finish_decompress");
    #undef LOAD
    return 1;
}

/* ---- error handling (longjmp instead of the default exit()) ------------- */
/* Per-decode jmp_buf stored in cinfo.client_data so each decode has its OWN
 * recovery target. This makes decode_cover_to re-entrant — safe for the scan-
 * time pre-decode (cover_precache) to coexist with the event-thread thumbnail
 * pre-warm (cover_thumb_prewarm) — and removes the old shared static g_jmp
 * cross-thread longjmp hazard. */
struct cover_err_ctx { jmp_buf jmp; };
static void my_error_exit(j_common_ptr cinfo) {
    /* On error, abort this decode; cover_get returns NULL (no cover). We do
     * NOT call the default error_exit (which calls exit() and would kill
     * hiby_player). */
    longjmp(((struct cover_err_ctx *)cinfo->client_data)->jmp, 1);
}

/* Current free RAM in kB (MemAvailable from /proc/meminfo). Used to cap how
 * much memory libjpeg may allocate when decoding a cover, so a progressive
 * JPEG — which must buffer ALL DCT coefficients (no streaming downscale) and
 * can need ~11 MB for a 2400x2400 cover — never trips the kernel OOM-killer
 * (which would freeze the whole device). Returns a moderate fallback if the
 * file can't be read. */
static long read_mem_avail_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 8192;
    char line[128]; long v = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "MemAvailable:", 13)) { v = atol(line + 13); break; }
    }
    fclose(f);
    return v > 0 ? v : 8192;
}

/* Pick the largest scale_denom (1/2/4/8) whose output is still >= px on the
 * image's larger axis — keeps the decode buffer minimal without blurring
 * (covers smaller than 2*px decode 1:1, then we downscale). */
static int pick_denom(int w, int h, int px) {
    int m = w > h ? w : h;
    if (m >= 8 * px) return 8;
    if (m >= 4 * px) return 4;
    if (m >= 2 * px) return 2;
    return 1;
}

/* Decode cover_path -> px*px RGB565 into caller-provided out (px*px uint16_t).
 * Returns 1 on success, 0 on any failure (out untouched). */
static int decode_cover_to(const char *cover_path, int px, uint16_t *out) {
    if (!load_libjpeg()) return 0;

    FILE *fp = fopen(cover_path, "rb");
    if (!fp) return 0;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct cover_err_ctx ctx;     /* per-decode longjmp target (see my_error_exit) */
    cinfo.err = x_std_error(&jerr);
    jerr.error_exit = my_error_exit;
    cinfo.client_data = &ctx;     /* so any libjpeg error can find THIS decode's jmp */

    if (setjmp(ctx.jmp)) {
        /* Any libjpeg error lands here. Clean up what we can and bail. */
        x_destroy_decompress(&cinfo);
        fclose(fp);
        return 0;
    }

    x_CreateDecompress(&cinfo, JPEG_LIB_VERSION, sizeof(struct jpeg_decompress_struct));
    x_stdio_src(&cinfo, fp);
    x_read_header(&cinfo, TRUE);

    /* Memory cap instead of an unconditional progressive bail. IJG libjpeg
     * (the device's libjpeg 9.x — NOT libjpeg-turbo) must buffer ALL DCT
     * coefficients to decode a progressive JPEG, even with scale_denom
     * downscale. A 2400x2400 cover needs ~11 MB of coefficients; on this 56 MB
     * / ~handful-of-MB-free device that allocation trips the kernel OOM-killer
     * → hiby_player killed → hard freeze. The earlier fix bailed on ALL
     * progressive JPEGs, but most covers are small (300..1000px) and decode
     * fine — that bail dropped covers for ~half the library. Instead, cap
     * libjpeg's own tracked memory to (free RAM - 3 MB margin), clamped to a
     * hard ceiling. libjpeg checks the cap when allocating the coefficient
     * buffer and refuses it (→ our longjmp) BEFORE touching the pages, so a
     * too-big progressive is rejected with no allocation and no OOM-kill. A
     * progressive that fits under the cap renders normally; baseline JPEGs
     * use only a tiny single-pass buffer and are unaffected. Verified
     * on-device: 2400² progressive bails gracefully (avail unchanged), while
     * 300²/500²/1000² progressive and all baseline covers render. */
    {
        long cap = read_mem_avail_kb() - 3072;  /* leave 3 MB for app/player/UI */
        if (cap > 8192) cap = 8192;             /* hard ceiling */
        if (cap < 1024) cap = 1024;             /* always allow small/baseline */
        cinfo.mem->max_memory_to_use = cap * 1024;
    }

    cinfo.scale_num = 1;
    cinfo.scale_denom = pick_denom((int)cinfo.image_width, (int)cinfo.image_height, px);
    cinfo.out_color_space = JCS_RGB;
    x_start_decompress(&cinfo);

    int ow = (int)cinfo.output_width, oh = (int)cinfo.output_height;
    if (ow <= 0 || oh <= 0) longjmp(ctx.jmp, 1);

    JSAMPARRAY rowbuf = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                            JPOOL_IMAGE, (JDIMENSION)(ow * 3), (JDIMENSION)1);
    if (!rowbuf) longjmp(ctx.jmp, 1);

    /* Stream row-by-row; for each source row, fill the dest row(s) that
     * nearest-map to it. */
    while (cinfo.output_scanline < (JDIMENSION)oh) {
        int cur = (int)cinfo.output_scanline;
        x_read_scanlines(&cinfo, rowbuf, 1);
        for (int dy = 0; dy < px; dy++) {
            int sy = dy * oh / px; if (sy >= oh) sy = oh - 1;
            if (sy != cur) continue;
            uint16_t *drow = out + (size_t)dy * px;
            for (int dx = 0; dx < px; dx++) {
                int sx = dx * ow / px; if (sx >= ow) sx = ow - 1;
                uint8_t *p = rowbuf[0] + sx * 3;
                drow[dx] = rgb888_to_565(p[0], p[1], p[2]);
            }
        }
    }
    x_finish_decompress(&cinfo);
    x_destroy_decompress(&cinfo);
    fclose(fp);
    return 1;
}

/* ---- persistent on-SD RGB565 cache -------------------------------------- *
 * The first time a cover is needed we decode it (libjpeg, ~300 kB transient
 * for a baseline JPEG; progressive JPEGs that fit under the memory cap decode
 * too, and ones too big for free RAM bail gracefully via the longjmp above) and
 * write the small px*px RGB565 next to the source as "<cover>.NN.r565".
 * Every later request — across reboots — just reads that file: no libjpeg, no
 * decode transient, no OOM risk, instant. The in-RAM cache (cover_get / thumb
 * LRU) still fronts the blit so the render path never touches the SD card. */

static void build_r565_path(const char *cover_path, int px,
                            char *out, size_t outlen) {
    /* ".../cover.jpg" -> ".../cover.56.r565" (replace the extension). */
    size_t n = strlen(cover_path);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, cover_path, n);
    out[n] = 0;
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;   /* drop extension */
    char suffix[24];
    snprintf(suffix, sizeof(suffix), ".%d.r565", px);
    strncat(out, suffix, outlen - strlen(out) - 1);
}

/* Read a px*px RGB565 file into out (px*px uint16_t). 1 on success. */
static int load_r565(const char *path, int px, uint16_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t want = (size_t)px * px * sizeof(uint16_t);
    size_t got = fread(out, 1, want, f);
    fclose(f);
    return got == want;
}

/* Write a px*px RGB565 buffer to path. Best-effort (errors ignored): the file
 * is an optimization; if the SD is read-only or write fails we just re-decode
 * next time. */
static void save_r565(const char *path, int px, const uint16_t *buf) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, sizeof(uint16_t), (size_t)px * px, f);
    fclose(f);
}

/* Get a px*px RGB565 buffer (malloc'd) for cover_path: load the persistent
 * .r565 from the SD if present, else decode (libjpeg + progressive guard) and
 * save it for next time. Returns NULL if no/undecodable cover. */
static uint16_t *load_or_decode(const char *cover_path, int px) {
    char rpath[600];
    build_r565_path(cover_path, px, rpath, sizeof(rpath));

    uint16_t *out = malloc((size_t)px * px * sizeof(uint16_t));
    if (!out) return NULL;

    if (load_r565(rpath, px, out)) return out;   /* cache hit on SD */

    /* Miss: decode (JPEG via libjpeg, PNG via pngdec), persist, return. */
    const char *dote = strrchr(cover_path, '.');
    int is_png = dote && (!strcmp(dote, ".png") || !strcmp(dote, ".PNG"));
    int ok = is_png ? png_decode_to_rgb565(cover_path, px, out)
                    : decode_cover_to(cover_path, px, out);
    if (!ok) { free(out); return NULL; }
    save_r565(rpath, px, out);
    return out;
}

const uint16_t *cover_get(sqlite3 *db, int book_id) {
    if (book_id <= 0 || !db) return NULL;
    if (s_cache_book_id == book_id) return s_cache_buf;

    /* Drop the old cover first to keep peak RAM flat during the new decode. */
    free(s_cache_buf);
    s_cache_buf = NULL;
    s_cache_book_id = book_id;

    audiobook_book_t b;
    if (audiobook_get_book(db, book_id, &b) <= 0) return NULL;
    if (!b.cover_path[0]) return NULL;

    /* Only attempt image covers we can decode: JPEG (libjpeg) or PNG
     * (pngdec/libz). The extension check keeps an unknown format from wasting
     * a dlopen + failed-decode cycle. */
    const char *dot = strrchr(b.cover_path, '.');
    if (!dot) return NULL;
    if (strcmp(dot, ".jpg") != 0 && strcmp(dot, ".JPG") != 0 &&
        strcmp(dot, ".jpeg") != 0 && strcmp(dot, ".JPEG") != 0 &&
        strcmp(dot, ".png") != 0 && strcmp(dot, ".PNG") != 0)
        return NULL;

    s_cache_buf = load_or_decode(b.cover_path, COVER_PX);
    return s_cache_buf;
}

/* Pre-decode cover_path to px*px RGB565 and persist it as "<cover>.<px>.r565"
 * next to the source on the SD — so the first time the user opens/views a
 * book, cover_get is a cheap load_r565 hit (no libjpeg decode, no transient
 * buffer, no decode stall on the event thread). Best-effort: a non-image path
 * or a decode failure simply leaves no .r565 (a later lazy decode retries).
 * Called from the scanner (event thread) per book; re-entrant with the
 * event-thread thumbnail pre-warm thanks to the per-decode jmp_buf. Returns 1
 * if a usable .r565 now exists (was cached or just decoded), 0 otherwise. */
int cover_precache(const char *cover_path, int px) {
    if (!cover_path || !cover_path[0]) return 0;

    /* Only bother with image covers we can decode (mirror cover_get's check). */
    const char *dot = strrchr(cover_path, '.');
    if (!dot) return 0;
    if (strcmp(dot, ".jpg") != 0 && strcmp(dot, ".JPG") != 0 &&
        strcmp(dot, ".jpeg") != 0 && strcmp(dot, ".JPEG") != 0 &&
        strcmp(dot, ".png") != 0 && strcmp(dot, ".PNG") != 0)
        return 0;

    /* Already cached on SD? Skip the malloc+decode entirely. */
    char rpath[600];
    build_r565_path(cover_path, px, rpath, sizeof(rpath));
    FILE *test = fopen(rpath, "rb");
    if (test) { fclose(test); return 1; }

    /* Miss: decode + persist (save_r565 is the side effect we want). */
    uint16_t *buf = load_or_decode(cover_path, px);
    if (!buf) return 0;
    free(buf);
    return 1;
}

/* ---- small-thumbnail LRU cache (for the list view) ---------------------- */
#define COVER_THUMB_CACHE 16
struct thumb_entry {
    int book_id;          /* -1 = empty slot */
    uint16_t *buf;        /* COVER_THUMB_PX*COVER_THUMB_PX, malloc'd */
    unsigned age;         /* higher = more recently used */
};
static struct thumb_entry s_thumb[COVER_THUMB_CACHE];
static unsigned s_thumb_clock = 0;

/* Cache-only lookup (render-safe: never decodes, never touches the DB). Returns
 * the cached COVER_THUMB_PX RGB565 buffer for book_id, or NULL if not cached.
 * Ages the entry on hit. */
const uint16_t *cover_thumb_cached(int book_id) {
    if (book_id <= 0) return NULL;
    for (int i = 0; i < COVER_THUMB_CACHE; i++) {
        if (s_thumb[i].book_id == book_id && s_thumb[i].buf) {
            s_thumb[i].age = ++s_thumb_clock;
            return s_thumb[i].buf;
        }
    }
    return NULL;
}

/* Load (from SD cache) or decode + persist a thumbnail, and store it in the
 * RAM LRU. Called from the event loop (at most ONE per tick) — NEVER from the
 * render/pan hook, so a slow libjpeg decode or SD read can't block the display.
 * Returns 1 if the thumbnail is now in the RAM cache (already cached, or just
 * decoded/loaded); 0 if it could not be cached (no cover / not JPEG / decode
 * failed — e.g. a progressive JPEG, which the guard bails on). The caller marks
 * a 0-result book_id "failed" so the pre-warm walk advances past it instead of
 * retrying the same undecodable cover every tick (which would starve every book
 * below it). The SD .r565 makes subsequent sessions skip the decode entirely. */
int cover_thumb_prewarm(sqlite3 *db, int book_id) {
    if (book_id <= 0 || !db) return 0;
    if (cover_thumb_cached(book_id)) return 1;   /* already have it in RAM */

    audiobook_book_t b;
    if (audiobook_get_book(db, book_id, &b) <= 0) return 0;
    if (!b.cover_path[0]) return 0;
    const char *dot = strrchr(b.cover_path, '.');
    if (!dot) return 0;
    if (strcmp(dot, ".jpg") != 0 && strcmp(dot, ".JPG") != 0 &&
        strcmp(dot, ".jpeg") != 0 && strcmp(dot, ".JPEG") != 0 &&
        strcmp(dot, ".png") != 0 && strcmp(dot, ".PNG") != 0)
        return 0;

    uint16_t *buf = load_or_decode(b.cover_path, COVER_THUMB_PX);
    if (!buf) return 0;

    /* Find a slot: prefer an empty one (buf == NULL), else evict lowest-age. */
    int slot = -1;
    for (int i = 0; i < COVER_THUMB_CACHE; i++) {
        if (s_thumb[i].buf == NULL) { slot = i; break; }
    }
    if (slot < 0) {
        int oldest = 0;
        for (int i = 1; i < COVER_THUMB_CACHE; i++)
            if (s_thumb[i].age < s_thumb[oldest].age) oldest = i;
        slot = oldest;
        free(s_thumb[slot].buf);
    }
    s_thumb[slot].book_id = book_id;
    s_thumb[slot].buf = buf;
    s_thumb[slot].age = ++s_thumb_clock;
    return 1;
}

/* Free the thumbnail cache (called from cover_shutdown). */
static void thumb_clear(void) {
    for (int i = 0; i < COVER_THUMB_CACHE; i++) {
        free(s_thumb[i].buf);
        s_thumb[i].buf = NULL;
        s_thumb[i].book_id = -1;
        s_thumb[i].age = 0;
    }
}