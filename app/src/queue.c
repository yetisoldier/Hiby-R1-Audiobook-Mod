#include "queue.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

void queue_init(playback_queue *q) {
    if (!q) return;
    memset(q, 0, sizeof(*q));
}

void queue_free(playback_queue *q) {
    if (!q) return;
    free(q->tracks);
    memset(q, 0, sizeof(*q));
}

int queue_set_tracks(playback_queue *q, int64_t book_id, const track_list *tracks) {
    if (!q || !tracks) return -1;
    queue_free(q);
    q->book_id = book_id;
    q->track_count = tracks->count;
    if (tracks->count == 0) return 0;
    q->tracks = ab_xcalloc(tracks->count, sizeof(*q->tracks));
    memcpy(q->tracks, tracks->items, tracks->count * sizeof(*q->tracks));
    q->current_index = 0;
    return 0;
}

const track_row *queue_current(const playback_queue *q) {
    if (!q || !q->tracks || q->current_index >= q->track_count) return NULL;
    return &q->tracks[q->current_index];
}

const track_row *queue_next(playback_queue *q) {
    if (!q || !q->tracks) return NULL;
    if (q->current_index + 1 < q->track_count) {
        q->current_index++;
        return &q->tracks[q->current_index];
    }
    if (q->repeat_book && q->track_count > 0) {
        q->current_index = 0;
        return &q->tracks[0];
    }
    return NULL;
}

const track_row *queue_prev(playback_queue *q) {
    if (!q || !q->tracks || q->current_index == 0) return NULL;
    q->current_index--;
    return &q->tracks[q->current_index];
}
