#ifndef R1_AB_DECODER_H
#define R1_AB_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dr_flac.h"
#include "dr_wav.h"
#include "minimp3_ex.h"

typedef enum decoder_kind {
    DECODER_KIND_NONE = 0,
    DECODER_KIND_WAV,
    DECODER_KIND_FLAC,
    DECODER_KIND_MP3,
    DECODER_KIND_SILENCE,
} decoder_kind;

typedef struct audiobook_decoder {
    decoder_kind kind;
    char path[512];
    unsigned sample_rate;
    unsigned channels;
    uint64_t total_frames;
    uint64_t current_frame;
    bool eof;
    union {
        drwav wav;
        drflac *flac;
        mp3dec_ex_t mp3;
    } u;
} audiobook_decoder;

int decoder_open(audiobook_decoder *dec, const char *path);
void decoder_close(audiobook_decoder *dec);
int decoder_seek_ms(audiobook_decoder *dec, uint64_t position_ms);
size_t decoder_read_frames(audiobook_decoder *dec, int16_t *dst, size_t frames);
uint64_t decoder_duration_ms(const audiobook_decoder *dec);
unsigned decoder_sample_rate(const audiobook_decoder *dec);
unsigned decoder_channels(const audiobook_decoder *dec);
bool decoder_is_eof(const audiobook_decoder *dec);

#endif
