/* mp4_audio.c — RAM-frugal MP4/M4B audio demux (AAC playback).
 *
 * See mp4_audio.h for the RAM strategy. This TU is self-contained: it
 * duplicates a few small box-walking primitives (qt_read32/64, pread_full,
 * qt_find_child, a moov slurper) so it never touches tags.c and cannot
 * regress the scanner/chapter path.
 */
#include "mp4_audio.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MOOV_MAX_BYTES (16 * 1024 * 1024)   /* OOM guard — >16MB => reject */
#define CHUNK_MAX       100000              /* chunks; ~1.6MB index ceiling */
#define STTS_ENTRY_MAX  8192               /* RLE entries; AAC usually 1 */

/* ---- duplicated box primitives ------------------------------------------ */

static uint32_t qt_read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t qt_read64(const uint8_t *p) {
    return ((uint64_t)qt_read32(p) << 32) | (uint64_t)qt_read32(p + 4);
}

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

/* Find a direct child atom of type within [start,end). Sets body_off/body_end
 * (offsets relative to buf). Returns 1 if found. */
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

/* Slurp the moov atom body into a malloc'd buffer, keeping the fd open for
 * later sample preads. Sets *out_body_file_off to the absolute file offset of
 * the moov body (buffer byte 0 == that file offset, so a box at buffer offset X
 * lives at file offset out_body_file_off + X). Returns NULL on failure.
 * Rejects moov bodies larger than MOOV_MAX_BYTES (graceful, not an OOM freeze). */
static uint8_t *read_moov_fd(const char *path, int *out_fd,
                             int64_t *out_len, int64_t *out_body_file_off) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    int64_t off = 0;
    uint8_t hdr[16];
    for (;;) {
        if (lseek(fd, off, SEEK_SET) < 0) break;
        if (read(fd, hdr, 8) != 8) break;
        uint32_t size = qt_read32(hdr);
        uint32_t type = qt_read32(hdr + 4);
        int64_t atom_size = (int64_t)size;
        int hdrsz = 8;
        if (size == 1) {
            if (read(fd, hdr + 8, 8) != 8) break;
            atom_size = (int64_t)qt_read64(hdr);
            hdrsz = 16;
        } else if (size == 0) {
            break;  /* atom extends to EOF; not moov */
        }
        if (atom_size < hdrsz) break;
        if (type == 0x6d6f6f76 /* 'moov' */) {
            int64_t body = atom_size - hdrsz;
            if (body <= 0 || body > MOOV_MAX_BYTES) break;
            uint8_t *buf = malloc((size_t)body);
            if (!buf) break;
            size_t got = 0;
            while (got < (size_t)body) {
                ssize_t r = read(fd, buf + got, (size_t)body - got);
                if (r <= 0) break;
                got += (size_t)r;
            }
            if (got != (size_t)body) { free(buf); break; }
            *out_fd = fd;                  /* keep open */
            *out_len = body;
            *out_body_file_off = off + hdrsz;
            return buf;
        }
        off += atom_size;
    }
    close(fd);
    return NULL;
}

/* ---- soun trak selection ------------------------------------------------- */

/* Walk top-level trak boxes in the moov body; return the first whose mdia/hdlr
 * subtype is 'soun'. Sets trak body offsets. */
static int find_soun_trak(const uint8_t *moov, int len,
                          int *trak_off, int *trak_end) {
    int off = 0;
    while (off + 8 <= len) {
        uint32_t size = qt_read32(moov + off);
        uint32_t type = qt_read32(moov + off + 4);
        int hdrsz = 8;
        int64_t asize = size;
        if (size == 1 && off + 16 <= len) { asize = (int64_t)qt_read64(moov + off); hdrsz = 16; }
        else if (size == 0) { asize = len - off; }
        if (asize < hdrsz || off + asize > len) break;
        if (type == 0x7472616b /* 'trak' */) {
            int tb = off + hdrsz, te = off + (int)asize;
            int mdia_b, mdia_e;
            if (qt_find_child(moov, tb, te, 0x6d646961 /* 'mdia' */, &mdia_b, &mdia_e)) {
                int hdlr_b, hdlr_e;
                if (qt_find_child(moov, mdia_b, mdia_e, 0x68646c72 /* 'hdlr' */,
                                  &hdlr_b, &hdlr_e)) {
                    /* hdlr body: version(1)+flags(3)+compType(4)+compSubtype(4) */
                    if (hdlr_e - hdlr_b >= 12 &&
                        qt_read32(moov + hdlr_b + 8) == 0x736f756e /* 'soun' */) {
                        *trak_off = tb;
                        *trak_end = te;
                        return 1;
                    }
                }
            }
        }
        off += (int)asize;
    }
    return 0;
}

/* ---- esds -> ASC --------------------------------------------------------- */

/* Read an MP4 variable-length descriptor size (1-4 bytes). Sets *consumed. */
static int read_desc_len(const uint8_t *p, int avail, int *consumed) {
    int len = 0, c = 0;
    for (int i = 0; i < 4 && i < avail; i++) {
        uint8_t b = p[i];
        len = (len << 7) | (b & 0x7f);
        c++;
        if (!(b & 0x80)) { *consumed = c; return len; }
    }
    *consumed = c;
    return len;
}

/* Parse esds body -> AudioSpecificConfig (DecoderSpecificInfo payload).
 * Returns ASC length, 0 on failure. */
static int parse_esds_asc(const uint8_t *b, int boff, int bend,
                          uint8_t *asc_out, int asc_cap) {
    int p = boff + 4;  /* skip version(1)+flags(3) */
    if (p > bend || b[p] != 0x03) return 0;       /* ES_Descriptor */
    int cl;
    read_desc_len(b + p + 1, bend - (p + 1), &cl);
    p += 1 + cl;
    if (p + 3 > bend) return 0;
    int esflags = b[p + 2];                        /* ES_ID(2)+flags(1) */
    p += 3;
    if (esflags & 0x80) p += 2;                    /* streamDependenceFlag */
    if (esflags & 0x40) {                          /* URL_Flag */
        if (p >= bend) return 0;
        p += 1 + b[p];
    }
    if (esflags & 0x20) p += 2;                    /* OCRstreamFlag */
    if (p >= bend || b[p] != 0x04) return 0;       /* DecoderConfigDescriptor */
    int cl2;
    read_desc_len(b + p + 1, bend - (p + 1), &cl2);
    p += 1 + cl2;
    if (p + 13 > bend) return 0;
    p += 13;  /* objType(1)+streamType(1)+bufSize(3)+maxBR(4)+avgBR(4) */
    if (p >= bend || b[p] != 0x05) return 0;       /* DecoderSpecificInfo */
    int cl3;
    int asc_len = read_desc_len(b + p + 1, bend - (p + 1), &cl3);
    p += 1 + cl3;
    if (asc_len <= 0 || p + asc_len > bend) return 0;
    if (asc_len > asc_cap) asc_len = asc_cap;
    memcpy(asc_out, b + p, asc_len);
    return asc_len;
}

/* Find esds inside an mp4a (or enca) sample entry and extract ASC. */
static int extract_asc(const uint8_t *moov, int mp4a_b, int mp4a_e,
                       uint8_t *asc_out, int asc_cap) {
    for (int q = mp4a_b; q + 8 <= mp4a_e; q++) {
        if (qt_read32(moov + q + 4) == 0x65736473 /* 'esds' */) {
            uint32_t sz = qt_read32(moov + q);
            if (sz >= 8 && q + (int)sz <= mp4a_e) {
                return parse_esds_asc(moov, q + 8, q + (int)sz, asc_out, asc_cap);
            }
        }
    }
    return 0;
}

/* ---- stsc (sample-to-chunk) ---------------------------------------------- */

/* Expand stsc RLE into per-chunk samples_per_chunk (chunk_count entries). */
static int parse_stsc(const uint8_t *buf, int boff, int bend,
                      uint32_t *chunk_samples, uint32_t chunk_count) {
    if (boff + 8 > bend) return 0;
    uint32_t entries = qt_read32(buf + boff + 4);
    if (entries == 0) {
        for (uint32_t c = 0; c < chunk_count; c++) chunk_samples[c] = 1;
        return 1;
    }
    if (entries > 65536) return 0;
    int p = boff + 8;
    /* read all entries first (need lookahead for next first_chunk) */
    uint32_t *fc = malloc((size_t)entries * 4);
    uint32_t *spc = malloc((size_t)entries * 4);
    if (!fc || !spc) { free(fc); free(spc); return 0; }
    for (uint32_t i = 0; i < entries; i++) {
        if (p + 12 > bend) { entries = i; break; }
        fc[i] = qt_read32(buf + p);
        spc[i] = qt_read32(buf + p + 4);
        p += 12;
    }
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t first = fc[i];
        uint32_t next = (i + 1 < entries) ? fc[i + 1] : (chunk_count + 1);
        if (first == 0) first = 1;
        for (uint32_t c = first; c < next; c++) {
            if (c >= 1 && c <= chunk_count) chunk_samples[c - 1] = spc[i];
        }
    }
    free(fc); free(spc);
    return 1;
}

/* ---- chunk lookup -------------------------------------------------------- */

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000ff) << 24) | ((v & 0x0000ff00) << 8) |
           ((v & 0x00ff0000) >> 8)  | ((v & 0xff000000) >> 24);
}

static int find_chunk(mp4_audio_t *m, uint32_t idx) {
    int lo = 0, hi = (int)m->chunk_count - 1, ans = 0;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (m->chunk_first[mid] <= idx) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return ans;
}

/* ---- public API ---------------------------------------------------------- */

int mp4_audio_open(const char *path, mp4_audio_t *m) {
    memset(m, 0, sizeof(*m));
    m->stsz_cache_chunk = (uint32_t)-1;

    int fd;
    int64_t moov_len, moov_body_file_off;
    uint8_t *moov = read_moov_fd(path, &fd, &moov_len, &moov_body_file_off);
    if (!moov) return -1;

    int rv = -1;  /* fail-closed; cleared only on full success */
    int trak_b, trak_e;
    if (!find_soun_trak(moov, (int)moov_len, &trak_b, &trak_e)) goto done;

    /* mdhd timescale */
    int mdia_b, mdia_e;
    if (!qt_find_child(moov, trak_b, trak_e, 0x6d646961, &mdia_b, &mdia_e)) goto done;
    int mdhd_b, mdhd_e;
    if (!qt_find_child(moov, mdia_b, mdia_e, 0x6d646864 /* 'mdhd' */, &mdhd_b, &mdhd_e)) goto done;
    if (mdhd_e - mdhd_b < 24) goto done;
    {
        int version = moov[mdhd_b];
        m->timescale = qt_read32(moov + mdhd_b + (version == 0 ? 12 : 20));
    }
    if (m->timescale == 0) goto done;

    /* minf -> stbl */
    int minf_b, minf_e;
    if (!qt_find_child(moov, mdia_b, mdia_e, 0x6d696e66, &minf_b, &minf_e)) goto done;
    int stbl_b, stbl_e;
    if (!qt_find_child(moov, minf_b, minf_e, 0x7374626c, &stbl_b, &stbl_e)) goto done;

    /* stco or co64 -> chunk_off + chunk_count */
    int stco_b, stco_e, is64 = 0;
    if (qt_find_child(moov, stbl_b, stbl_e, 0x7374636f /* 'stco' */, &stco_b, &stco_e)) is64 = 0;
    else if (qt_find_child(moov, stbl_b, stbl_e, 0x636f3634 /* 'co64' */, &stco_b, &stco_e)) is64 = 1;
    else goto done;
    {
        uint32_t count = qt_read32(moov + stco_b + 4);
        if (count == 0 || count > CHUNK_MAX) goto done;
        m->chunk_count = count;
        m->chunk_off = malloc((size_t)count * sizeof(uint64_t));
        m->chunk_samples = malloc((size_t)count * sizeof(uint32_t));
        m->chunk_first = malloc((size_t)count * sizeof(uint32_t));
        if (!m->chunk_off || !m->chunk_samples || !m->chunk_first) goto done;
        int got = 0;
        int p = stco_b + 8;
        for (uint32_t i = 0; i < count; i++) {
            if (is64) {
                if (p + 8 > stco_e) break;
                m->chunk_off[i] = qt_read64(moov + p); p += 8;
            } else {
                if (p + 4 > stco_e) break;
                m->chunk_off[i] = (uint64_t)qt_read32(moov + p); p += 4;
            }
            got++;
        }
        if (got != (int)count) goto done;
    }

    /* stsc -> chunk_samples */
    {
        int stsc_b, stsc_e;
        if (!qt_find_child(moov, stbl_b, stbl_e, 0x73747363 /* 'stsc' */, &stsc_b, &stsc_e)) goto done;
        if (!parse_stsc(moov, stsc_b, stsc_e, m->chunk_samples, m->chunk_count)) goto done;
    }

    /* chunk_first = cumulative; sample_count = total */
    {
        uint32_t cum = 0;
        for (uint32_t c = 0; c < m->chunk_count; c++) {
            m->chunk_first[c] = cum;
            cum += m->chunk_samples[c];
        }
        m->sample_count = cum;
    }

    /* stsz -> const_size, count (authoritative sample_count), entry-table offset */
    {
        int stsz_b, stsz_e;
        if (!qt_find_child(moov, stbl_b, stbl_e, 0x7374737a /* 'stsz' */, &stsz_b, &stsz_e)) goto done;
        if (stsz_e - stsz_b < 12) goto done;
        m->const_size = qt_read32(moov + stsz_b + 4);
        uint32_t sz_count = qt_read32(moov + stsz_b + 8);
        if (sz_count == 0) sz_count = m->sample_count;  /* fall back to chunk total */
        if (sz_count > 100000000u) goto done;            /* sanity */
        m->sample_count = sz_count;
        /* absolute file offset of the per-sample size table (const_size==0 path) */
        m->stsz_file_off = moov_body_file_off + stsz_b + 12;
    }

    /* stts -> RLE for ms<->sample */
    {
        int stts_b, stts_e;
        if (qt_find_child(moov, stbl_b, stbl_e, 0x73747473 /* 'stts' */, &stts_b, &stts_e)) {
            uint32_t entries = qt_read32(moov + stts_b + 4);
            if (entries > 0 && entries <= STTS_ENTRY_MAX) {
                m->stts_count = malloc((size_t)entries * 4);
                m->stts_delta = malloc((size_t)entries * 4);
                if (m->stts_count && m->stts_delta) {
                    int p = stts_b + 8, n = 0;
                    for (uint32_t i = 0; i < entries; i++) {
                        if (p + 8 > stts_e) break;
                        m->stts_count[n] = qt_read32(moov + p);
                        m->stts_delta[n] = qt_read32(moov + p + 4);
                        p += 8; n++;
                    }
                    m->stts_n = n;
                }
            }
        }
    }

    /* stsd -> mp4a/enca -> esds -> ASC */
    {
        int stsd_b, stsd_e;
        if (!qt_find_child(moov, stbl_b, stbl_e, 0x73747364 /* 'stsd' */, &stsd_b, &stsd_e)) goto done;
        int entry_b = stsd_b + 8;  /* skip version(1)+flags(3)+entry_count(4) */
        int mp4a_b = -1, mp4a_e = 0;
        if (!qt_find_child(moov, entry_b, stsd_e, 0x6d703461 /* 'mp4a' */, &mp4a_b, &mp4a_e)) {
            qt_find_child(moov, entry_b, stsd_e, 0x656e6361 /* 'enca' */, &mp4a_b, &mp4a_e);
        }
        if (mp4a_b < 0) goto done;
        m->asc_len = extract_asc(moov, mp4a_b, mp4a_e, m->asc, (int)sizeof(m->asc));
        if (m->asc_len <= 0) goto done;
    }

    m->fd = fd;
    rv = 0;

done:
    free(moov);
    if (rv != 0) {
        close(fd);
        mp4_audio_close(m);
    }
    return rv;
}

int mp4_audio_read_sample(mp4_audio_t *m, uint32_t idx, uint8_t *dst, int cap) {
    if (idx >= m->sample_count) return 0;  /* EOF */
    int c = find_chunk(m, idx);
    uint32_t w = idx - m->chunk_first[c];
    int64_t frame_off;
    int frame_size;
    if (m->const_size != 0) {
        frame_off = (int64_t)m->chunk_off[c] + (int64_t)w * m->const_size;
        frame_size = (int)m->const_size;
    } else {
        if (m->stsz_cache_chunk != c) {
            uint32_t need = m->chunk_samples[c];
            if (need == 0) return -1;
            if (m->stsz_cache_n < need) {
                uint32_t *nb = realloc(m->stsz_cache, (size_t)need * 4);
                if (!nb) return -1;
                m->stsz_cache = nb;
                m->stsz_cache_n = need;
            }
            int64_t so = m->stsz_file_off + (int64_t)m->chunk_first[c] * 4;
            int ok = pread_full(m->fd, m->stsz_cache, (size_t)need * 4, so);
            if (!ok) return -1;
            /* stsz entries are big-endian; convert to native for arithmetic. */
            for (uint32_t k = 0; k < need; k++) m->stsz_cache[k] = bswap32(m->stsz_cache[k]);
            m->stsz_cache_chunk = c;
        }
        uint32_t off_within = 0;
        for (uint32_t k = 0; k < w; k++) off_within += m->stsz_cache[k];
        frame_off = (int64_t)m->chunk_off[c] + off_within;
        frame_size = (int)m->stsz_cache[w];
    }
    if (frame_size <= 0 || frame_size > cap) return -1;
    if (!pread_full(m->fd, dst, (size_t)frame_size, frame_off)) return -1;
    return frame_size;
}

uint32_t mp4_audio_seek_sample(mp4_audio_t *m, int64_t ms) {
    if (ms <= 0 || m->stts_n == 0) return 0;
    int64_t target = ms * (int64_t)m->timescale / 1000;
    int64_t acc_time = 0;
    uint32_t acc_samples = 0;
    for (int i = 0; i < m->stts_n; i++) {
        int64_t seg = (int64_t)m->stts_count[i] * m->stts_delta[i];
        if (target < acc_time + seg) {
            int64_t into = target - acc_time;
            if (m->stts_delta[i] > 0) into /= m->stts_delta[i];
            uint32_t s = acc_samples + (uint32_t)into;
            if (s >= m->sample_count) s = m->sample_count ? m->sample_count - 1 : 0;
            return s;
        }
        acc_time += seg;
        acc_samples += m->stts_count[i];
    }
    return m->sample_count ? m->sample_count - 1 : 0;
}

void mp4_audio_close(mp4_audio_t *m) {
    if (m->fd > 0) { close(m->fd); m->fd = 0; }
    free(m->chunk_off);       m->chunk_off = NULL;
    free(m->chunk_samples);   m->chunk_samples = NULL;
    free(m->chunk_first);     m->chunk_first = NULL;
    free(m->stsz_cache);      m->stsz_cache = NULL; m->stsz_cache_n = 0; m->stsz_cache_chunk = (uint32_t)-1;
    free(m->stts_count);      m->stts_count = NULL;
    free(m->stts_delta);      m->stts_delta = NULL;
    m->stts_n = 0;
}