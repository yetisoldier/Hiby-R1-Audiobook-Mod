#include "ipc.h"
#include "common.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum { AB_IPC_MAGIC = 0x50494241u };

int ipc_client_connect(const char *socket_path) {
    if (!socket_path || !socket_path[0]) return -1;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int ipc_server_bind(const char *socket_path) {
    if (!socket_path || !socket_path[0]) return -1;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    unlink(socket_path);
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 8) != 0) {
        close(fd);
        unlink(socket_path);
        return -1;
    }
    chmod(socket_path, 0660);
    return fd;
}

int ipc_server_accept(int listen_fd, int timeout_ms) {
    if (listen_fd < 0) return -1;
    struct pollfd pfd = {.fd = listen_fd, .events = POLLIN};
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return -1;
    return accept(listen_fd, NULL, NULL);
}

int ipc_send_frame(int fd, const audiobook_ipc_frame *frame) {
    if (fd < 0 || !frame) return -1;
    size_t total = sizeof(frame->header) + frame->header.payload_len;
    if (total > sizeof(*frame)) return -1;
    return send(fd, frame, total, 0) >= 0 ? 0 : -1;
}

int ipc_recv_frame(int fd, audiobook_ipc_frame *frame, int timeout_ms) {
    if (fd < 0 || !frame) return -1;
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return -1;
    ssize_t n = recv(fd, frame, sizeof(*frame), 0);
    return n >= (ssize_t)sizeof(frame->header) ? 0 : -1;
}

int ipc_send_event(int fd, uint16_t type, uint64_t seq, const audiobook_event *event) {
    audiobook_ipc_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.header.magic = AB_IPC_MAGIC;
    frame.header.version = 1;
    frame.header.type = type;
    frame.header.payload_len = event ? (uint32_t)sizeof(*event) : 0u;
    frame.header.seq = seq;
    if (event) {
        audiobook_event tmp = *event;
        tmp.type = type;
        memcpy(frame.payload, &tmp, sizeof(tmp));
    }
    return ipc_send_frame(fd, &frame);
}
