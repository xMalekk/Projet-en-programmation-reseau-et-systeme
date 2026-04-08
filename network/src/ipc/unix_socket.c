// Backend IPC POSIX basé sur des sockets UNIX.
// Fournit ipc_connection_* pour relier Python et le daemon.

#include "medievail_net/ipc.h"
#include "medievail_net/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef IPC_DEFAULT_ENDPOINT
#define IPC_DEFAULT_ENDPOINT "/tmp/medievail_net.sock"
#endif

struct IPCConnection {
    int fd;
};

// remet message à zéro
static void zero_message(IPCMessage *message) {
    if (!message) return;
    message->type = IPC_MESSAGE_NONE;
    message->payload_size = 0;
    message->payload = NULL;
}

// passe le descripteur en non-bloquant
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

// ouvre un socket UNIX vers l'endpoint donné
IPCConnection *ipc_connection_create(const char *endpoint) {
    const char *path = (endpoint && endpoint[0]) ? endpoint : IPC_DEFAULT_ENDPOINT;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        log_error("[ipc] socket(AF_UNIX) echoue (%s)", strerror(errno));
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        log_error("[ipc] chemin trop long: %s", path);
        close(fd);
        return NULL;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("[ipc] connect(%s) échoue (%s)", path, strerror(errno));
        close(fd);
        return NULL;
    }

    if (set_nonblocking(fd) < 0) {
        log_warn("[ipc] impossible de passer en non-bloquant (%s)", strerror(errno));
    }

    IPCConnection *connection = calloc(1, sizeof(*connection));
    if (!connection) {
        close(fd);
        return NULL;
    }
    connection->fd = fd;
    log_info("[ipc] connecté au socket UNIX %s", path);
    return connection;
}

// ferme la connexion et libère la mémoire
void ipc_connection_destroy(IPCConnection *connection) {
    if (!connection) return;
    if (connection->fd >= 0) close(connection->fd);
    free(connection);
}

// lit exactement size octets
static int read_exact(int fd, void *buffer, size_t size) {
    unsigned char *ptr = buffer;
    size_t total = 0;
    while (total < size) {
        ssize_t rc = read(fd, ptr + total, size - total);
        if (rc == 0) return 0;
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
            return -1;
        }
        total += (size_t)rc;
    }
    return 1;
}

// attend un message (non bloquant avec timeout)
int ipc_connection_poll(IPCConnection *connection, IPCMessage *out_message, unsigned int timeout_ms) {
    if (!connection || connection->fd < 0 || !out_message) return -1;

    zero_message(out_message);

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(connection->fd, &rfds);

    int ready = select(connection->fd + 1, &rfds, NULL, NULL, timeout_ms ? &tv : NULL);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        log_error("[ipc] select échoue (%s)", strerror(errno));
        return -1;
    }
    if (ready == 0) return 0;

    uint32_t header[2];
    int rc = read_exact(connection->fd, header, sizeof(header));
    if (rc <= 0) {
        if (rc == 0) log_error("[ipc] connexion IPC fermée");
        return rc == 0 ? -1 : 0;
    }

    IPCMessageType type = (IPCMessageType)header[0];
    uint32_t payload_size = header[1];

    unsigned char *payload = NULL;
    if (payload_size > 0) {
        payload = malloc(payload_size);
        if (!payload) {
            log_error("[ipc] malloc payload (%u) échoue", payload_size);
            return -1;
        }
        rc = read_exact(connection->fd, payload, payload_size);
        if (rc <= 0) {
            free(payload);
            if (rc == 0) log_error("[ipc] connexion IPC fermée");
            return rc == 0 ? -1 : 0;
        }
    }

    out_message->type = type;
    out_message->payload_size = payload_size;
    out_message->payload = payload;
    return 1;
}

// envoie un message (en-tête + payload)
int ipc_connection_send(IPCConnection *connection, const IPCMessage *message) {
    if (!connection || connection->fd < 0 || !message) return -1;

    uint32_t header[2] = {
        (uint32_t)message->type,
        message->payload_size
    };

    struct iovec iov[2];
    iov[0].iov_base = header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = message->payload;
    iov[1].iov_len = message->payload ? message->payload_size : 0;

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = message->payload ? 2 : 1;

    ssize_t rc = sendmsg(connection->fd, &msg, 0);
    if (rc < 0) {
        log_error("[ipc] sendmsg échoue (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

// libère le payload d'un message
void ipc_message_free(IPCMessage *message) {
    if (!message) return;
    free(message->payload);
    zero_message(message);
}

// délai utilitaire pour calmer la boucle
void ipc_sleep(unsigned int milliseconds) {
    struct timespec req;
    req.tv_sec = milliseconds / 1000;
    req.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

