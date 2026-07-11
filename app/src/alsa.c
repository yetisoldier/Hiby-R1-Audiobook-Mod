#include "alsa.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <sound/asound.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static void mask_set(struct snd_mask *mask, unsigned bit) {
    if (!mask) return;
    memset(mask, 0, sizeof(*mask));
    if (bit < SNDRV_MASK_MAX) {
        mask->bits[bit / 32u] |= 1u << (bit % 32u);
    }
}

static void interval_exact(struct snd_interval *interval, unsigned value) {
    if (!interval) return;
    memset(interval, 0, sizeof(*interval));
    interval->min = value;
    interval->max = value;
    interval->integer = 1;
}

static int configure_pcm(audiobook_alsa *alsa) {
    struct snd_pcm_hw_params hw;
    memset(&hw, 0, sizeof(hw));
    hw.flags = SNDRV_PCM_HW_PARAMS_NORESAMPLE;
    hw.rmask = (1u << SNDRV_PCM_HW_PARAM_ACCESS) |
               (1u << SNDRV_PCM_HW_PARAM_FORMAT) |
               (1u << SNDRV_PCM_HW_PARAM_SUBFORMAT) |
               (1u << SNDRV_PCM_HW_PARAM_CHANNELS) |
               (1u << SNDRV_PCM_HW_PARAM_RATE) |
               (1u << SNDRV_PCM_HW_PARAM_PERIOD_SIZE) |
               (1u << SNDRV_PCM_HW_PARAM_BUFFER_SIZE) |
               (1u << SNDRV_PCM_HW_PARAM_PERIODS);
    mask_set(&hw.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK], SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    mask_set(&hw.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK], SNDRV_PCM_FORMAT_S16_LE);
    mask_set(&hw.masks[SNDRV_PCM_HW_PARAM_SUBFORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK], SNDRV_PCM_SUBFORMAT_STD);
    interval_exact(&hw.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL], alsa->channels);
    interval_exact(&hw.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL], alsa->rate);
    interval_exact(&hw.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL], alsa->period_size);
    interval_exact(&hw.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL], alsa->buffer_size);
    interval_exact(&hw.intervals[SNDRV_PCM_HW_PARAM_PERIODS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL], alsa->buffer_size / alsa->period_size);
    if (ioctl(alsa->fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) != 0) return -1;

    struct snd_pcm_sw_params sw;
    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sw.tstamp_type = SNDRV_PCM_TSTAMP_TYPE_MONOTONIC;
    sw.avail_min = alsa->period_size;
    sw.start_threshold = alsa->period_size;
    sw.stop_threshold = alsa->buffer_size;
    sw.silence_threshold = 0;
    sw.silence_size = 0;
    sw.boundary = alsa->buffer_size * 4u;
    if (ioctl(alsa->fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) != 0) return -1;
    if (ioctl(alsa->fd, SNDRV_PCM_IOCTL_PREPARE) != 0) return -1;
    return 0;
}

int alsa_open(audiobook_alsa *alsa, const char *device, unsigned rate, unsigned channels, unsigned buffer_size, unsigned period_size) {
    if (!alsa || !device || !device[0] || rate == 0 || channels == 0 || buffer_size == 0 || period_size == 0) return -1;
    memset(alsa, 0, sizeof(*alsa));
    ab_copy_str(alsa->device, sizeof(alsa->device), device);
    alsa->rate = rate;
    alsa->channels = channels;
    alsa->buffer_size = buffer_size;
    alsa->period_size = period_size;
    alsa->fd = open(device, O_WRONLY | O_CLOEXEC);
    if (alsa->fd < 0) return -1;
    if (configure_pcm(alsa) != 0) {
        close(alsa->fd);
        memset(alsa, 0, sizeof(*alsa));
        alsa->fd = -1;
        return -1;
    }
    alsa->configured = true;
    return 0;
}

ssize_t alsa_write_frames(audiobook_alsa *alsa, const int16_t *frames, size_t frame_count) {
    if (!alsa || alsa->fd < 0 || !frames || frame_count == 0) return -1;
    struct snd_xferi xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.buf = (void *)frames;
    xfer.frames = frame_count;
    if (ioctl(alsa->fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer) != 0) {
        if (errno == EPIPE || errno == ESTRPIPE) {
            ioctl(alsa->fd, SNDRV_PCM_IOCTL_PREPARE);
            return -1;
        }
        return -1;
    }
    return (ssize_t)xfer.result;
}

int alsa_pause(audiobook_alsa *alsa, bool pause) {
    if (!alsa || alsa->fd < 0) return -1;
    int value = pause ? 1 : 0;
    return ioctl(alsa->fd, SNDRV_PCM_IOCTL_PAUSE, &value) == 0 ? 0 : -1;
}

int alsa_prepare(audiobook_alsa *alsa) {
    if (!alsa || alsa->fd < 0) return -1;
    return ioctl(alsa->fd, SNDRV_PCM_IOCTL_PREPARE) == 0 ? 0 : -1;
}

int alsa_drop(audiobook_alsa *alsa) {
    if (!alsa || alsa->fd < 0) return -1;
    return ioctl(alsa->fd, SNDRV_PCM_IOCTL_DROP) == 0 ? 0 : -1;
}

void alsa_close(audiobook_alsa *alsa) {
    if (!alsa) return;
    if (alsa->fd >= 0) close(alsa->fd);
    memset(alsa, 0, sizeof(*alsa));
    alsa->fd = -1;
}
