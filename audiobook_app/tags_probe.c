#include <stdio.h>
#include "tags.h"

static int print_chapter(int ordinal, const char *title,
                         int64_t start_ms, int64_t end_ms, void *ctx) {
    (void)ctx;
    printf("chapter=%d|%lld|%lld|%s\n", ordinal, (long long)start_ms,
           (long long)end_ms, title ? title : "");
    return 0;
}

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
    printf("chapter_count=%d\n",
           audio_read_chapters(argv[1], print_chapter, NULL));
    return 0;
}
