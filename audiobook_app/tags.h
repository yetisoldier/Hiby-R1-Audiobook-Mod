/* tags.h — minimal audio tag readers for MP3 (ID3v2) and M4B (QuickTime atoms).
 *
 * Extracts: title, artist, album, narrator, track_number, disc_number,
 * duration_ms, and embedded chapter count. Minimal parsers — just enough
 * for library scanning, not for playback.
 */

#ifndef AUDIOBOOK_TAGS_H
#define AUDIOBOOK_TAGS_H

#include <stdint.h>

/* Supported audio file extensions */
#define AUDIO_EXT_MP3   1
#define AUDIO_EXT_M4B   2
#define AUDIO_EXT_M4A   3
#define AUDIO_EXT_AAC   4
#define AUDIO_EXT_WAV   5
#define AUDIO_EXT_FLAC  6
#define AUDIO_EXT_OGG   7

typedef struct {
    char title[512];
    char artist[256];      /* author/narrator candidate */
    char album[512];       /* book title candidate */
    char composer[256];    /* narrator candidate */
    char genre[128];
    char description[2048]; /* publisher summary: MP3 COMM / M4B desc/ldes */
    int track_number;
    int disc_number;
    int64_t duration_ms;   /* 0 if unknown */
    int embedded_chapters;  /* embedded/synthesized chapter count hint */
    int64_t file_size;
    int file_mtime;
} audio_tags_t;

/* Determine audio file type from filename extension. Returns AUDIO_EXT_* or 0. */
int audio_file_type(const char *filename);

/* Parse tags from a file. Returns 0 on success, -1 on error.
 * Fills in the audio_tags_t structure with whatever metadata is found.
 * Fields not found are left as zero/empty. */
int audio_read_tags(const char *path, audio_tags_t *out);

/* Estimate MP3 duration from file size + bitrate (fallback when no header
 * duration is available). Returns duration_ms. */
int64_t audio_estimate_mp3_duration(int64_t file_size, int bitrate);

/* Chapter callback for audio_read_chapters. ordinal is 1-based; title is
 * UTF-8 (may be "Chapter N" fallback); start_ms/end_ms are wall-clock times
 * within the file. Return non-zero to stop iterating. */
typedef int (*chapter_cb)(int ordinal, const char *title,
                          int64_t start_ms, int64_t end_ms, void *ctx);

/* Read embedded chapters from MP3 (ID3v2 CHAP/CTOC) or M4B/M4A (Nero chpl or
 * QuickTime chapter track). Calls cb for each chapter. Returns chapter count,
 * or 0 if none are found (caller should fall back to a placeholder). */
int audio_read_chapters(const char *path, chapter_cb cb, void *ctx);

/* Extract the embedded cover art from a file's metadata and write it as a
 * JPEG (<out_base>.jpg) or PNG (<out_base>.png) depending on the detected
 * image type, copying the actual path written into out_path. M4B/M4A: the
 * moov/udta/meta/ilst/covr 'data' atom (iTunes type flag 0x0d = JPEG, 0x0e =
 * PNG; confirmed against the byte signature). MP3: an ID3v2 APIC frame (the
 * image bytes after the encoding/MIME/pic-type/description prefix). Unsupported
 * formats are skipped. Returns 1 if an image was written, 0 if no supported
 * embedded cover was found or the write failed. */
int audio_extract_cover(const char *track_path, const char *out_base,
                        char *out_path, size_t out_path_len);

#endif /* AUDIOBOOK_TAGS_H */
