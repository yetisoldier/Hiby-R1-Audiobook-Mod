#include "player.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    PLAYER_PCM_FRAMES = 2048,
};

static uint64_t track_prefix_ms(const playback_queue *queue, size_t index) {
    uint64_t total = 0;
    if (!queue || !queue->tracks) return 0;
    if (index > queue->track_count) index = queue->track_count;
    for (size_t i = 0; i < index; i++) {
        total += queue->tracks[i].duration_ms > 0 ? (uint64_t)queue->tracks[i].duration_ms : 0u;
    }
    return total;
}

static uint64_t total_duration_ms(const playback_queue *queue) {
    return track_prefix_ms(queue, queue ? queue->track_count : 0);
}

static uint64_t current_track_ms_locked(const audiobook_player *player) {
    if (!player || player->decoder.sample_rate == 0) return 0;
    return (player->decoder.current_frame * 1000u) / player->decoder.sample_rate;
}

static void refresh_snapshot_locked(audiobook_player *player) {
    const track_row *track = queue_current(&player->queue);
    player->track_ordinal = track ? track->ordinal : 0;
    player->track_id = track ? track->track_id : 0;
    player->current_track_position_ms = current_track_ms_locked(player);
    player->position_ms = track_prefix_ms(&player->queue, player->queue.current_index) + player->current_track_position_ms;
    player->duration_ms = player->total_duration_ms;
}

static int open_transport_locked(audiobook_player *player) {
    const track_row *track = queue_current(&player->queue);
    if (!track) return -1;

    decoder_close(&player->decoder);
    alsa_close(&player->pcm);
    player->decoder_open = false;
    player->audio_open = false;

    if (decoder_open(&player->decoder, track->path) != 0) return -1;
    player->decoder_open = true;
    if (alsa_open(&player->pcm, player->cfg.pcm_device,
                  player->decoder.sample_rate ? player->decoder.sample_rate : 44100u,
                  2u, 8192u, 1024u) != 0) {
        decoder_close(&player->decoder);
        player->decoder_open = false;
        return -1;
    }
    player->audio_open = true;

    uint64_t seek_ms = player->pending_seek_ms;
    if (!player->pending_seek && player->position_ms > 0) {
        uint64_t base = track_prefix_ms(&player->queue, player->queue.current_index);
        seek_ms = player->position_ms > base ? player->position_ms - base : 0u;
    }
    if (seek_ms > 0 || player->pending_seek) {
        (void)decoder_seek_ms(&player->decoder, seek_ms);
    }
    player->pending_seek = false;
    refresh_snapshot_locked(player);
    return 0;
}

static int advance_track_locked(audiobook_player *player, int direction) {
    const track_row *track = direction > 0 ? queue_next(&player->queue) : queue_prev(&player->queue);
    if (!track) return -1;
    player->pending_seek = true;
    player->pending_seek_ms = 0;
    player->track_changed = true;
    refresh_snapshot_locked(player);
    return 0;
}

static int convert_samples_to_stereo(const int16_t *src, size_t sample_count, unsigned channels, int16_t *dst, size_t dst_frames_cap) {
    if (!src || !dst || channels == 0) return -1;
    if (channels == 1) {
        size_t frames = sample_count;
        if (frames > dst_frames_cap) frames = dst_frames_cap;
        for (size_t i = 0; i < frames; i++) {
            int16_t s = src[i];
            dst[i * 2u] = s;
            dst[i * 2u + 1u] = s;
        }
        return (int)(frames * 2u);
    }
    size_t frames = sample_count / channels;
    if (frames > dst_frames_cap) frames = dst_frames_cap;
    for (size_t i = 0; i < frames; i++) {
        dst[i * 2u] = src[i * channels];
        dst[i * 2u + 1u] = src[i * channels + 1u];
    }
    return (int)(frames * 2u);
}

static int write_stereo_block(audiobook_player *player, const int16_t *samples, size_t sample_count, unsigned channels) {
    int16_t stereo[PLAYER_PCM_FRAMES * 2u];
    int stereo_samples = convert_samples_to_stereo(samples, sample_count, channels, stereo, PLAYER_PCM_FRAMES);
    if (stereo_samples < 0) return -1;
    size_t stereo_frames = (size_t)stereo_samples / 2u;
    size_t offset_frames = 0;
    while (offset_frames < stereo_frames) {
        ssize_t wrote = alsa_write_frames(&player->pcm, stereo + offset_frames * 2u, stereo_frames - offset_frames);
        if (wrote < 0) {
            if (alsa_drop(&player->pcm) == 0 && alsa_prepare(&player->pcm) == 0) {
                continue;
            }
            return -1;
        }
        if (wrote == 0) continue;
        offset_frames += (size_t)wrote;
    }
    return 0;
}

static void *player_worker(void *arg) {
    audiobook_player *player = arg;
    int16_t input[PLAYER_PCM_FRAMES * 2u];

    pthread_mutex_lock(&player->lock);
    while (!player->stop_thread) {
        while (!player->stop_thread && (!player->book_loaded || (!player->want_playing && !player->pending_seek && !player->track_changed))) {
            pthread_cond_wait(&player->cond, &player->lock);
        }
        if (player->stop_thread) break;

        if (player->track_changed || !player->decoder_open) {
            player->track_changed = false;
            if (open_transport_locked(player) != 0) {
                player->state = PLAYER_STOPPED;
                player->want_playing = false;
                pthread_cond_wait(&player->cond, &player->lock);
                continue;
            }
        } else if (player->pending_seek) {
            uint64_t base = track_prefix_ms(&player->queue, player->queue.current_index);
            uint64_t local = player->pending_seek_ms;
            if (player->position_ms > base) {
                local = player->position_ms - base;
            }
            (void)decoder_seek_ms(&player->decoder, local);
            player->pending_seek = false;
            refresh_snapshot_locked(player);
        }

        if (!player->want_playing) {
            if (player->pcm.fd >= 0) {
                (void)alsa_pause(&player->pcm, true);
            }
            pthread_cond_wait(&player->cond, &player->lock);
            continue;
        }

        player->state = PLAYER_PLAYING;
        pthread_mutex_unlock(&player->lock);

        size_t frames = decoder_read_frames(&player->decoder, input, PLAYER_PCM_FRAMES);
        if (frames == 0) {
            pthread_mutex_lock(&player->lock);
            if (advance_track_locked(player, 1) != 0) {
                player->state = PLAYER_STOPPED;
                player->eof_reached = true;
                player->want_playing = false;
                refresh_snapshot_locked(player);
                pthread_mutex_unlock(&player->lock);
                continue;
            }
            pthread_mutex_unlock(&player->lock);
            continue;
        }

        pthread_mutex_lock(&player->lock);
        if (player->stop_thread) break;
        if (write_stereo_block(player, input, frames * (player->decoder.channels ? player->decoder.channels : 2u), player->decoder.channels) != 0) {
            player->state = PLAYER_STOPPED;
            player->want_playing = false;
            pthread_mutex_unlock(&player->lock);
            continue;
        }
        refresh_snapshot_locked(player);
    }
    pthread_mutex_unlock(&player->lock);
    return NULL;
}

int player_init(audiobook_player *player, const audiobook_config *cfg) {
    if (!player || !cfg) return -1;
    memset(player, 0, sizeof(*player));
    player->cfg = *cfg;
    player->state = PLAYER_STOPPED;
    player->speed = cfg->default_speed > 0.0f ? cfg->default_speed : 1.0f;
    queue_init(&player->queue);
    pthread_mutex_init(&player->lock, NULL);
    pthread_cond_init(&player->cond, NULL);
    player->pcm.fd = -1;
    if (pthread_create(&player->thread, NULL, player_worker, player) != 0) {
        pthread_cond_destroy(&player->cond);
        pthread_mutex_destroy(&player->lock);
        queue_free(&player->queue);
        return -1;
    }
    player->thread_started = true;
    return 0;
}

void player_shutdown(audiobook_player *player) {
    if (!player) return;
    pthread_mutex_lock(&player->lock);
    player->stop_thread = true;
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    if (player->thread_started) {
        pthread_join(player->thread, NULL);
    }
    pthread_mutex_lock(&player->lock);
    decoder_close(&player->decoder);
    alsa_close(&player->pcm);
    player->decoder_open = false;
    player->audio_open = false;
    pthread_mutex_unlock(&player->lock);
    pthread_cond_destroy(&player->cond);
    pthread_mutex_destroy(&player->lock);
    queue_free(&player->queue);
    memset(player, 0, sizeof(*player));
}

int player_open_book(audiobook_player *player, const book_row *book, const track_list *tracks, const progress_row *resume) {
    if (!player || !book || !tracks) return -1;
    pthread_mutex_lock(&player->lock);
    if (queue_set_tracks(&player->queue, book->book_id, tracks) != 0) {
        pthread_mutex_unlock(&player->lock);
        return -1;
    }
    if (resume && resume->track_ordinal > 0) {
        size_t idx = (size_t)(resume->track_ordinal - 1);
        if (idx < player->queue.track_count) {
            player->queue.current_index = idx;
        }
    }
    player->book_loaded = true;
    player->book_id = book->book_id;
    player->total_duration_ms = total_duration_ms(&player->queue);
    player->speed = resume && resume->playback_speed > 0.0f ? resume->playback_speed : player->cfg.default_speed;
    player->pending_seek = true;
    player->pending_seek_ms = resume && resume->position_ms > 0 ? (uint64_t)resume->position_ms : 0u;
    player->eof_reached = false;
    player->state = PLAYER_PAUSED;
    refresh_snapshot_locked(player);
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return 0;
}

int player_play(audiobook_player *player) {
    if (!player) return -1;
    pthread_mutex_lock(&player->lock);
    player->want_playing = true;
    player->state = PLAYER_PLAYING;
    player->eof_reached = false;
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return 0;
}

int player_pause(audiobook_player *player) {
    if (!player) return -1;
    pthread_mutex_lock(&player->lock);
    player->want_playing = false;
    player->state = PLAYER_PAUSED;
    refresh_snapshot_locked(player);
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return 0;
}

int player_stop(audiobook_player *player) {
    if (!player) return -1;
    pthread_mutex_lock(&player->lock);
    player->want_playing = false;
    player->state = PLAYER_STOPPED;
    player->position_ms = 0;
    player->current_track_position_ms = 0;
    player->eof_reached = false;
    player->pending_seek = true;
    player->pending_seek_ms = 0;
    player->track_changed = true;
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return 0;
}

int player_seek_ms(audiobook_player *player, uint64_t position_ms) {
    if (!player) return -1;
    pthread_mutex_lock(&player->lock);
    player->pending_seek = true;
    player->pending_seek_ms = position_ms;
    refresh_snapshot_locked(player);
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return 0;
}

int player_set_speed(audiobook_player *player, float speed) {
    if (!player || speed <= 0.25f || speed > 4.0f) return -1;
    pthread_mutex_lock(&player->lock);
    player->speed = speed;
    pthread_mutex_unlock(&player->lock);
    return 0;
}

int player_next_track(audiobook_player *player) {
    if (!player) return -1;
    pthread_mutex_lock(&player->lock);
    int rc = advance_track_locked(player, 1);
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return rc;
}

int player_previous_track(audiobook_player *player) {
    if (!player) return -1;
    pthread_mutex_lock(&player->lock);
    int rc = advance_track_locked(player, -1);
    pthread_cond_broadcast(&player->cond);
    pthread_mutex_unlock(&player->lock);
    return rc;
}

int player_poll(audiobook_player *player, player_snapshot *out) {
    if (!player || !out) return -1;
    pthread_mutex_lock(&player->lock);
    refresh_snapshot_locked(player);
    memset(out, 0, sizeof(*out));
    out->state = player->state;
    out->book_id = player->book_id;
    out->track_id = player->track_id;
    out->track_ordinal = player->track_ordinal;
    out->position_ms = player->position_ms;
    out->duration_ms = player->duration_ms;
    out->speed = player->speed;
    out->eof_reached = player->eof_reached;
    pthread_mutex_unlock(&player->lock);
    return 0;
}
