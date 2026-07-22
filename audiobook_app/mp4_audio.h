/* mp4_audio.h — RAM-frugal MP4/M4B audio demux for AAC playback.
 *
 * Reads an MP4 container, locates the 'soun' (audio) trak, and exposes a
 * sample-granularity read/seek API for feeding AAC frames to libfdk-aac.
 *
 * RAM strategy (device has ~56MB total, ~20MB free): the moov atom is slurped
 * into a transient buffer only long enough to walk boxes and build a
 * CHUNK-granularity index (chunk offsets + samples-per-chunk). Per-sample sizes
 * are NOT retained: if stsz declares a constant sample size, sample offsets are
 * computed arithmetically; otherwise a small per-chunk stsz slice is pread
 * on demand from the file (the stsz body's absolute file offset is recorded
 * before moov is freed). stts is kept as a small RLE table for ms<->sample.
 *
 * This module is self-contained (duplicates a few small box primitives also
 * present in tags.c) so it touches no existing code and cannot regress the
 * scanner/chapter path.
 */
#ifndef MP4_AUDIO_H
#define MP4_AUDIO_H

#include <stdint.h>
#include <stddef.h>   /* size_t for moov_map_len */

typedef struct {
    int fd;                 /* kept-open file fd for pread of AAC frames */

    /* moov body mmap (PROT_READ MAP_PRIVATE, page-aligned). When non-NULL the
     * moov is demand-paged from the SD file (no eager 16MB heap slurp) and
     * stays mapped for the playback lifetime, munmap'd on close. NULL/0 when
     * read_moov_fd fell back to malloc (small moov) — in that case the moov
     * buffer is freed at the end of open (it is not retained). */
    uint8_t *moov_map;
    size_t   moov_map_len;

    uint32_t timescale;     /* soun trak mdhd timescale (ticks/sec) */
    uint32_t sample_count;  /* total AAC frames (from stsz count) */
    uint32_t const_size;    /* stsz constant sample size, 0 if variable */

    /* chunk-granularity index (small — N_chunks entries) */
    uint32_t chunk_count;
    uint64_t *chunk_off;    /* malloc: file offset of each chunk (stco/co64) */
    uint32_t *chunk_samples;/* malloc: AAC frames in each chunk (from stsc) */
    uint32_t *chunk_first;  /* malloc: first sample index of each chunk (cumulative) */

    /* variable-size support: absolute file offset of the stsz BODY (the 4-byte
     * fields after version/flags/sample_size/count). Used to pread a chunk's
     * sample-size slice on demand when const_size == 0. */
    int64_t  stsz_file_off;

    /* per-chunk stsz cache (avoids re-reading sizes for sequential playback) */
    uint32_t *stsz_cache;   /* malloc: sizes for the currently-cached chunk */
    uint32_t  stsz_cache_chunk; /* chunk index currently cached, or UINT32_MAX */
    uint32_t  stsz_cache_n;     /* entries in cache */

    /* AudioSpecificConfig (esds DecoderSpecificInfo) for aacDecoder_ConfigRaw */
    uint8_t  asc[32];
    int      asc_len;

    /* stts RLE (small) for ms<->sample conversion (seek) */
    uint32_t *stts_count;   /* malloc: per-entry run length (sample count) */
    uint32_t *stts_delta;   /* malloc: per-entry delta (in timescale ticks) */
    int      stts_n;        /* number of RLE entries stored */
} mp4_audio_t;

/* Open path, locate the soun trak, build the chunk index. Returns 0 on success,
 * negative on error (file/format/OOM/oversize). On success the fd is kept open;
 * call mp4_audio_close to release it. */
int  mp4_audio_open(const char *path, mp4_audio_t *m);

/* Read one AAC sample (frame) by index into dst (cap bytes). Returns frame size
 * in bytes, 0 on EOF (idx >= sample_count), -1 on read error. Sequential reads
 * are cheap (per-chunk stsz cached). */
int  mp4_audio_read_sample(mp4_audio_t *m, uint32_t idx, uint8_t *dst, int cap);

/* Convert a millisecond offset to a sample index (nearest frame at or before
 * that time). Returns 0 for ms<=0. */
uint32_t mp4_audio_seek_sample(mp4_audio_t *m, int64_t ms);

/* Free all malloc'd state and close the fd. Safe to call on a zeroed struct. */
void mp4_audio_close(mp4_audio_t *m);

#endif /* MP4_AUDIO_H */