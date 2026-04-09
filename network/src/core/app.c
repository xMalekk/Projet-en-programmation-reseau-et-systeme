// medievail_net daemon: routeur UDP entre Python local et pairs distants

#include "medievail_net/app.h"
#include "medievail_net/ipc.h"
#include "medievail_net/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_LISTEN_PORT 20000
#define MAX_DATAGRAM_SIZE 65535

typedef struct UdpRouter {
    int fd;
    struct sockaddr_in *peers;
    size_t peer_count;
    size_t peer_capacity;
} UdpRouter;

// helpers 

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static int create_udp_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log_error("[udp] socket() echoue (%s)", strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_warn("[udp] setsockopt echec (%s)", strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("[udp] bind(%d) echoue (%s)", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) < 0) {
        log_warn("[udp] impossible de passer en non-bloquant (%s)", strerror(errno));
    }

    log_info("[udp] ecoute sur 0.0.0.0:%d", port);
    return fd;
}

static int parse_peer_spec(const char *spec, struct sockaddr_in *out_addr) {
    if (!spec || !spec[0]) return -1;
    const char *colon = strrchr(spec, ':');
    if (!colon || colon == spec) return -1;

    char host[256];
    size_t host_len = (size_t)(colon - spec);
    if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
    memcpy(host, spec, host_len);
    host[host_len] = '\0';

    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) return -1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        return -1;
    }

    struct sockaddr_in addr = *(struct sockaddr_in *)res->ai_addr;
    addr.sin_port = htons((uint16_t)port);
    freeaddrinfo(res);

    *out_addr = addr;
    return 0;
}

static void udp_router_close(UdpRouter *router) {
    if (!router) return;
    if (router->fd >= 0) {
        close(router->fd);
        router->fd = -1;
    }
    free(router->peers);
    router->peers = NULL;
    router->peer_count = 0;
    router->peer_capacity = 0;
}

static bool same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_family == b->sin_family &&
           a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

static void udp_router_add_peer(UdpRouter *router, const struct sockaddr_in *addr) {
    if (!router || router->fd < 0 || !addr) return;
    for (size_t i = 0; i < router->peer_count; ++i) {
        if (same_peer(&router->peers[i], addr)) {
            return;
        }
    }

    if (router->peer_count == router->peer_capacity) {
        size_t new_cap = router->peer_capacity == 0 ? 4 : router->peer_capacity * 2;
        struct sockaddr_in *new_peers = realloc(router->peers, new_cap * sizeof(*new_peers));
        if (!new_peers) {
            log_warn("[udp] impossible d'ajouter un peer (memoire)" );
            return;
        }
        router->peers = new_peers;
        router->peer_capacity = new_cap;
    }
    router->peers[router->peer_count++] = *addr;

    char ipbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ipbuf, sizeof(ipbuf));
    log_info("[udp] peer enregistré : %s:%d", ipbuf, ntohs(addr->sin_port));
}

static int udp_router_init(UdpRouter *router, const NetAppConfig *config) {
    if (!router) return -1;
    router->fd = -1;
    router->peers = NULL;
    router->peer_count = 0;
    router->peer_capacity = 0;

    int port = (config && config->listen_port > 0) ? config->listen_port : DEFAULT_LISTEN_PORT;
    router->fd = create_udp_socket(port);
    if (router->fd < 0) {
        return -1;
    }

    if (config && config->peer_addresses && config->peer_count > 0) {
        for (size_t i = 0; i < config->peer_count; ++i) {
            struct sockaddr_in addr;
            if (parse_peer_spec(config->peer_addresses[i], &addr) == 0) {
                udp_router_add_peer(router, &addr);
            } else {
                log_warn("[udp] peer '%s' invalide", config->peer_addresses[i]);
            }
        }
    }
    return 0;
}

// transfert

static void relay_event_to_peers(const IPCMessage *message, const UdpRouter *router) {
    if (!router || router->fd < 0 || router->peer_count == 0 || !message) {
        return;
    }
    if (message->payload_size + sizeof(uint32_t) * 2 > MAX_DATAGRAM_SIZE) {
        log_warn("[udp] evenement trop volumineux (%u)", message->payload_size);
        return;
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

    for (size_t i = 0; i < router->peer_count; ++i) {
        msg.msg_name = (void *)&router->peers[i];
        msg.msg_namelen = sizeof(struct sockaddr_in);
        if (sendmsg(router->fd, &msg, 0) < 0) {
            log_error("[udp] sendmsg peer %zu echoue (%s)", i, strerror(errno));
        }
    }
}

static void pump_network_incoming(UdpRouter *router, IPCConnection *ipc) {
    if (!router || router->fd < 0 || !ipc) return;

    unsigned char buffer[MAX_DATAGRAM_SIZE];
    for (;;) {
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        ssize_t received = recvfrom(router->fd, buffer, sizeof(buffer), MSG_DONTWAIT,
                                    (struct sockaddr *)&src, &srclen);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            log_error("[udp] recvfrom echoue (%s)", strerror(errno));
            break;
        }
        if (received < (ssize_t)sizeof(uint32_t) * 2) {
            log_warn("[udp] datagramme trop court (%zd)", received);
            continue;
        }

        udp_router_add_peer(router, &src);

        uint32_t raw_type, raw_size;
        memcpy(&raw_type, buffer, sizeof(uint32_t));
        memcpy(&raw_size, buffer + sizeof(uint32_t), sizeof(uint32_t));
        IPCMessageType type = (IPCMessageType)ntohl(raw_type);
        uint32_t payload_size = ntohl(raw_size);
        if (payload_size > (uint32_t)(received - (ssize_t)sizeof(uint32_t) * 2)) {
            log_warn("[udp] datagramme tronqué (%u/%zd)", payload_size, received);
            continue;
        }

        IPCMessage message;
        message.type = type;
        message.payload_size = payload_size;
        message.payload = NULL;
        if (payload_size > 0) {
            message.payload = malloc(payload_size);
            if (!message.payload) {
                log_error("[udp] allocation %u echoue", payload_size);
                continue;
            }
            memcpy(message.payload, buffer + sizeof(uint32_t) * 2, payload_size);
        }

        if (ipc_connection_send(ipc, &message) < 0) {
            log_error("[udp->ipc] impossible d'envoyer un message à Python");
        }
        free(message.payload);
    }
}

static void log_config(const NetAppConfig *config) {
    log_info("[config] player      : %s", config->player_id ? config->player_id : "<inconnu>");
    log_info("[config] IPC endpoint: %s", config->ipc_endpoint ? config->ipc_endpoint : "<defaut>");
    log_info("[config] listen port : %d", config->listen_port > 0 ? config->listen_port : DEFAULT_LISTEN_PORT);
    log_info("[config] peers       : %zu", config->peer_count);
}

// entrée principale

int net_app_run(const NetAppConfig *config) {
    if (!config) {
        log_error("net_app_run appelé sans configuration");
        return -1;
    }

    log_info("medievail_net (UDP) ");
    log_config(config);

    IPCConnection *ipc = ipc_connection_create(config->ipc_endpoint);
    if (!ipc) {
        log_error("[ipc] impossible d'établir la connexion locale");
        return -2;
    }

    UdpRouter router;
    if (udp_router_init(&router, config) != 0) {
        log_error("[udp] impossible d'initialiser le socket réseau");
        ipc_connection_destroy(ipc);
        return -3;
    }

    int should_run = 1;
    while (should_run) {
        pump_network_incoming(&router, ipc);

        IPCMessage message;
        int rc = ipc_connection_poll(ipc, &message, 50);
        if (rc < 0) {
            log_error("[ipc] erreur de lecture -> arrêt");
            break;
        }
        if (rc == 0) {
            continue;
        }

        switch (message.type) {
            case IPC_MESSAGE_EVENT:
            case IPC_MESSAGE_CONTROL:
                relay_event_to_peers(&message, &router);
                break;
            case IPC_MESSAGE_SHUTDOWN:
                relay_event_to_peers(&message, &router);
                log_info("[ipc] shutdown demandé par Python");
                should_run = 0;
                break;
            default:
                log_warn("[ipc] type de message inconnu (%d)", message.type);
                break;
        }
        ipc_message_free(&message);
    }

    udp_router_close(&router);
    ipc_connection_destroy(ipc);
    log_info("medievail_net arrêté ");
    return 0;
}
