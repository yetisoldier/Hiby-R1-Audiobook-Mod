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
    if (!dst || dst_len <= 0 || !src || src_len <= 0) return;
    int n = src_len < dst_len - 1 ? src_len : dst_len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
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
    if (!data || len <= 0 || !out || out_len <= 0) return;
    /* encoding: 0=ISO-8859-1, 1=UTF-16 w/ BOM, 2=UTF-16BE, 3=UTF-8 */
    if (encoding == 1 || encoding == 2) {
        /* UTF-16: just take the ASCII subset, skip BOM */
        int si = 0;
        if (encoding == 1 && len >= 2 && data[0] == 0xFF && data[1] == 0xFE)
            si = 2; /* BOM */
        int di = 0;
        for (; si + 1 < len && di < out_len - 1; si += 2) {
            if (data[si] == 0 && data[si+1] == 0) break; /* null terminator */
            if (data[si+1] == 0 && data[si] < 128)
                out[di++] = data[si];
            else
                out[di++] = '?';
        }
        out[di] = '\0';
    } else {
        /* ISO-8859-1 or UTF-8: copy directly, stop at null */
        int n = len < out_len - 1 ? len : out_len - 1;
        int di = 0;
        for (int i = 0; i < n; i++) {
            if (data[i] == 0) break;
            out[di++] = data[i];
        }
        out[di] = '\0';
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

/* ---- M4B/M4A (QuickTime) parsing ---------------------------------------- */

/* Read a 32-bit big-endian value */
static uint32_t qt_read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t qt_read64(const uint8_t *p) {
    return ((uint64_t)qt_read32(p) << 32) | (uint64_t)qt_read32(p + 4);
}

/* Parse QuickTime atoms recursively to find mvhd (duration) and count
 * chapter tracks. depth-limited to prevent infinite recursion. */
static void parse_qt_atoms(const uint8_t *buf, int buf_len, int offset,
                          int end, audio_tags_t *out, int depth) {
    if (depth > 6 || offset >= end) return;

    while (offset + 8 <= end) {
        uint32_t atom_size = qt_read32(buf + offset);
        uint32_t atom_type = qt_read32(buf + offset + 4);

        /* Handle 64-bit extended size */
        int header_size = 8;
        if (atom_size == 1 && offset + 16 <= end) {
            /* 64-bit size follows — but we'll just cap at end */
            header_size = 16;
        }
        if (atom_size == 0) atom_size = end - offset; /* extends to end */
        if (atom_size < 8 || offset + atom_size > end) break;

        int body_offset = offset + header_size;
        int body_end = offset + atom_size;
        char type_str[5] = {0};
        memcpy(type_str, buf + offset + 4, 4);

        if (atom_type == 0x6d766864 /* 'mvhd' */) {
            /* Movie header: version(1) + flags(3) + creation(4) + mod(4)
             * + timescale(4) + duration(4 or 8) */
            if (body_offset + 24 <= body_end) {
                int version = buf[body_offset];
                uint32_t timescale = qt_read32(buf + body_offset + 12);
                uint64_t duration = 0;
                if (version == 0) {
                    duration = qt_read32(buf + body_offset + 16);
                } else {
                    if (body_offset + 24 <= body_end)
                        duration = qt_read64(buf + body_offset + 20);
                }
                if (timescale > 0)
                    out->duration_ms = (int64_t)(duration * 1000 / timescale);
            }
        } else if (atom_type == 0x7472616b /* 'trak' */) {
            /* Track: check if it's a chapter track by looking for 'chap'
             * reference. Count tracks with audio media. */
            parse_qt_atoms(buf, body_end, body_offset, body_end, out, depth + 1);

            /* Check for chapter track reference (elst or chap atom) */
            /* Simple heuristic: count trak atoms at depth 1 as chapters
             * if there's more than one trak and the first has audio. */
            if (depth == 0) {
                out->embedded_chapters++;
            }
        } else if (atom_type == 0x6d6f6f76 /* 'moov' */ ||
                   atom_type == 0x6d646961 /* 'mdia' */ ||
                   atom_type == 0x6d696e66 /* 'minf' */ ||
                   atom_type == 0x7374626c /* 'stbl' */ ||
                   atom_type == 0x75647461 /* 'udta' */) {
            parse_qt_atoms(buf, body_end, body_offset, body_end, out, depth + 1);
        }

        offset += atom_size;
    }
}

static void parse_m4b(const char *path, audio_tags_t *out) {
    /* Read the first 256KB — enough for the moov atom in most audiobooks.
     * For very large M4B files with moov at the end, we won't get duration,
     * but that's acceptable for a minimal scanner. */
    int read_len = 262144;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t *buf = malloc(read_len);
    if (!buf) { fclose(f); return; }
    int n = fread(buf, 1, read_len, f);
    fclose(f);
    if (n < 8) { free(buf); return; }

    out->embedded_chapters = 0;
    parse_qt_atoms(buf, n, 0, n, out, 0);

    /* If we found trak atoms but no mvhd (moov might be at end of file),
     * estimate duration from file size — rough M4B ~1MB/min at 128kbps */
    if (out->duration_ms == 0) {
        out->duration_ms = (out->file_size * 8000) / 128000;
    }

    /* If more than 1 trak was found, first one is usually the audio track
     * and the rest are chapter markers. Adjust count. */
    if (out->embedded_chapters > 1) {
        out->embedded_chapters--;  /* first trak is audio, not chapter */
    } else {
        out->embedded_chapters = 0;
    }

    free(buf);
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
        memcpy(title, buf + p, tn);
        title[tn] = '\0';
        p += namelen;

        int64_t start_ms = start_100ns / 10000;  /* 100ns -> ms */
        /* Emit the previous chapter now that we know its end. */
        if (emitted > 0 && cb) {
            cb(emitted, prev_title, prev_start_ms, start_ms, ctx);
        }
        emitted++;
        prev_start_ms = start_ms;
        strncpy(prev_title, title, sizeof(prev_title) - 1);
        prev_title[sizeof(prev_title) - 1] = '\0';
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
                 * text (UTF-8 for ASCII chapter titles). */
                int tlen = (sbuf[0] << 8) | sbuf[1];
                int avail = (int)sizes[i] - 2;
                if (tlen < 0 || tlen > avail) tlen = avail;  /* length unreliable */
                if (tlen > (int)sizeof(title) - 1) tlen = (int)sizeof(title) - 1;
                memcpy(title, sbuf + 2, tlen);
                title[tlen] = '\0';
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
        uint8_t buf[65536];
        int n = read_file_header(path, buf, sizeof(buf));
        if (n > 10) {
            parse_id3v2(buf, n, out);
            if (out->duration_ms == 0) {
                int bitrate = find_mp3_bitrate(buf, n);
                out->duration_ms = audio_estimate_mp3_duration(out->file_size,
                                                                bitrate);
            }
        }
    } else if (type == AUDIO_EXT_M4B || type == AUDIO_EXT_M4A) {
        parse_m4b(path, out);
    } else {
        /* For other formats, just record file info */
        return 0;
    }

    return 0;
}