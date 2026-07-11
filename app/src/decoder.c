#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define MINIMP3_IMPLEMENTATION

#include "decoder.h"
#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dr_flac.h"
#include "dr_wav.h"
#include "minimp3_ex.h"

static uint64_t frames_to_ms(uint64_t frames, unsigned rate) {
    return rate > 0 ? (frames * 1000u) / rate : 0u;
}

static uint64_t ms_to_frames(uint64_t ms, unsigned rate) {
    return rate > 0 ? (ms * rate) / 1000u : 0u;
}

static void decoder_reset(audiobook_decoder *dec) {
    if (!dec) return;
    memset(dec, 0, sizeof(*dec));
    dec->kind = DECODER_KIND_NONE;
}

int decoder_open(audiobook_decoder *dec, const char *path) {
    if (!dec || !path || !path[0]) return -1;
    decoder_reset(dec);
    ab_copy_str(dec->path, sizeof(dec->path), path);

    if (ab_ends_with(path, ".wav")) {
        if (!drwav_init_file(&dec->u.wav, path, NULL)) return -1;
        dec->kind = DECODER_KIND_WAV;
        dec->sample_rate = dec->u.wav.sampleRate;
        dec->channels = dec->u.wav.channels;
        dec->total_frames = dec->u.wav.totalPCMFrameCount;
        return 0;
    }

    if (ab_ends_with(path, ".flac")) {
        dec->u.flac = drflac_open_file(path, NULL);
        if (!dec->u.flac) return -1;
        dec->kind = DECODER_KIND_FLAC;
        dec->sample_rate = dec->u.flac->sampleRate;
        dec->channels = dec->u.flac->channels;
        dec->total_frames = dec->u.flac->totalPCMFrameCount;
        return 0;
    }

    if (ab_ends_with(path, ".mp3") || ab_ends_with(path, ".m4a") || ab_ends_with(path, ".m4b") ||
        ab_ends_with(path, ".aac")) {
        if (mp3dec_ex_open(&dec->u.mp3, path, MP3D_SEEK_TO_SAMPLE) != 0) return -1;
        dec->kind = DECODER_KIND_MP3;
        dec->sample_rate = (unsigned)(dec->u.mp3.info.hz > 0 ? dec->u.mp3.info.hz : 44100);
        dec->channels = (unsigned)(dec->u.mp3.info.channels > 0 ? dec->u.mp3.info.channels : 2);
        dec->total_frames = dec->channels > 0 ? dec->u.mp3.samples / dec->channels : dec->u.mp3.samples;
        return 0;
    }

    dec->kind = DECODER_KIND_SILENCE;
    dec->sample_rate = 44100;
    dec->channels = 2;
    dec->total_frames = 0;
    return 0;
}

void decoder_close(audiobook_decoder *dec) {
    if (!dec) return;
    switch (dec->kind) {
    case DECODER_KIND_WAV:
        drwav_uninit(&dec->u.wav);
        break;
    case DECODER_KIND_FLAC:
        drflac_close(dec->u.flac);
        break;
    case DECODER_KIND_MP3:
        mp3dec_ex_close(&dec->u.mp3);
        break;
    default:
        break;
    }
    decoder_reset(dec);
}

int decoder_seek_ms(audiobook_decoder *dec, uint64_t position_ms) {
    if (!dec) return -1;
    uint64_t frame = ms_to_frames(position_ms, dec->sample_rate);
    switch (dec->kind) {
    case DECODER_KIND_WAV:
        return drwav_seek_to_pcm_frame(&dec->u.wav, frame) ? 0 : -1;
    case DECODER_KIND_FLAC:
        return drflac_seek_to_pcm_frame(dec->u.flac, frame) ? 0 : -1;
    case DECODER_KIND_MP3:
        return mp3dec_ex_seek(&dec->u.mp3, frame * dec->channels) == 0 ? 0 : -1;
    case DECODER_KIND_SILENCE:
    case DECODER_KIND_NONE:
        dec->current_frame = frame;
        return 0;
    }
    return -1;
}

size_t decoder_read_frames(audiobook_decoder *dec, int16_t *dst, size_t frames) {
    if (!dec || !dst || frames == 0) return 0;
    switch (dec->kind) {
    case DECODER_KIND_WAV: {
        drwav_uint64 got = drwav_read_pcm_frames_s16(&dec->u.wav, frames, dst);
        dec->current_frame += got;
        dec->eof = got == 0;
        return (size_t)got;
    }
    case DECODER_KIND_FLAC: {
        drwav_uint64 got = drflac_read_pcm_frames_s16(dec->u.flac, frames, dst);
        dec->current_frame += got;
        dec->eof = got == 0;
        return (size_t)got;
    }
    case DECODER_KIND_MP3: {
        size_t samples = mp3dec_ex_read(&dec->u.mp3, (mp3d_sample_t *)dst, frames * dec->channels);
        size_t got = dec->channels > 0 ? samples / dec->channels : samples;
        dec->current_frame += got;
        dec->eof = got == 0;
        return got;
    }
    case DECODER_KIND_SILENCE:
        memset(dst, 0, frames * dec->channels * sizeof(int16_t));
        dec->current_frame += frames;
        return frames;
    case DECODER_KIND_NONE:
        break;
    }
    return 0;
}

uint64_t decoder_duration_ms(const audiobook_decoder *dec) {
    if (!dec) return 0;
    return frames_to_ms(dec->total_frames, dec->sample_rate);
}

unsigned decoder_sample_rate(const audiobook_decoder *dec) {
    return dec ? dec->sample_rate : 0;
}

unsigned decoder_channels(const audiobook_decoder *dec) {
    return dec ? dec->channels : 0;
}

bool decoder_is_eof(const audiobook_decoder *dec) {
    return dec ? dec->eof : true;
}
