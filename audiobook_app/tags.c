/* tags.c — minimal audio tag readers.
 *
 * MP3: parses ID3v2.3/.4 frames (TIT2, TPE1, TALB, TCOM, TCON, TRCK, TPOS),
 *       estimates duration from bitrate or Xing header.
 * M4B/M4A: parses QuickTime atoms (moov/mvhd for duration, moov/trak/
 *          mdia/minf/stbl/stsd for codec, moov/trak/mdia/minf/stbl/stts
 *          for sample timing). Chapter atoms (trak.mdia.minf.stbl.stsc +
 *          chap) are counted. Just extracts duration + chapter count.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include "tags.h"
#include "utf8.h"

/* ---- Utilities ---------------------------------------------------------- */

static int read_file_header(const char *path, uint8_t *buf, int buf_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = fread(buf, 1, buf_len, f);
    fclose(f);
    return n;
}

static int get_file_info(const char *path, int64_t *size, int *mtime) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    *size = (int64_t)st.st_size;
    *mtime = (int)st.st_mtime;
    return 0;
}

static void safe_copy(char *dst, int dst_len, const uint8_t *src, int src_len) {
    if (!dst || dst_len <= 0) return;
    if (!src || src_len <= 0) { dst[0] = '\0'; return; }
    /* Boundary-safe: if the fixed buffer cuts a multi-byte UTF-8 sequence,
     * back up to the last complete codepoint. */
    utf8_safe_truncate(dst, dst_len, (const char *)src, src_len);
}

/* ---- ID3v2 parsing ------------------------------------------------------ */

static uint32_t syncsafe_to_uint(const uint8_t *p) {
    return ((uint32_t)(p[0] & 0x7f) << 21) |
           ((uint32_t)(p[1] & 0x7f) << 14) |
           ((uint32_t)(p[2] & 0x7f) << 7) |
           ((uint32_t)(p[3] & 0x7f));
}

static void parse_id3v2_text(const uint8_t *data, int len, int encoding,
                             char *out, int out_len) {
    if (!out || out_len <= 0) return;
    out[0] = '\0';
    if (!data || len <= 0) return;
    /* encoding: 0=ISO-8859-1, 1=UTF-16 w/ BOM, 2=UTF-16BE, 3=UTF-8 */
    if (encoding == 1 || encoding == 2) {
        /* UTF-16 -> UTF-8 (BOM-aware; encoding 2 has no BOM, treat as BE) */
        utf16_to_utf8(data, len, (encoding == 2), out, out_len);
    } else if (encoding == 3) {
        /* UTF-8: copy up to the first NUL, boundary-safe */
        int n = 0;
        while (n < len && data[n] != 0) n++;
        /* Some encoders claim UTF-8 but actually write cp1251. Validate and
         * fall back to cp1251 conversion if the bytes aren't valid UTF-8. */
        if (utf8_is_valid((const char *)data, n)) {
            utf8_safe_truncate(out, out_len, (const char *)data, n);
        } else {
            cp1251_to_utf8(data, n, out, out_len);
        }
    } else {
        /* encoding 0 = ISO-8859-1 per spec, but Russian MP3s in the wild are
         * almost always Windows-1251. Heuristic: if any byte >= 0x80 appears,
         * treat the field as Windows-1251 and convert to UTF-8; otherwise it
         * is plain ASCII and we copy bytes through. A Latin-1 field with
         * accents would mis-decode here — acceptable for the target audience
         * (Russian audiobooks) and only triggers on encoding-0 + high bytes. */
        int has_high = 0, n = 0;
        while (n < len && data[n] != 0) {
            if (data[n] >= 0x80) has_high = 1;
            n++;
        }
        if (has_high)
            cp1251_to_utf8(data, n, out, out_len);
        else
            utf8_safe_truncate(out, out_len, (const char *)data, n);
    }
}

static void parse_id3v2(const uint8_t *buf, int buf_len, audio_tags_t *out) {
    if (buf_len < 10) return;
    if (buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3') return;

    int major = buf[3];
    uint32_t tag_size = syncsafe_to_uint(buf + 6);
    if (tag_size > (uint32_t)(buf_len - 10)) tag_size = buf_len - 10;

    int pos = 10;
    int end = 10 + tag_size;

    while (pos + 10 <= end) {
        /* Frame header: 4-byte ID + 4-byte size + 2-byte flags */
        char id[5] = {0};
        memcpy(id, buf + pos, 4);

        uint32_t frame_size;
        if (major == 4) {
            frame_size = syncsafe_to_uint(buf + pos + 4);
        } else {
            /* v2.3: regular 4-byte big-endian */
            frame_size = ((uint32_t)buf[pos+4] << 24) |
                         ((uint32_t)buf[pos+5] << 16) |
                         ((uint32_t)buf[pos+6] << 8) |
                         ((uint32_t)buf[pos+7]);
        }

        if (frame_size == 0) break;
        int encoding = buf[pos + 10];  /* first byte of frame data = encoding */
        const uint8_t *text_data = buf + pos + 11;
        int text_len = (int)frame_size - 1;
        if (text_len <= 0) {
            pos += 10 + frame_size;
            continue;
        }

        if (strcmp(id, "TIT2") == 0) {
            parse_id3v2_text(text_data, text_len, encoding,
                            out->title, sizeof(out->title));
        } else if (strcmp(id, "TPE1") == 0) {
            parse_id3v2_text(text_data, text_len, encoding,
                            out->artist, sizeof(out->artist));
        } else if (strcmp(id, "TALB") == 0) {
            parse_id3v2_text(text_data, text_len, encoding,
                            out->album, sizeof(out->album));
        } else if (strcmp(id, "TCOM") == 0) {
            parse_id3v2_text(text_data, text_len, encoding,
                            out->composer, sizeof(out->composer));
        } else if (strcmp(id, "TCON") == 0) {
            parse_id3v2_text(text_data, text_len, encoding,
                            out->genre, sizeof(out->genre));
        } else if (strcmp(id, "TRCK") == 0) {
            char num[16] = {0};
            parse_id3v2_text((const uint8_t*)text_data, text_len, encoding,
                            num, sizeof(num));
            /* "n/total" or just "n" */
            out->track_number = atoi(num);
        } else if (strcmp(id, "TPOS") == 0) {
            char num[16] = {0};
            parse_id3v2_text((const uint8_t*)text_data, text_len, encoding,
                            num, sizeof(num));
            out->disc_number = atoi(num);
        }

        pos += 10 + frame_size;
    }
}

/* ---- MP3 duration estimation ------------------------------------------- */

static int find_mp3_bitrate(const uint8_t *buf, int len) {
    /* Find first valid MPEG frame sync (0xFF + E0) */
    for (int i = 0; i < len - 4; i++) {
        if ((buf[i] & 0xFF) != 0xFF) continue;
        if ((buf[i+1] & 0xE0) != 0xE0) continue;

        int version = (buf[i+1] >> 3) & 0x03;  /* MPEG version */
        int layer = (buf[i+1] >> 1) & 0x03;
        int bitrate_idx = (buf[i+2] >> 4) & 0x0F;

        if (bitrate_idx == 0 || bitrate_idx == 15) continue;
        if (layer == 0) continue;

        /* MPEG-1 Layer 3 bitrate table (kbps) */
        static const int br_table[2][3][16] = {
            { /* MPEG-1 */
                {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0}, /* L1 */
                {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},   /* L2 */
                {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}     /* L3 */
            },
            { /* MPEG-2/2.5 */
                {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0},
                {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},
                {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0}
            }
        };

        int tbl_row = (version == 3) ? 0 : 1;  /* 3=MPEG-1, 2=MPEG-2, 0=MPEG-2.5 */
        int tbl_col = (layer == 3) ? 0 : (layer == 2) ? 1 : 2;
        int bitrate = br_table[tbl_row][tbl_col][bitrate_idx] * 1000;
        return bitrate;
    }
    return 128000;  /* default assumption */
}

int64_t audio_estimate_mp3_duration(int64_t file_size, int bitrate) {
    if (bitrate <= 0) bitrate = 128000;
    /* duration_ms = file_size * 8 / bitrate * 1000 = file_size * 8000 / bitrate */
    return (file_size * 8000) / bitrate;
}

/* Find the first valid MPEG frame header in buf[start..end). Returns the
 * index of the 4-byte frame header, or -1. Validates version/layer/bitrate/
 * samplerate against the reserved values so a random 0xFFE? inside an ID3
 * payload doesn't trip a false sync. */
static int find_first_mpeg_frame(const uint8_t *buf, int start, int end) {
    for (int i = start; i + 4 <= end; i++) {
        if ((buf[i] & 0xFF) != 0xFF) continue;
        if ((buf[i+1] & 0xE0) != 0xE0) continue;
        int ver = (buf[i+1] >> 3) & 0x03;
        int layer = (buf[i+1] >> 1) & 0x03;
        int br_idx = (buf[i+2] >> 4) & 0x0F;
        int sr_idx = (buf[i+2] >> 2) & 0x03;
        if (ver == 1) continue;        /* reserved MPEG version */
        if (layer == 0) continue;      /* reserved layer */
        if (br_idx == 0 || br_idx == 15) continue;
        if (sr_idx == 3) continue;     /* reserved samplerate */
        return i;
    }
    return -1;
}

/* Parse an MP3 file's true duration. Seeks past any ID3v2 tag (a large
 * embedded cover can push the first MPEG frame well past the old 64 KB read),
 * finds the first MPEG frame, and uses the Xing/Info or VBRI VBR header for
 * an exact frame-count duration. For CBR / no VBR tag, falls back to the
 * first frame's bitrate (exact for CBR). Returns duration_ms, or 0 if no
 * frame could be parsed. */
static int64_t parse_mp3_duration(const char *path, int64_t file_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    /* ID3v2 size is syncsafe in the first 10 bytes; seek past it (+ a little
     * slack for a footer / padding) so we reach the first MPEG frame. */
    int64_t frame_off = 0;
    uint8_t id3[10];
    if (fread(id3, 1, 10, f) == 10 && id3[0] == 'I' && id3[1] == 'D' &&
        id3[2] == '3') {
        int64_t id3_size = ((int64_t)(id3[6] & 0x7f) << 21) |
                           ((int64_t)(id3[7] & 0x7f) << 14) |
                           ((int64_t)(id3[8] & 0x7f) << 7) |
                           (id3[9] & 0x7f);
        frame_off = 10 + id3_size;
    }
    if (frame_off < 0) frame_off = 0;

    /* Read a chunk that comfortably holds the first frame + its VBR header
     * (the Xing/Info header sits at 4 + side-info, <= 36 bytes in). */
    if (fseeko(f, (off_t)frame_off, SEEK_SET) != 0) { fclose(f); return 0; }
    uint8_t buf[8192];
    int n = (int)fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 4) return 0;

    int fi = find_first_mpeg_frame(buf, 0, n);
    if (fi < 0) return 0;

    int ver = (buf[fi+1] >> 3) & 0x03;     /* 3=MPEG-1, 2=MPEG-2, 0=MPEG-2.5 */
    int layer = (buf[fi+1] >> 1) & 0x03;  /* 3=LI, 2=LII, 1=LIII */
    int sr_idx = (buf[fi+2] >> 2) & 0x03;
    int ch_mode = (buf[fi+3] >> 6) & 0x03;
    int mono = (ch_mode == 3);

    static const int sr_mpeg1[4]  = {44100, 48000, 32000, 0};
    static const int sr_mpeg2[4]  = {22050, 24000, 16000, 0};
    static const int sr_mpeg25[4] = {11025, 12000, 8000, 0};
    int sr = 0;
    if (ver == 3) sr = sr_mpeg1[sr_idx];
    else if (ver == 2) sr = sr_mpeg2[sr_idx];
    else if (ver == 0) sr = sr_mpeg25[sr_idx];
    if (sr <= 0) return 0;

    /* Samples per frame by MPEG version + layer. */
    int spf;
    if (layer == 3) spf = 384;           /* Layer I */
    else if (layer == 2) spf = 1152;       /* Layer II */
    else spf = (ver == 3) ? 1152 : 576;    /* Layer III: 1152 (MPEG-1), 576 (2/2.5) */

    /* Xing/Info header offset = 4 (header) + side info.
     * MPEG-1: 32 stereo / 17 mono; MPEG-2/2.5: 17 stereo / 9 mono. */
    int sideinfo = (ver == 3) ? (mono ? 17 : 32) : (mono ? 9 : 17);
    int xo = fi + 4 + sideinfo;
    if (xo + 12 <= n &&
        (memcmp(buf + xo, "Xing", 4) == 0 || memcmp(buf + xo, "Info", 4) == 0)) {
        uint32_t flags = ((uint32_t)buf[xo+4] << 24) | ((uint32_t)buf[xo+5] << 16) |
                         ((uint32_t)buf[xo+6] << 8) | (uint32_t)buf[xo+7];
        if (flags & 0x01) {  /* frame count present */
            uint32_t frames = ((uint32_t)buf[xo+8] << 24) |
                              ((uint32_t)buf[xo+9] << 16) |
                              ((uint32_t)buf[xo+10] << 8) |
                              (uint32_t)buf[xo+11];
            if (frames > 0)
                return (int64_t)frames * spf * 1000 / sr;
        }
    }

    /* Fraunhofer VBRI: fixed at 4 + 32 (MPEG-1 stereo side info). Layout:
     * "VBRI" + version(2) + delay(2) + quality(2) + bytes(4) + frames(4). */
    int vo = fi + 4 + 32;
    if (vo + 18 <= n && memcmp(buf + vo, "VBRI", 4) == 0) {
        uint32_t frames = ((uint32_t)buf[vo+14] << 24) |
                          ((uint32_t)buf[vo+15] << 16) |
                          ((uint32_t)buf[vo+16] << 8) |
                          (uint32_t)buf[vo+17];
        if (frames > 0)
            return (int64_t)frames * spf * 1000 / sr;
    }

    /* No VBR tag: estimate from the first frame's bitrate (exact for CBR). */
    int bitrate = find_mp3_bitrate(buf, n);
    if (bitrate <= 0) bitrate = 128000;
    return (file_size * 8000) / bitrate;
}

/* ---- M4B/M4A (QuickTime) parsing ---------------------------------------- */

/* Read a 32-bit big-endian value */
static uint32_t qt_read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t qt_read64(const uint8_t *p) {
    return ((uint64_t)qt_read32(p) << 32) | (uint64_t)qt_read32(p + 4);
}

/* Forward declarations: parse_m4b (below) needs these, but they are defined
 * later in the file. read_moov mmaps the moov atom regardless of its file
 * position (so moov-at-end works) and returns its full body; parse_m4b reads
 * mvhd + counts traks from that body instead of recursing with a fixed-size
 * read buffer. The old parse_qt_atoms recursed into containers using the
 * atom's declared end, which for a moov larger than the 256 KB read buffer
 * walked past the allocation and overwrote the real mvhd duration with
 * garbage, leaving every long M4B on a 128 kbps size-estimate fallback. */
static uint8_t *read_moov(const char *path, int64_t *out_len,
                          uint8_t **out_map, size_t *out_map_len);
static int qt_find_child(const uint8_t *buf, int start, int end, uint32_t type,
                         int *body_off, int *body_end);

static void parse_m4b(const char *path, audio_tags_t *out) {
    /* Read the real moov via the mmap helper: it walks top-level atoms so
     * moov-at-end works, and it maps the whole moov so mvhd is parsed from
     * the true atom body (no fixed 256 KB read that over-reads past the
     * buffer on large moovs and corrupts the duration). Falls back to a
     * rough 128 kbps size estimate only if the moov genuinely cannot be
     * read. */
    int64_t moov_len = 0;
    uint8_t *map = NULL;
    size_t map_len = 0;
    uint8_t *moov = read_moov(path, &moov_len, &map, &map_len);

    if (moov) {
        /* mvhd is a direct child of moov and carries the movie duration. */
        int mvhd_b, mvhd_e;
        if (qt_find_child(moov, 0, (int)moov_len, 0x6d766864 /* 'mvhd' */,
                          &mvhd_b, &mvhd_e) &&
            mvhd_e - mvhd_b >= 20) {
            int version = moov[mvhd_b];
            uint32_t timescale = qt_read32(moov + mvhd_b + 12);
            uint64_t duration = 0;
            if (version == 0) {
                duration = qt_read32(moov + mvhd_b + 16);
            } else if (mvhd_e - mvhd_b >= 24) {
                duration = qt_read64(moov + mvhd_b + 20);
            }
            if (timescale > 0)
                out->duration_ms = (int64_t)(duration * 1000 / timescale);
        }

        /* Count trak atoms (direct children of moov): audio + chapter tracks. */
        int off = 0;
        while (off + 8 <= (int)moov_len) {
            uint32_t size = qt_read32(moov + off);
            uint32_t atype = qt_read32(moov + off + 4);
            int64_t asize = (int64_t)size;
            int hs = 8;
            if (size == 1 && off + 16 <= (int)moov_len) {
                asize = (int64_t)qt_read64(moov + off + 8);
                hs = 16;
            } else if (size == 0) {
                asize = (int)moov_len - off;
            }
            if (asize < hs || off + asize > (int)moov_len) break;
            if (atype == 0x7472616b /* 'trak' */) out->embedded_chapters++;
            off += (int)asize;
        }

        if (map) munmap(map, map_len);
        else free(moov);
    }

    /* Fallback: rough M4B ~1MB/min at 128kbps, only if moov/duration absent. */
    if (out->duration_ms == 0) {
        out->duration_ms = (out->file_size * 8000) / 128000;
    }

    /* >1 trak = audio + chapter tracks; first trak is the audio track. */
    if (out->embedded_chapters > 1) {
        out->embedded_chapters--;
    } else {
        out->embedded_chapters = 0;
    }
}

/* ---- M4B chapter parsing (Nero chpl + QuickTime chapter track) ---------- */

/* Map the moov atom's file region into memory (mmap) instead of malloc+read.
 * Only the pages the parser actually dereferences — atom headers + the small
 * chapter-track sample tables (stts/stsz/stco/stsc) — get faulted in from disk;
 * the multi-MB audio sample tables are never touched, so they never fault in.
 * This lets a 15 MB moov parse in a few KB of RAM instead of OOM-thrashing this
 * 56 MB device (the bug: malloc(15MB)+read exceeded ~14 MB free → scan hung).
 *
 * Walks top-level atoms (seeking past mdat) so moov-at-end works too. Returns a
 * pointer to the moov body; the caller munmaps *out_map (size *out_map_len).
 * If mmap is unavailable, falls back to malloc+read for SMALL moovs only (<=
 * MOOV_MALLOC_MAX) and leaves *out_map == NULL so the caller frees the buffer;
 * large moovs return NULL (→ placeholder chapters) so the scan never hangs. */
#define MOOV_MALLOC_MAX (8 * 1024 * 1024)

static uint8_t *read_moov(const char *path, int64_t *out_len,
                          uint8_t **out_map, size_t *out_map_len) {
    *out_map = NULL;
    *out_map_len = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    int64_t off = 0;
    uint8_t hdr[16];
    int64_t moov_off = -1, moov_asize = 0;
    int moov_hdrsz = 8;
    for (;;) {
        if (lseek(fd, off, SEEK_SET) < 0) break;
        if (read(fd, hdr, 8) != 8) break;
        uint32_t size = qt_read32(hdr);
        uint32_t type = qt_read32(hdr + 4);
        int64_t atom_size = (int64_t)size;
        int hdrsz = 8;
        if (size == 1) {  /* 64-bit extended size */
            if (read(fd, hdr + 8, 8) != 8) break;
            atom_size = (int64_t)qt_read64(hdr);
            hdrsz = 16;
        } else if (size == 0) {  /* atom extends to EOF; not moov */
            break;
        }
        if (atom_size < hdrsz) break;
        if (type == 0x6d6f6f76 /* 'moov' */) {
            moov_off = off;
            moov_asize = atom_size;
            moov_hdrsz = hdrsz;
            break;
        }
        off += atom_size;
    }
    if (moov_off < 0) { close(fd); return NULL; }

    int64_t body = moov_asize - moov_hdrsz;
    if (body <= 0) { close(fd); return NULL; }

    /* mmap the moov's file region. The offset must be page-aligned; `skew` is
     * the bytes before the moov in the first mapped page. The fd position is
     * left at the moov body start by the atom walk above (we read hdrsz bytes
     * of the moov header), so the malloc fallback below can read directly. */
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    int64_t map_off = (moov_off / page) * page;
    int64_t skew = moov_off - map_off;
    size_t map_len = (size_t)(moov_asize + skew);

    void *map = mmap(NULL, map_len, PROT_READ, MAP_PRIVATE, fd, map_off);
    if (map != MAP_FAILED) {
        close(fd);
        *out_map = (uint8_t *)map;
        *out_map_len = map_len;
        *out_len = body;
        return (uint8_t *)map + skew + moov_hdrsz;
    }

    /* mmap failed — fall back to malloc+read, but only for small moovs so we
     * never re-introduce the OOM-thrash hang on big moovs. fd is positioned at
     * the moov body; re-seek explicitly for robustness. */
    if (body > MOOV_MALLOC_MAX) { close(fd); return NULL; }
    if (lseek(fd, moov_off + moov_hdrsz, SEEK_SET) < 0) { close(fd); return NULL; }
    uint8_t *buf = malloc((size_t)body);
    if (!buf) { close(fd); return NULL; }
    size_t got = 0;
    while (got < (size_t)body) {
        ssize_t r = read(fd, buf + got, (size_t)body - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got != (size_t)body) { free(buf); return NULL; }
    *out_len = body;
    /* *out_map stays NULL → caller frees (not munmaps) the returned buffer. */
    return buf;
}

/* Read exactly n bytes from fd at file offset off into buf. Returns 1 on
 * success. */
static int pread_full(int fd, void *buf, size_t n, int64_t off) {
    if (lseek(fd, (off_t)off, SEEK_SET) < 0) return 0;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return got == n ? 1 : 0;
}

/* Find a direct child atom of type within [start,end) in a buffer. Sets
 * body_off/body_end to the atom body. Returns 1 if found. */
static int qt_find_child(const uint8_t *buf, int start, int end, uint32_t type,
                         int *body_off, int *body_end) {
    int off = start;
    while (off + 8 <= end) {
        uint32_t size = qt_read32(buf + off);
        uint32_t atype = qt_read32(buf + off + 4);
        int hdrsz = 8;
        int64_t asize = size;
        if (size == 1 && off + 16 <= end) { asize = (int64_t)qt_read64(buf + off); hdrsz = 16; }
        else if (size == 0) { asize = end - off; }
        if (asize < hdrsz || off + asize > end) break;
        if (atype == type) {
            *body_off = off + hdrsz;
            *body_end = off + (int)asize;
            return 1;
        }
        off += (int)asize;
    }
    return 0;
}

/* Recursively search for an atom of type within [start,end), descending into
 * known container atoms. Returns 1 if found (body set). */
static int qt_find_deep(const uint8_t *buf, int start, int end, uint32_t type,
                        int *body_off, int *body_end) {
    /* First check direct children. */
    if (qt_find_child(buf, start, end, type, body_off, body_end)) return 1;
    /* Descend into container children. */
    int off = start;
    while (off + 8 <= end) {
        uint32_t size = qt_read32(buf + off);
        uint32_t atype = qt_read32(buf + off + 4);
        int hdrsz = 8;
        int64_t asize = size;
        if (size == 1 && off + 16 <= end) { asize = (int64_t)qt_read64(buf + off); hdrsz = 16; }
        else if (size == 0) { asize = end - off; }
        if (asize < hdrsz || off + asize > end) break;
        if (atype == 0x6d6f6f76 || atype == 0x7472616b || atype == 0x6d646961 ||
            atype == 0x6d696e66 || atype == 0x7374626c || atype == 0x75647461 ||
            atype == 0x65647473 || atype == 0x74726566 || atype == 0x6d657461 ||
            atype == 0x64696e66) {
            if (qt_find_deep(buf, off + hdrsz, off + (int)asize, type,
                             body_off, body_end)) return 1;
        }
        off += (int)asize;
    }
    return 0;
}

/* Parse a Nero chpl atom (flat chapter list). Returns chapter count emitted. */
static int parse_nero_chpl(const uint8_t *buf, int boff, int bend, int64_t track_dur_ms,
                           chapter_cb cb, void *ctx) {
    if (boff + 8 > bend) return 0;
    /* version(1) + flags(3) + chapter_count(4) */
    uint32_t count = qt_read32(buf + boff + 4);
    if (count == 0 || count > 4096) return 0;
    int p = boff + 8;
    int emitted = 0;
    int64_t prev_start_ms = -1;
    char prev_title[256] = "";
    for (uint32_t i = 0; i < count && p + 9 <= bend; i++) {
        int64_t start_100ns = (int64_t)qt_read64(buf + p);
        p += 8;
        if (p >= bend) break;
        uint8_t namelen = buf[p++];
        if (p + namelen > bend) break;
        char title[256];
        int tn = namelen < (int)sizeof(title) - 1 ? namelen : (int)sizeof(title) - 1;
        utf8_safe_truncate(title, (int)sizeof(title), (const char *)(buf + p), tn);
        p += namelen;

        int64_t start_ms = start_100ns / 10000;  /* 100ns -> ms */
        /* Emit the previous chapter now that we know its end. */
        if (emitted > 0 && cb) {
            cb(emitted, prev_title, prev_start_ms, start_ms, ctx);
        }
        emitted++;
        prev_start_ms = start_ms;
        utf8_safe_truncate(prev_title, (int)sizeof(prev_title), title, -1);
    }
    if (emitted > 0 && cb) {
        cb(emitted, prev_title, prev_start_ms, track_dur_ms, ctx);
    }
    return emitted;
}

/* Parse an stts atom into per-sample durations (ms units = sample_delta).
 * Returns sample count, or 0. out_deltas is filled up to max. */
static int parse_stts(const uint8_t *buf, int boff, int bend,
                      uint32_t *out_deltas, int max) {
    if (boff + 8 > bend) return 0;
    uint32_t entries = qt_read32(buf + boff + 4);
    if (entries > 1000000) return 0;
    int p = boff + 8;
    int n = 0;
    for (uint32_t i = 0; i < entries && n < max; i++) {
        if (p + 8 > bend) break;
        uint32_t count = qt_read32(buf + p);
        uint32_t delta = qt_read32(buf + p + 4);
        p += 8;
        for (uint32_t k = 0; k < count && n < max; k++) out_deltas[n++] = delta;
    }
    return n;
}

/* Parse an stsz atom into per-sample sizes. Returns sample count. */
static int parse_stsz(const uint8_t *buf, int boff, int bend,
                      uint32_t *out_sizes, int max) {
    if (boff + 12 > bend) return 0;
    uint32_t sample_size = qt_read32(buf + boff + 4);
    uint32_t count = qt_read32(buf + boff + 8);
    if (count > 1000000) return 0;
    if (count > (uint32_t)max) count = (uint32_t)max;
    int p = boff + 12;
    if (sample_size != 0) {
        for (uint32_t i = 0; i < count; i++) out_sizes[i] = sample_size;
        return (int)count;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (p + 4 > bend) return (int)i;
        out_sizes[i] = qt_read32(buf + p);
        p += 4;
    }
    return (int)count;
}

/* Parse an stco atom into chunk offsets. Returns chunk count. */
static int parse_stco(const uint8_t *buf, int boff, int bend,
                      uint64_t *out_offsets, int max, int is64) {
    if (boff + 8 > bend) return 0;
    uint32_t count = qt_read32(buf + boff + 4);
    if (count > 1000000) return 0;
    if (count > (uint32_t)max) count = (uint32_t)max;
    int p = boff + 8;
    for (uint32_t i = 0; i < count; i++) {
        if (is64) {
            if (p + 8 > bend) return (int)i;
            out_offsets[i] = qt_read64(buf + p);
            p += 8;
        } else {
            if (p + 4 > bend) return (int)i;
            out_offsets[i] = (uint64_t)qt_read32(buf + p);
            p += 4;
        }
    }
    return (int)count;
}

/* Parse an stsc (sample-to-chunk) atom. Each run says: starting at first_chunk
 * (1-based), every chunk contains samples_per_chunk samples (with sample desc
 * index sdi). Returns run count, or 0. Needed because chapter tracks often pack
 * ALL samples into ONE chunk (stco has 1 entry, stsc says N samples/chunk), so
 * stco offsets do NOT map 1:1 to samples. */
static int parse_stsc(const uint8_t *buf, int boff, int bend,
                      uint32_t *out_first_chunk, uint32_t *out_spc,
                      uint32_t *out_sdi, int max) {
    if (boff + 8 > bend) return 0;
    uint32_t entries = qt_read32(buf + boff + 4);
    if (entries == 0 || entries > 1000000) return 0;
    if ((int)entries > max) entries = (uint32_t)max;
    int p = boff + 8;
    for (uint32_t i = 0; i < entries; i++) {
        if (p + 12 > bend) return (int)i;
        out_first_chunk[i] = qt_read32(buf + p);
        out_spc[i] = qt_read32(buf + p + 4);
        out_sdi[i] = qt_read32(buf + p + 8);
        p += 12;
    }
    return (int)entries;
}

/* Parse a QuickTime chapter track (text/subt handler) and emit chapters,
 * reading sample titles from the file. Per-sample file offsets are resolved from
 * stsc (sample-to-chunk) + stsz (sizes) + stco (chunk offsets); this handles
 * chapter tracks that pack many samples into one chunk. Returns chapter count. */
static int parse_qt_chapter_track(const char *path, const uint8_t *moov,
                                   int moov_len, int trak_off, int trak_end,
                                   int64_t track_dur_ms, chapter_cb cb,
                                   void *ctx) {
    int mdia_off, mdia_end;
    if (!qt_find_child(moov, trak_off, trak_end, 0x6d646961 /* 'mdia' */,
                      &mdia_off, &mdia_end)) return 0;
    int mdhd_off, mdhd_end;
    if (!qt_find_child(moov, mdia_off, mdia_end, 0x6d646864 /* 'mdhd' */,
                      &mdhd_off, &mdhd_end)) return 0;
    if (mdhd_end - mdhd_off < 24) return 0;
    int version = moov[mdhd_off];
    uint32_t timescale = qt_read32(moov + mdhd_off + (version == 0 ? 12 : 20));
    if (timescale == 0) return 0;

    int minf_off, minf_end;
    if (!qt_find_child(moov, mdia_off, mdia_end, 0x6d696e66 /* 'minf' */,
                      &minf_off, &minf_end)) return 0;
    int stbl_off, stbl_end;
    if (!qt_find_child(moov, minf_off, minf_end, 0x7374626c /* 'stbl' */,
                      &stbl_off, &stbl_end)) return 0;

    int stts_off, stts_end, stsz_off, stsz_end;
    if (!qt_find_child(moov, stbl_off, stbl_end, 0x73747473 /* 'stts' */,
                      &stts_off, &stts_end)) return 0;
    if (!qt_find_child(moov, stbl_off, stbl_end, 0x7374737a /* 'stsz' */,
                      &stsz_off, &stsz_end)) return 0;

    /* stco or co64 */
    int stco_off = -1, stco_end = 0, is64 = 0;
    if (qt_find_child(moov, stbl_off, stbl_end, 0x7374636f /* 'stco' */,
                      &stco_off, &stco_end)) is64 = 0;
    else if (qt_find_child(moov, stbl_off, stbl_end, 0x636f3634 /* 'co64' */,
                           &stco_off, &stco_end)) is64 = 1;
    else return 0;

    /* stsc (sample-to-chunk) — optional but needed when samples share a chunk. */
    int stsc_off, stsc_end;
    int has_stsc = qt_find_child(moov, stbl_off, stbl_end, 0x73747363 /* 'stsc' */,
                                 &stsc_off, &stsc_end);

    int max_samples = 4096;
    static uint32_t deltas[4096];
    static uint32_t sizes[4096];
    static uint64_t offsets[4096];      /* chunk offsets (stco) */
    static uint64_t sample_off[4096];   /* resolved per-sample file offsets */
    int n_dur = parse_stts(moov, stts_off, stts_end, deltas, max_samples);
    int n_sz = parse_stsz(moov, stsz_off, stsz_end, sizes, max_samples);
    int n_off = parse_stco(moov, stco_off, stco_end, offsets, max_samples, is64);
    if (n_dur == 0 || n_sz == 0 || n_off == 0) return 0;

    /* Chapter count is the sample count (one chapter per text sample). */
    int n = n_dur;
    if (n_sz < n) n = n_sz;
    if (n > max_samples) n = max_samples;

    /* Resolve each sample's file offset. With stsc, walk the sample-to-chunk
     * table: run r covers chunks [first_chunk[r], first_chunk[r+1]-1], each
     * holding spc[r] samples packed contiguously; sample i in a chunk is at
     * chunk_offset + sum(sizes of earlier samples in the same chunk). Without
     * stsc, fall back to the legacy one-sample-per-chunk assumption. */
    if (has_stsc) {
        static uint32_t fc[256], spc[256], sdi[256];
        int n_runs = parse_stsc(moov, stsc_off, stsc_end, fc, spc, sdi, 256);
        int run = 0;
        uint32_t left = (n_runs > 0) ? spc[0] : 1;  /* samples left in current chunk */
        int chunk_idx = 0;                          /* 0-based into offsets[] */
        uint64_t within = 0;                        /* byte offset within chunk */
        int i = 0;
        for (; i < n; i++) {
            if (left == 0) {
                chunk_idx++;
                within = 0;
                if (chunk_idx >= n_off) break;      /* ran out of chunk offsets */
                /* Crossing into the next stsc run? Chunks are 1-based; the new
                 * chunk is chunk_idx+1. Advance when we reach its first_chunk. */
                while (run + 1 < n_runs && (uint32_t)(chunk_idx + 1) >= fc[run + 1])
                    run++;
                left = spc[run];
            }
            sample_off[i] = offsets[chunk_idx] + within;
            within += sizes[i];
            left--;
        }
        n = i;   /* if we broke early on missing chunk offsets, stop here */
    } else {
        if (n_off < n) n = n_off;
        for (int i = 0; i < n; i++) sample_off[i] = offsets[i];
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    int emitted = 0;
    int64_t cum = 0;
    for (int i = 0; i < n; i++) {
        int64_t start_ms = (cum * 1000) / timescale;
        cum += deltas[i];
        int64_t end_ms = (cum * 1000) / timescale;
        if (i == n - 1) end_ms = track_dur_ms;

        char title[256];
        const char *emit_title = title;
        if (sample_off[i] == 0 || sizes[i] == 0 || sizes[i] > 4096) {
            snprintf(title, sizeof(title), "Chapter %d", i + 1);
        } else {
            uint8_t sbuf[4096];
            if (!pread_full(fd, sbuf, sizes[i], (int64_t)sample_off[i])) {
                snprintf(title, sizeof(title), "Chapter %d", i + 1);
            } else {
                /* Text sample: uint16 length prefix, then that many bytes of
                 * UTF-8 text. */
                int tlen = (sbuf[0] << 8) | sbuf[1];
                int avail = (int)sizes[i] - 2;
                if (tlen < 0 || tlen > avail) tlen = avail;  /* length unreliable */
                if (tlen > (int)sizeof(title) - 1) tlen = (int)sizeof(title) - 1;
                utf8_safe_truncate(title, (int)sizeof(title),
                                   (const char *)(sbuf + 2), tlen);
            }
        }
        emitted++;
        if (cb) cb(emitted, emit_title, start_ms, end_ms, ctx);
    }
    close(fd);
    return emitted;
}

/* Identify QuickTime chapter tracks and emit chapters from them. Returns
 * total chapters emitted. */
static int parse_qt_chapters(const char *path, const uint8_t *moov, int moov_len,
                             int64_t track_dur_ms, chapter_cb cb, void *ctx) {
    int emitted = 0;
    int off = 0;
    while (off + 8 <= moov_len) {
        uint32_t size = qt_read32(moov + off);
        uint32_t atype = qt_read32(moov + off + 4);
        int hdrsz = 8;
        int64_t asize = size;
        if (size == 1 && off + 16 <= moov_len) { asize = (int64_t)qt_read64(moov + off); hdrsz = 16; }
        else if (size == 0) { asize = moov_len - off; }
        if (asize < hdrsz || off + asize > moov_len) break;
        if (atype == 0x7472616b /* 'trak' */) {
            int trak_off = off + hdrsz, trak_end = off + (int)asize;
            int mdia_off, mdia_end;
            if (qt_find_child(moov, trak_off, trak_end, 0x6d646961 /* 'mdia' */,
                              &mdia_off, &mdia_end)) {
                int hdlr_off, hdlr_end;
                if (qt_find_child(moov, mdia_off, mdia_end, 0x68646c72 /* 'hdlr' */,
                                  &hdlr_off, &hdlr_end)) {
                    /* hdlr body: version(1)+flags(3)+comp_type(4)+subtype(4) */
                    if (hdlr_end - hdlr_off >= 12) {
                        uint32_t subtype = qt_read32(moov + hdlr_off + 8);
                        if (subtype == 0x74657874 /* 'text' */ ||
                            subtype == 0x73756274 /* 'subt' */ ||
                            subtype == 0x7362746c /* 'sbtl' */) {
                            emitted += parse_qt_chapter_track(path, moov, moov_len,
                                                              trak_off, trak_end,
                                                              track_dur_ms, cb, ctx);
                        }
                    }
                }
            }
        }
        off += (int)asize;
    }
    return emitted;
}

int audio_read_chapters(const char *path, chapter_cb cb, void *ctx) {
    if (!path) return 0;
    int64_t moov_len = 0;
    uint8_t *map_base = NULL;
    size_t map_len = 0;
    uint8_t *moov = read_moov(path, &moov_len, &map_base, &map_len);
    if (!moov) return 0;

    /* mvhd gives the overall movie duration for the last chapter's end. */
    int64_t track_dur_ms = 0;
    int mvhd_off, mvhd_end;
    if (qt_find_child(moov, 0, (int)moov_len, 0x6d766864 /* 'mvhd' */,
                      &mvhd_off, &mvhd_end) &&
        mvhd_end - mvhd_off >= 24) {
        int version = moov[mvhd_off];
        uint32_t timescale = qt_read32(moov + mvhd_off + 12);
        uint64_t duration = 0;
        if (version == 0) duration = qt_read32(moov + mvhd_off + 16);
        else if (mvhd_end - mvhd_off >= 24) duration = qt_read64(moov + mvhd_off + 20);
        if (timescale > 0) track_dur_ms = (int64_t)(duration * 1000 / timescale);
    }

    int emitted = 0;

    /* 1. Nero chpl (moov/udta). */
    int chpl_off, chpl_end;
    if (qt_find_deep(moov, 0, (int)moov_len, 0x6368706c /* 'chpl' */,
                     &chpl_off, &chpl_end)) {
        emitted = parse_nero_chpl(moov, chpl_off, chpl_end, track_dur_ms, cb, ctx);
    }

    /* 2. QuickTime chapter track (only if no chpl). */
    if (emitted == 0) {
        emitted = parse_qt_chapters(path, moov, (int)moov_len, track_dur_ms, cb, ctx);
    }

    if (map_base) munmap(map_base, map_len);
    else free(moov);
    return emitted;
}

/* ---- Embedded cover-art extraction (JPEG or PNG) ------------------------ */
/* The cover decoder (cover.c) takes an image file path and decodes it — JPEG
 * via the device's libjpeg (scale_denom downscale-on-decode) or PNG via pngdec
 * (libz dlopen'd, row-streamed) — so even a large embedded cover is cheap. So
 * extraction = find the embedded image bytes (JPEG or PNG) in the file's
 * metadata and write them to a per-book image file on the SD card. The scan
 * then stores that path as the book's cover_path and the normal decode/cache
 * path takes over. Both formats are detected by their byte signatures (JPEG
 * SOI 0xFFD8FF, PNG sig 89 50 4E 47...), so we don't rely on the tag's declared
 * type; unsupported formats are skipped (those books simply show no cover). */

/* Write len bytes (a complete image) to out_path. Returns 1 on success; on
 * any short write or close error the partial file is removed so a later
 * attempt doesn't pick up a truncated image. */
static int write_image_file(const char *out_path, const uint8_t *data, size_t len) {
    if (!out_path || !data || len == 0) return 0;
    FILE *f = fopen(out_path, "wb");
    if (!f) return 0;
    size_t w = fwrite(data, 1, len, f);
    int ok = (w == len);
    if (fclose(f) != 0) ok = 0;
    if (!ok) unlink(out_path);
    return ok;
}

/* 8-byte PNG signature. */
static const uint8_t PNG_SIG[8] = { 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A };

/* Locate the first embedded image in buf[0..len): a JPEG SOI (0xFF 0xD8 0xFF)
 * or a PNG signature (8 bytes). APIC frames put an encoding byte + MIME string
 * + picture type + description before the image, so it rarely starts at offset
 * 0; a small forward scan finds either signature. Sets *out_off to the image
 * start index and *out_kind to 0=JPEG / 1=PNG. Returns 1 if found. */
static int find_image_off(const uint8_t *buf, size_t len, size_t *out_off, int *out_kind) {
    if (len < 3) return 0;
    for (size_t i = 0; i + 3 <= len; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF) {
            *out_off = i; *out_kind = 0; return 1;
        }
        if (i + 8 <= len && buf[i] == PNG_SIG[0] && buf[i + 1] == PNG_SIG[1] &&
            buf[i + 2] == PNG_SIG[2] && buf[i + 3] == PNG_SIG[3] &&
            buf[i + 4] == PNG_SIG[4] && buf[i + 5] == PNG_SIG[5] &&
            buf[i + 6] == PNG_SIG[6] && buf[i + 7] == PNG_SIG[7]) {
            *out_off = i; *out_kind = 1; return 1;
        }
    }
    return 0;
}

/* Write <out_base>.jpg (JPEG) or <out_base>.png (PNG) and copy the full path
 * into out_path. Returns 1 on success. */
static int emit_image(const char *out_base, char *out_path, size_t out_path_len,
                      const uint8_t *data, size_t len, int kind) {
    const char *ext = kind == 1 ? "png" : "jpg";
    char path[600];
    if (snprintf(path, sizeof(path), "%s.%s", out_base, ext) >= (int)sizeof(path)) return 0;
    if (!write_image_file(path, data, len)) return 0;
    if (out_path && out_path_len) {
        if (snprintf(out_path, out_path_len, "%s", path) >= (int)out_path_len) {
            unlink(path);   /* caller couldn't receive the path — undo */
            return 0;
        }
    }
    return 1;
}

/* M4B/M4A: the cover lives in moov/udta/meta/ilst/covr. The iTunes 'meta' atom
 * is a FullBox — a 4-byte version/flags header sits before its children — which
 * the plain atom walker (qt_find_deep) mis-aligns on. Instead we raw-scan the
 * mmap'd moov for the 4-byte 'covr' marker; that is safe because the moov is
 * metadata-only (atom names + small tables + tag text — no audio samples that
 * could false-match), and each candidate is validated against the covr/data
 * atom layout + a JPEG SOI before anything is written. The mmap means a 15 MB
 * moov with a 79 KB cover costs ~the cover size in RAM, not 15 MB. */
static int extract_m4b_cover(const char *path, const char *out_base,
                             char *out_path, size_t out_path_len) {
    int64_t moov_len = 0;
    uint8_t *map_base = NULL;
    size_t map_len = 0;
    uint8_t *moov = read_moov(path, &moov_len, &map_base, &map_len);
    if (!moov || moov_len < 24) {
        if (map_base) munmap(map_base, map_len); else free(moov);
        return 0;
    }

    int found = 0;
    for (int64_t i = 0; i + 24 <= moov_len && !found; i++) {
        if (moov[i] != 'c' || moov[i + 1] != 'o' ||
            moov[i + 2] != 'v' || moov[i + 3] != 'r')
            continue;
        if (i < 4) continue;
        uint32_t covr_size = qt_read32(moov + i - 4);   /* size precedes type */
        if (covr_size < 24 || (int64_t)i - 4 + covr_size > moov_len) continue;

        /* First child is a 'data' atom:
         *   [size(4)]['data'(4)][type/flags(4)][reserved(4)][image...]
         * starting at i+4 (right after the covr type field). The 4-byte
         * type/flags field is 0x0000000T, so the format code is its LAST
         * byte: 0x0d=JPEG, 0x0e=PNG. We accept either and confirm the actual
         * format from the byte signature at the image offset. */
        uint32_t data_size = qt_read32(moov + i + 4);
        if (data_size < 16) continue;
        if (qt_read32(moov + i + 8) != 0x64617461 /* 'data' */) continue;
        uint8_t fmt = moov[i + 15];
        if (fmt != 0x0d && fmt != 0x0e) continue;

        int64_t img_off = (int64_t)i + 20;    /* covr type(4)+data hdr(16) */
        int64_t img_len = (int64_t)data_size - 16;
        if (img_len < 8 || img_off + img_len > moov_len) continue;

        /* Detect the actual format by signature (don't trust the flag byte). */
        int kind = -1;
        if (moov[img_off] == 0xFF && moov[img_off + 1] == 0xD8) kind = 0;
        else if (moov[img_off] == PNG_SIG[0] && moov[img_off + 1] == PNG_SIG[1] &&
                 moov[img_off + 2] == PNG_SIG[2] && moov[img_off + 3] == PNG_SIG[3])
            kind = 1;
        if (kind < 0) continue;

        found = emit_image(out_base, out_path, out_path_len,
                          moov + img_off, (size_t)img_len, kind);
    }

    if (map_base) munmap(map_base, map_len); else free(moov);
    return found;
}

/* MP3: the cover is an ID3v2 APIC frame (v2.2 = PIC). We read up to ~1 MB of
 * the tag so even a large embedded JPEG fits; the buffer is freed at once. The
 * APIC body is encoding(1)+MIME\0+pic_type(1)+desc\0+image, so we scan it for
 * the JPEG SOI rather than parsing the variable-length prefix, then take the
 * image up to the frame's end. */
#define MP3_COVER_READ_MAX (1024 * 1024)

static int extract_mp3_cover(const char *path, const char *out_base,
                             char *out_path, size_t out_path_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) != 10) { fclose(f); return 0; }
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') { fclose(f); return 0; }

    int major = hdr[3];
    uint32_t tag_size = syncsafe_to_uint(hdr + 6);
    size_t read_len = (size_t)tag_size + 10;
    if (read_len > MP3_COVER_READ_MAX) read_len = MP3_COVER_READ_MAX;

    uint8_t *buf = malloc(read_len);
    if (!buf) { fclose(f); return 0; }
    memcpy(buf, hdr, 10);
    size_t got = 10 + fread(buf + 10, 1, read_len - 10, f);
    fclose(f);

    int found = 0;
    uint32_t walk_end = tag_size + 10;
    if (walk_end > got) walk_end = (uint32_t)got;

    uint32_t pos = 10;
    while (pos + 10 <= walk_end && !found) {
        if (buf[pos] == 0) break;            /* padding / end of frames */
        char id[5] = {0};
        memcpy(id, buf + pos, 4);

        uint32_t frame_size;
        if (major == 4) frame_size = syncsafe_to_uint(buf + pos + 4);
        else frame_size = ((uint32_t)buf[pos + 4] << 24) | ((uint32_t)buf[pos + 5] << 16) |
                         ((uint32_t)buf[pos + 6] << 8) | ((uint32_t)buf[pos + 7]);
        if (frame_size == 0) break;
        if ((uint64_t)pos + 10 + frame_size > walk_end) break;

        if (strcmp(id, "APIC") == 0) {
            size_t body_off = (size_t)pos + 10;
            size_t body_len = frame_size;
            size_t j; int kind;
            if (find_image_off(buf + body_off, body_len, &j, &kind)) {
                size_t img_off = body_off + j;
                size_t img_end = (size_t)pos + 10 + frame_size;
                if (img_end > got) img_end = got;
                if (img_end > img_off)
                    found = emit_image(out_base, out_path, out_path_len,
                                       buf + img_off, img_end - img_off, kind);
            }
        }
        pos += 10 + frame_size;
    }

    free(buf);
    return found;
}

int audio_extract_cover(const char *track_path, const char *out_base,
                        char *out_path, size_t out_path_len) {
    if (!track_path || !out_base) return 0;
    int type = audio_file_type(track_path);
    if (type == AUDIO_EXT_M4B || type == AUDIO_EXT_M4A)
        return extract_m4b_cover(track_path, out_base, out_path, out_path_len);
    if (type == AUDIO_EXT_MP3)
        return extract_mp3_cover(track_path, out_base, out_path, out_path_len);
    return 0;
}

/* ---- File type detection ------------------------------------------------ */

int audio_file_type(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;
    dot++;
    if (strcasecmp(dot, "mp3") == 0) return AUDIO_EXT_MP3;
    if (strcasecmp(dot, "m4b") == 0) return AUDIO_EXT_M4B;
    if (strcasecmp(dot, "m4a") == 0) return AUDIO_EXT_M4A;
    if (strcasecmp(dot, "aac") == 0) return AUDIO_EXT_AAC;
    if (strcasecmp(dot, "wav") == 0) return AUDIO_EXT_WAV;
    if (strcasecmp(dot, "flac") == 0) return AUDIO_EXT_FLAC;
    if (strcasecmp(dot, "ogg") == 0) return AUDIO_EXT_OGG;
    return 0;
}

/* ---- Main entry point --------------------------------------------------- */

int audio_read_tags(const char *path, audio_tags_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (get_file_info(path, &out->file_size, &out->file_mtime) < 0)
        return -1;

    int type = audio_file_type(path);
    if (type == 0) return -1;

    if (type == AUDIO_EXT_MP3) {
        /* ID3 text tags (TIT2/TPE1/...) live in the first few KB of the tag,
         * so the 64 KB read covers them. Duration is parsed separately by
         * seeking past the ID3 tag to the first MPEG frame and reading the
         * Xing/Info/VBRI header: a large embedded cover can push the first
         * frame past 64 KB, and a VBR file's real duration is the frame
         * count, not one frame's bitrate. */
        uint8_t buf[65536];
        int n = read_file_header(path, buf, sizeof(buf));
        if (n > 10) parse_id3v2(buf, n, out);
        out->duration_ms = parse_mp3_duration(path, out->file_size);
    } else if (type == AUDIO_EXT_M4B || type == AUDIO_EXT_M4A) {
        parse_m4b(path, out);
    } else {
        /* For other formats, just record file info */
        return 0;
    }

    return 0;
}