// Backend IPC basé sur UDP local : même protocole que les échanges réseau.
// Permet à Python de dialoguer avec le démon via un simple socket UDP.

#include "medievail_net/ipc.h"
#include "medievail_net/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_IPC_PORT 21000
#define DEFAULT_IPC_HOST "127.0.0.1"
#define MAX_DATAGRAM_SIZE 65535

struct IPCConnection {
    int fd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    int remote_known;
};

// parse "host:port" en sockaddr_in
static int parse_endpoint(const char *endpoint, struct sockaddr_in *out_addr) {
    const char *host = DEFAULT_IPC_HOST;
    int port = DEFAULT_IPC_PORT;

    char hostbuf[256];
    if (endpoint && endpoint[0]) {
        const char *colon = strrchr(endpoint, ':');
        if (colon) {
            size_t len = (size_t)(colon - endpoint);
            if (len >= sizeof(hostbuf)) len = sizeof(hostbuf) - 1;
            memcpy(hostbuf, endpoint, len);
            hostbuf[len] = '\0';
            host = hostbuf;
            port = atoi(colon + 1);
        } else {
            port = atoi(endpoint);
        }
    }
    if (port <= 0 || port > 65535) {
        port = DEFAULT_IPC_PORT;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        log_error("[ipc-udp] hôte invalide %s", host);
        return -1;
    }
    *out_addr = addr;
    return 0;
}

// passe un socket en non-bloquant
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

// remote par défaut (utile avant réception)
static struct sockaddr_in make_default_remote(void) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, DEFAULT_IPC_HOST, &addr.sin_addr);
    addr.sin_port = htons(DEFAULT_IPC_PORT);
    return addr;
}

// ouvre/binde le socket UDP local
IPCConnection *ipc_connection_create(const char *endpoint) {
    struct sockaddr_in local;
    if (parse_endpoint(endpoint, &local) != 0) {
        log_error("[ipc-udp] endpoint invalide: %s", endpoint ? endpoint : "<null>");
        return NULL;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log_error("[ipc-udp] socket() échoue (%s)", strerror(errno));
        return NULL;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        log_error("[ipc-udp] bind() échoue (%s)", strerror(errno));
        close(fd);
        return NULL;
    }

    if (set_nonblocking(fd) < 0) {
        log_warn("[ipc-udp] impossible de passer en non-bloquant (%s)", strerror(errno));
    }

    IPCConnection *conn = calloc(1, sizeof(*conn));
    if (!conn) {
        close(fd);
        return NULL;
    }
    conn->fd = fd;
    conn->local_addr = local;
    conn->remote_addr = make_default_remote();
    conn->remote_known = 0;

    char hostbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, hostbuf, sizeof(hostbuf));
    log_info("[ipc-udp] écoute sur %s:%d", hostbuf, ntohs(local.sin_port));
    return conn;
}

// ferme la connexion UDP
void ipc_connection_destroy(IPCConnection *connection) {
    if (!connection) return;
    if (connection->fd >= 0) close(connection->fd);
    free(connection);
}

// lit un datagramme avec timeout et remplit IPCMessage
static int read_datagram(IPCConnection *connection, unsigned int timeout_ms, IPCMessage *out_message) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(connection->fd, &rfds);

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };

    int ready = select(connection->fd + 1, &rfds, NULL, NULL, timeout_ms ? &tv : NULL);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        log_error("[ipc-udp] select échoue (%s)", strerror(errno));
        return -1;
    }
    if (ready == 0) {
        return 0;
    }

    unsigned char buffer[MAX_DATAGRAM_SIZE];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    ssize_t received = recvfrom(connection->fd, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&src, &srclen);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        log_error("[ipc-udp] recvfrom échoue (%s)", strerror(errno));
        return -1;
    }
    if (received < (ssize_t)sizeof(uint32_t) * 2) {
        log_warn("[ipc-udp] datagramme trop petit (%zd)", received);
        return 0;
    }

    connection->remote_addr = src;
    connection->remote_known = 1;

    uint32_t raw_type;
    uint32_t raw_size;
    memcpy(&raw_type, buffer, sizeof(uint32_t));
    memcpy(&raw_size, buffer + sizeof(uint32_t), sizeof(uint32_t));
    IPCMessageType type = (IPCMessageType)ntohl(raw_type);
    uint32_t payload_size = ntohl(raw_size);

    if (payload_size > (uint32_t)(received - (ssize_t)sizeof(uint32_t) * 2)) {
        log_warn("[ipc-udp] datagramme tronqué (%u vs %zd)", payload_size, received);
        return 0;
    }

    out_message->type = type;
    out_message->payload_size = payload_size;
    if (payload_size > 0) {
        out_message->payload = malloc(payload_size);
        if (!out_message->payload) {
            log_error("[ipc-udp] allocation payload %u échoue", payload_size);
            return -1;
        }
        memcpy(out_message->payload, buffer + sizeof(uint32_t) * 2, payload_size);
    } else {
        out_message->payload = NULL;
    }
    return 1;
}

// poll/lecture non bloquante
int ipc_connection_poll(IPCConnection *connection, IPCMessage *out_message, unsigned int timeout_ms) {
    if (!connection || connection->fd < 0 || !out_message) return -1;

    out_message->type = IPC_MESSAGE_NONE;
    out_message->payload_size = 0;
    out_message->payload = NULL;

    return read_datagram(connection, timeout_ms, out_message);
}

// envoie un message au client Python enregistré
int ipc_connection_send(IPCConnection *connection, const IPCMessage *message) {
    if (!connection || connection->fd < 0 || !message) return -1;
    if (!connection->remote_known) {
        log_warn("[ipc-udp] aucun client Python enregistré, envoi abandonné");
        return 0;
    }

    uint32_t header[2] = {
        htonl((uint32_t)message->type),
        htonl(message->payload_size)
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
    msg.msg_name = (void *)&connection->remote_addr;
    msg.msg_namelen = sizeof(connection->remote_addr);

    ssize_t sent = sendmsg(connection->fd, &msg, 0);
    if (sent < 0) {
        log_error("[ipc-udp] sendmsg échoue (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

// libère la mémoire du payload
void ipc_message_free(IPCMessage *message) {
    if (!message) return;
    free(message->payload);
    message->payload = NULL;
    message->payload_size = 0;
    message->type = IPC_MESSAGE_NONE;
}

// petit helper de pause
void ipc_sleep(unsigned int milliseconds) {
    struct timeval tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    select(0, NULL, NULL, NULL, &tv);
}