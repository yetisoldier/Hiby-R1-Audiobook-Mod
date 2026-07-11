#ifndef R1_AB_ALSA_H
#define R1_AB_ALSA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct audiobook_alsa {
    int fd;
    unsigned rate;
    unsigned channels;
    unsigned buffer_size;
    unsigned period_size;
    bool configured;
    char device[64];
} audiobook_alsa;

int alsa_open(audiobook_alsa *alsa, const char *device, unsigned rate, unsigned channels, unsigned buffer_size, unsigned period_size);
ssize_t alsa_write_frames(audiobook_alsa *alsa, const int16_t *frames, size_t frame_count);
int alsa_pause(audiobook_alsa *alsa, bool pause);
int alsa_prepare(audiobook_alsa *alsa);
int alsa_drop(audiobook_alsa *alsa);
void alsa_close(audiobook_alsa *alsa);

#endif
