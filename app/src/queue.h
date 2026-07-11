#ifndef R1_AB_QUEUE_H
#define R1_AB_QUEUE_H

#include "db.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct playback_queue {
    int64_t book_id;
    size_t track_count;
    size_t current_index;
    track_row *tracks;
    bool repeat_book;
    bool shuffle;
} playback_queue;

void queue_init(playback_queue *q);
void queue_free(playback_queue *q);
int queue_set_tracks(playback_queue *q, int64_t book_id, const track_list *tracks);
const track_row *queue_current(const playback_queue *q);
const track_row *queue_next(playback_queue *q);
const track_row *queue_prev(playback_queue *q);

#endif

