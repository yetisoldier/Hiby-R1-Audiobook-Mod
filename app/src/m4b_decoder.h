#ifndef R1_AB_M4B_DECODER_H
#define R1_AB_M4B_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "neaacdec.h"
#include "mp4read.h"

typedef struct m4b_chapter {
    uint64_t start_ms;
    uint64_t end_ms;
    char title[128];
} m4b_chapter;

typedef struct m4b_frame_info {
    uint64_t len;
    uint64_t offset;
} m4b_frame_info_t;

typedef struct m4b_decoder_state {
    bool open;
    bool mp4_container;
    char path[512];
    FILE *adts_file;
    m4b_frame_info_t *adts_frames;
    size_t adts_frame_count;
    size_t adts_frame_cap;
    size_t adts_frame_index;
    size_t adts_frame_max_size;
    uint8_t *adts_frame_buf;
    uint64_t current_frame;
    uint64_t total_frames;
    uint32_t pcm_frame_size;
    unsigned sample_rate;
    unsigned channels;
    bool eof;
    NeAACDecHandle decoder;
    m4b_chapter *chapters;
    size_t chapter_count;
} m4b_decoder_state;

int m4b_decoder_open(m4b_decoder_state *state, const char *path);
size_t m4b_decoder_read_frames(m4b_decoder_state *state, int16_t *dst, size_t frames);
int m4b_decoder_seek_ms(m4b_decoder_state *state, uint64_t position_ms);
uint64_t m4b_decoder_duration_ms(const m4b_decoder_state *state);
void m4b_decoder_close(m4b_decoder_state *state);
size_t m4b_decoder_chapter_count(const m4b_decoder_state *state);
const m4b_chapter *m4b_decoder_chapters(const m4b_decoder_state *state);

#endif
