#ifndef R1_AB_IPC_H
#define R1_AB_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum audiobook_event_type {
    AB_EVT_BOOK_OPENED = 1,
    AB_EVT_PLAYBACK_STARTED,
    AB_EVT_POSITION_TICK,
    AB_EVT_PAUSED,
    AB_EVT_TRACK_CHANGED,
    AB_EVT_EOF_REACHED,
    AB_EVT_SEEK_REQUESTED,
    AB_EVT_SEEK_COMPLETE,
    AB_EVT_BOOK_COMPLETED,
    AB_EVT_APP_EXITING,
    AB_EVT_SCAN_REQUEST,
};

#define AB_EVT_RESUMED AB_EVT_PLAYBACK_STARTED

typedef struct audiobook_ipc_header {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t payload_len;
    uint64_t seq;
} audiobook_ipc_header;

typedef struct audiobook_ipc_frame {
    audiobook_ipc_header header;
    uint8_t payload[256];
} audiobook_ipc_frame;

typedef struct audiobook_event {
    uint16_t type;
    uint32_t book_id;
    char book_key[128];
    uint32_t track_id;
    uint32_t track_ordinal;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint32_t playback_speed_x100;
    uint32_t play_state;
    uint32_t completed;
    char reason[64];
} audiobook_event;

int ipc_client_connect(const char *socket_path);
int ipc_server_bind(const char *socket_path);
int ipc_server_accept(int listen_fd, int timeout_ms);
int ipc_send_frame(int fd, const audiobook_ipc_frame *frame);
int ipc_recv_frame(int fd, audiobook_ipc_frame *frame, int timeout_ms);
int ipc_send_event(int fd, uint16_t type, uint64_t seq, const audiobook_event *event);

#endif
