#ifndef R1_AB_RESUME_H
#define R1_AB_RESUME_H

#include "db.h"
#include "ipc.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct resume_state {
    book_row book;
    progress_row progress;
    bool bound;
    bool started;
    bool completed;
    uint64_t started_at_ms;
    uint64_t last_save_ms;
} resume_state;

void resume_init(resume_state *r);
int resume_bind_book(resume_state *r, const book_row *book, const progress_row *progress);
uint32_t resume_smart_rewind_ms(uint64_t paused_seconds, bool rebooted, uint32_t saved_position_ms);
int resume_on_event(resume_state *r, const audiobook_event *ev, progress_row *out);
int resume_write_record_atomic(const char *dir, const progress_row *progress, const book_row *book);
int resume_read_record(const char *path, progress_row *progress);

#endif

