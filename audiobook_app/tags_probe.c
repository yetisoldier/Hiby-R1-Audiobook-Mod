#include <stdio.h>
#include "tags.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <audio-file>\n", argv[0]);
        return 2;
    }

    audio_tags_t tags;
    if (audio_read_tags(argv[1], &tags) < 0) {
        fprintf(stderr, "could not read tags: %s\n", argv[1]);
        return 1;
    }

    printf("title=%s\n", tags.title);
    printf("artist=%s\n", tags.artist);
    printf("album=%s\n", tags.album);
    printf("description=%s\n", tags.description);
    printf("duration_ms=%lld\n", (long long)tags.duration_ms);
    return 0;
}
