#ifndef R1_AB_PLAYER_H
#define R1_AB_PLAYER_H

#include "alsa.h"
#include "config.h"
#include "db.h"
#include "decoder.h"
#include "queue.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_state;

typedef struct player_snapshot {
    player_state state;
    int64_t book_id;
    int64_t track_id;
    int track_ordinal;
    uint64_t position_ms;
    uint64_t duration_ms;
    float speed;
    bool eof_reached;
} player_snapshot;

typedef struct audiobook_player {
    audiobook_config cfg;
    playback_queue queue;
    player_state state;
    player_state last_reported_state;
    int64_t book_id;
    int64_t track_id;
    int track_ordinal;
    bool book_loaded;
    bool want_playing;
    bool stop_thread;
    bool thread_started;
    bool track_changed;
    bool pending_seek;
    uint64_t pending_seek_ms;
    bool decoder_open;
    bool audio_open;
    uint64_t total_duration_ms;
    uint64_t current_track_position_ms;
    uint64_t started_at_ms;
    uint64_t paused_at_ms;
    uint64_t position_ms;
    uint64_t duration_ms;
    float speed;
    bool eof_reached;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    audiobook_decoder decoder;
    audiobook_alsa pcm;
} audiobook_player;

int player_init(audiobook_player *player, const audiobook_config *cfg);
void player_shutdown(audiobook_player *player);
int player_open_book(audiobook_player *player, const book_row *book, const track_list *tracks, const progress_row *resume);
int player_play(audiobook_player *player);
int player_pause(audiobook_player *player);
int player_stop(audiobook_player *player);
int player_seek_ms(audiobook_player *player, uint64_t position_ms);
int player_set_speed(audiobook_player *player, float speed);
int player_next_track(audiobook_player *player);
int player_previous_track(audiobook_player *player);
int player_poll(audiobook_player *player, player_snapshot *out);

#endif
