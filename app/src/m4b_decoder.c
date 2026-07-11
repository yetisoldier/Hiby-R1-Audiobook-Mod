#include "m4b_decoder.h"

#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unicode_support.h"

static uint64_t frames_to_ms(uint64_t frames, unsigned rate) {
    return rate > 0 ? (frames * 1000u) / rate : 0u;
}

static uint64_t ms_to_frames(uint64_t ms, unsigned rate, uint32_t frame_size) {
    if (rate == 0 || frame_size == 0) return 0;
    return (ms * rate / 1000u) / frame_size;
}

static uint32_t adts_sample_rate_from_index(unsigned index) {
    static const uint32_t table[] = {
        96000u, 88200u, 64000u, 48000u, 44100u, 32000u, 24000u,
        22050u, 16000u, 12000u, 11025u, 8000u, 7350u,
    };
    return index < sizeof(table) / sizeof(table[0]) ? table[index] : 0u;
}

static int adts_parse_header(const uint8_t *buf, size_t len, uint32_t *frame_len, unsigned *sample_rate, unsigned *channels) {
    if (!buf || len < 7 || (buf[0] != 0xFF) || ((buf[1] & 0xF0) != 0xF0)) return -1;
    unsigned sr_index = (unsigned)((buf[2] >> 2) & 0x0F);
    unsigned ch = (unsigned)(((buf[2] & 0x01) << 2) | ((buf[3] >> 6) & 0x03));
    uint32_t size = (uint32_t)(((buf[3] & 0x03) << 11) | (buf[4] << 3) | (buf[5] >> 5));
    if (size < 7) return -1;
    if (frame_len) *frame_len = size;
    if (sample_rate) *sample_rate = adts_sample_rate_from_index(sr_index);
    if (channels) *channels = ch;
    return 0;
}

static int adts_scan_frames(m4b_decoder_state *state) {
    state->adts_file = faad_fopen(state->path, "rb");
    if (!state->adts_file) return -1;

    uint8_t header[9];
    uint64_t offset = 0;
    state->adts_frame_index = 0;
    while (1) {
        if (fseek(state->adts_file, (long)offset, SEEK_SET) != 0) return -1;
        size_t got = fread(header, 1, sizeof(header), state->adts_file);
        if (got < 7) break;
        uint32_t frame_len = 0;
        unsigned sample_rate = 0;
        unsigned channels = 0;
        if (adts_parse_header(header, got, &frame_len, &sample_rate, &channels) != 0) return -1;
        if (state->sample_rate == 0) state->sample_rate = sample_rate;
        if (state->channels == 0) state->channels = channels;
        if (frame_len == 0) return -1;
        if (state->adts_frame_count == state->adts_frame_cap) {
            size_t new_cap = state->adts_frame_cap ? state->adts_frame_cap * 2 : 1024;
            state->adts_frames = ab_xrealloc(state->adts_frames, new_cap * sizeof(*state->adts_frames));
            state->adts_frame_cap = new_cap;
        }
        state->adts_frames[state->adts_frame_count].offset = (uint32_t)offset;
        state->adts_frames[state->adts_frame_count].len = frame_len;
        if (frame_len > state->adts_frame_max_size) state->adts_frame_max_size = frame_len;
        state->adts_frame_count++;
        offset += frame_len;
    }

    if (state->adts_frame_count == 0) return -1;
    state->adts_frame_buf = ab_xcalloc(state->adts_frame_max_size, sizeof(uint8_t));
    if (fseek(state->adts_file, (long)state->adts_frames[0].offset, SEEK_SET) != 0) return -1;

    size_t first_len = state->adts_frames[0].len;
    if (fread(state->adts_frame_buf, 1, first_len, state->adts_file) != first_len) return -1;

    state->decoder = NeAACDecOpen();
    if (!state->decoder) return -1;
    NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(state->decoder);
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;
    NeAACDecSetConfiguration(state->decoder, config);

    unsigned long rate = 0;
    unsigned char channels = 0;
    unsigned long consumed = NeAACDecInit(state->decoder, state->adts_frame_buf, (unsigned long)first_len, &rate, &channels);
    if ((long)consumed < 0) return -1;
    state->sample_rate = rate > 0 ? (unsigned)rate : state->sample_rate;
    state->channels = channels > 0 ? channels : (state->channels > 0 ? state->channels : 2u);

    NeAACDecFrameInfo frame_info = {0};
    void *pcm = NeAACDecDecode(state->decoder, &frame_info, state->adts_frame_buf, (unsigned long)first_len);
    if (!pcm || frame_info.error != 0 || frame_info.samples == 0) {
        state->pcm_frame_size = 1024;
    } else if (frame_info.channels > 0) {
        state->pcm_frame_size = (uint32_t)(frame_info.samples / frame_info.channels);
    } else {
        state->pcm_frame_size = 1024;
    }
    if (state->pcm_frame_size == 0) state->pcm_frame_size = 1024;
    state->total_frames = (uint64_t)state->adts_frame_count * state->pcm_frame_size;
    state->adts_frame_index = 0;
    state->current_frame = 0;
    state->eof = false;
    NeAACDecPostSeekReset(state->decoder, 1);
    return 0;
}

static int read_be32(FILE *fp, uint32_t *out) {
    uint8_t buf[4];
    if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) return -1;
    *out = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    return 0;
}

static int read_be64(FILE *fp, uint64_t *out) {
    uint8_t buf[8];
    if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) return -1;
    *out = ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) | ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) | ((uint64_t)buf[6] << 8) | (uint64_t)buf[7];
    return 0;
}

static int parse_chpl_box(FILE *fp, uint64_t box_end, m4b_decoder_state *state) {
    uint8_t version_flags[4];
    if (fread(version_flags, 1, sizeof(version_flags), fp) != sizeof(version_flags)) return -1;
    int chapter_count = fgetc(fp);
    if (chapter_count <= 0) return 0;
    m4b_chapter *chapters = ab_xcalloc((size_t)chapter_count, sizeof(*chapters));
    for (int i = 0; i < chapter_count; i++) {
        uint64_t start_100ns = 0;
        if (read_be64(fp, &start_100ns) != 0) {
            free(chapters);
            return -1;
        }
        int title_len = fgetc(fp);
        if (title_len < 0) {
            free(chapters);
            return -1;
        }
        char title[256];
        size_t to_read = (size_t)title_len;
        if (to_read >= sizeof(title)) to_read = sizeof(title) - 1;
        if (fread(title, 1, to_read, fp) != to_read) {
            free(chapters);
            return -1;
        }
        title[to_read] = '\0';
        if ((size_t)title_len > to_read) {
            if (fseek(fp, (long)(title_len - (int)to_read), SEEK_CUR) != 0) {
                free(chapters);
                return -1;
            }
        }
        chapters[i].start_ms = start_100ns / 10000u;
        chapters[i].end_ms = chapters[i].start_ms;
        ab_copy_str(chapters[i].title, sizeof(chapters[i].title), title);
    }
    for (int i = 0; i < chapter_count; i++) {
        chapters[i].end_ms = (i + 1 < chapter_count) ? chapters[i + 1].start_ms : frames_to_ms(state->total_frames, state->sample_rate);
    }
    free(state->chapters);
    state->chapters = chapters;
    state->chapter_count = (size_t)chapter_count;
    (void)box_end;
    return 0;
}

static int scan_mp4_boxes(FILE *fp, uint64_t box_end, m4b_decoder_state *state) {
    while ((uint64_t)ftell(fp) < box_end) {
        long box_start = ftell(fp);
        uint32_t size32 = 0;
        if (read_be32(fp, &size32) != 0) return -1;
        char type[5] = {0};
        if (fread(type, 1, 4, fp) != 4) return -1;
        uint64_t size = size32;
        uint64_t header = 8;
        if (size32 == 1) {
            if (read_be64(fp, &size) != 0) return -1;
            header = 16;
        } else if (size32 == 0) {
            size = box_end - (uint64_t)box_start;
        }
        if (size < header) return -1;
        uint64_t next = (uint64_t)box_start + size;
        if (next > box_end) next = box_end;
        if (!strcmp(type, "moov") || !strcmp(type, "udta") || !strcmp(type, "trak") ||
            !strcmp(type, "mdia") || !strcmp(type, "minf") || !strcmp(type, "stbl")) {
            if (scan_mp4_boxes(fp, next, state) != 0) return -1;
        } else if (!strcmp(type, "chpl")) {
            if (parse_chpl_box(fp, next, state) != 0) return -1;
        } else {
            if (fseek(fp, (long)next, SEEK_SET) != 0) return -1;
        }
    }
    return 0;
}

static int parse_mp4_chapters(m4b_decoder_state *state) {
    FILE *fp = faad_fopen(state->path, "rb");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long end = ftell(fp);
    if (end <= 0) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    int rc = scan_mp4_boxes(fp, (uint64_t)end, state);
    fclose(fp);
    return rc;
}

static int open_mp4(m4b_decoder_state *state) {
    if (mp4read_open(state->path) != 0) return -1;
    state->decoder = NeAACDecOpen();
    if (!state->decoder) {
        mp4read_close();
        return -1;
    }
    NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(state->decoder);
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;
    NeAACDecSetConfiguration(state->decoder, config);

    unsigned long rate = 0;
    unsigned char channels = 0;
    if (NeAACDecInit2(state->decoder, mp4config.asc.buf, mp4config.asc.size, &rate, &channels) < 0) {
        NeAACDecClose(state->decoder);
        state->decoder = NULL;
        mp4read_close();
        return -1;
    }
    mp4AudioSpecificConfig mp4ASC = {0};
    state->pcm_frame_size = 1024;
    if (mp4config.asc.size && NeAACDecAudioSpecificConfig(mp4config.asc.buf, mp4config.asc.size, &mp4ASC) >= 0) {
        if (mp4ASC.frameLengthFlag == 1) state->pcm_frame_size = 960;
        if (mp4ASC.sbr_present_flag == 1 || mp4ASC.forceUpSampling) state->pcm_frame_size *= 2;
    }
    state->sample_rate = rate > 0 ? (unsigned)rate : mp4config.samplerate;
    state->channels = channels > 0 ? channels : mp4config.channels;
    state->total_frames = mp4config.samples;
    state->current_frame = 0;
    state->eof = false;
    NeAACDecPostSeekReset(state->decoder, 1);
    (void)parse_mp4_chapters(state);
    return 0;
}

static void clear_chapters(m4b_decoder_state *state) {
    free(state->chapters);
    state->chapters = NULL;
    state->chapter_count = 0;
}

int m4b_decoder_open(m4b_decoder_state *state, const char *path) {
    if (!state || !path || !path[0]) return -1;
    memset(state, 0, sizeof(*state));
    ab_copy_str(state->path, sizeof(state->path), path);
    state->sample_rate = 0;
    state->channels = 0;
    state->pcm_frame_size = 1024;

    if (ab_ends_with(path, ".aac")) {
        state->mp4_container = false;
        if (adts_scan_frames(state) != 0) {
            m4b_decoder_close(state);
            return -1;
        }
        state->open = true;
        return 0;
    }

    state->mp4_container = true;
    if (open_mp4(state) != 0) {
        memset(state, 0, sizeof(*state));
        return -1;
    }
    state->open = true;
    return 0;
}

static int read_adts_frame(m4b_decoder_state *state, int16_t *dst, size_t frames) {
    if (!state->adts_file || state->adts_frame_index >= state->adts_frame_count) {
        state->eof = true;
        return 0;
    }
    frame_info_t info = state->adts_frames[state->adts_frame_index];
    if (fseek(state->adts_file, (long)info.offset, SEEK_SET) != 0) {
        state->eof = true;
        return 0;
    }
    if (info.len > state->adts_frame_max_size) {
        state->eof = true;
        return 0;
    }
    if (fread(state->adts_frame_buf, 1, info.len, state->adts_file) != info.len) {
        state->eof = true;
        return 0;
    }
    NeAACDecFrameInfo frame_info = {0};
    void *pcm = NeAACDecDecode(state->decoder, &frame_info, state->adts_frame_buf, (unsigned long)info.len);
    if (!pcm || frame_info.error != 0 || frame_info.samples == 0 || frame_info.channels == 0) {
        state->eof = true;
        return 0;
    }
    size_t got = (size_t)(frame_info.samples / frame_info.channels);
    if (got > frames) got = frames;
    memcpy(dst, pcm, got * frame_info.channels * sizeof(int16_t));
    state->adts_frame_index++;
    state->current_frame += got;
    state->eof = state->adts_frame_index >= state->adts_frame_count;
    return got;
}

static int read_mp4_frame(m4b_decoder_state *state, int16_t *dst, size_t frames) {
    if (mp4read_frame() != 0) {
        state->eof = true;
        return 0;
    }
    NeAACDecFrameInfo frame_info = {0};
    void *pcm = NeAACDecDecode(state->decoder, &frame_info, mp4config.bitbuf.data, (unsigned long)mp4config.bitbuf.size);
    if (!pcm || frame_info.error != 0 || frame_info.samples == 0 || frame_info.channels == 0) {
        state->eof = true;
        return 0;
    }
    size_t got = (size_t)(frame_info.samples / frame_info.channels);
    if (got > frames) got = frames;
    memcpy(dst, pcm, got * frame_info.channels * sizeof(int16_t));
    state->current_frame += got;
    state->eof = mp4config.frame.current >= mp4config.frame.nsamples;
    return got;
}

size_t m4b_decoder_read_frames(m4b_decoder_state *state, int16_t *dst, size_t frames) {
    if (!state || !state->open || !dst || frames == 0) return 0;
    if (state->mp4_container) return (size_t)read_mp4_frame(state, dst, frames);
    return (size_t)read_adts_frame(state, dst, frames);
}

int m4b_decoder_seek_ms(m4b_decoder_state *state, uint64_t position_ms) {
    if (!state || !state->open) return -1;
    if (state->mp4_container) {
        uint64_t frame = ms_to_frames(position_ms, state->sample_rate, state->pcm_frame_size);
        if (mp4config.frame.nsamples > 0 && frame >= mp4config.frame.nsamples) frame = mp4config.frame.nsamples - 1;
        if (mp4read_seek((uint32_t)frame) != 0) return -1;
        NeAACDecPostSeekReset(state->decoder, 1);
        state->current_frame = frame * state->pcm_frame_size;
        state->eof = false;
        return 0;
    }
    uint64_t frame = ms_to_frames(position_ms, state->sample_rate, state->pcm_frame_size);
    if (frame >= state->adts_frame_count) frame = state->adts_frame_count > 0 ? state->adts_frame_count - 1 : 0;
    state->adts_frame_index = (size_t)frame;
    state->current_frame = frame * state->pcm_frame_size;
    if (fseek(state->adts_file, (long)state->adts_frames[state->adts_frame_index].offset, SEEK_SET) != 0) return -1;
    NeAACDecPostSeekReset(state->decoder, 1);
    state->eof = false;
    return 0;
}

uint64_t m4b_decoder_duration_ms(const m4b_decoder_state *state) {
    if (!state || !state->open) return 0;
    return frames_to_ms(state->total_frames, state->sample_rate);
}

void m4b_decoder_close(m4b_decoder_state *state) {
    if (!state) return;
    if (state->decoder) {
        NeAACDecClose(state->decoder);
        state->decoder = NULL;
    }
    if (state->mp4_container) {
        mp4read_close();
    }
    if (state->adts_file) {
        fclose(state->adts_file);
        state->adts_file = NULL;
    }
    free(state->adts_frames);
    free(state->adts_frame_buf);
    clear_chapters(state);
    memset(state, 0, sizeof(*state));
}

size_t m4b_decoder_chapter_count(const m4b_decoder_state *state) {
    return state ? state->chapter_count : 0;
}

const m4b_chapter *m4b_decoder_chapters(const m4b_decoder_state *state) {
    return state ? state->chapters : NULL;
}
