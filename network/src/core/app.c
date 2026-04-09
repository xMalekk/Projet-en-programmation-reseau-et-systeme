#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "app.h"
#include "ipc.h"
#include "log.h"

#define DEFAULT_LISTEN_PORT 20000
#define MAX_DATAGRAM_SIZE 65535

// Format strict réseau
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t payload_size;
} NetHeader;


// Structure UdpRouter
typedef struct {
    int fd;
    struct sockaddr_in *peers;
    int peer_count;
} UdpRouter;


// Helpers généraux
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int create_udp_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        close(fd);
        return -1;
    }
    
    set_nonblocking(fd);
    return fd;
}

// Fonction utilitaire pour éviter les doublons
int same_peer(struct sockaddr_in *a, struct sockaddr_in *b) {
    return (a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port);
}

void udp_router_add_peer(UdpRouter *router, struct sockaddr_in *addr) {
    for (int i = 0; i < router->peer_count; i++) {
        if (same_peer(&router->peers[i], addr)) return;
    }

    router->peers = realloc(router->peers, sizeof(struct sockaddr_in) * (router->peer_count + 1));
    router->peers[router->peer_count] = *addr;
    router->peer_count++;
    
    log_info("[Router] Nouveau joueur enregistré : %s:%d", inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
}

// Transforme une chaîne "IP:PORT" en structure sockaddr_in
int parse_peer_spec(const char *spec, struct sockaddr_in *addr) {
    char buffer[256];
    strncpy(buffer, spec, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0'; // Sécurité

    char *colon = strchr(buffer, ':');
    if (!colon) return -1; 

    *colon = '\0'; // Coupe la chaîne en deux à l'endroit du ':'
    int port = atoi(colon + 1);

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    
    // Convertit l'IP texte en format binaire réseau
    if (inet_pton(AF_INET, buffer, &addr->sin_addr) <= 0) {
        return -1;
    }
    return 0;
}

void udp_router_init(UdpRouter *router, const NetAppConfig *config) {
    router->peer_count = 0;
    router->peers = NULL;
    uint16_t port = (config->listen_port > 0) ? config->listen_port : DEFAULT_LISTEN_PORT;
    router->fd = create_udp_socket(port);
    
    // Chargement de la liste initiale des pairs depuis la config
    if (config->peer_addresses != NULL && config->peer_count > 0) {
        for (size_t i = 0; i < config->peer_count; i++) {
            struct sockaddr_in addr;
            if (parse_peer_spec(config->peer_addresses[i], &addr) == 0) {
                // On réutilise udp_router_add_peer pour les enregistrer proprement
                udp_router_add_peer(router, &addr);
            } else {
                log_warning("[Router] Impossible de parser l'adresse du pair : %s", config->peer_addresses[i]);
            }
        }
    }
}

void udp_router_close(UdpRouter *router) {
    if (router->fd >= 0) close(router->fd);
    if (router->peers) free(router->peers);
    router->peer_count = 0;
}


// Transfert réseau <-> IPC
void relay_event_to_peers(UdpRouter *router, IPCMessage *msg) {
    size_t total_size = sizeof(NetHeader) + msg->payload_size;
    char *packet = malloc(total_size);
    if (!packet) return;

    // En-tête
    NetHeader hdr;
    hdr.type = htonl((uint32_t)msg->type);
    hdr.payload_size = htonl(msg->payload_size);
    memcpy(packet, &hdr, sizeof(NetHeader));
    
    // Payload
    if (msg->payload_size > 0 && msg->payload) {
        memcpy(packet + sizeof(NetHeader), msg->payload, msg->payload_size);
    }

    // Multicast Best-Effort (envoie à tout les pairs)
    for (int i = 0; i < router->peer_count; i++) {
        sendto(router->fd, packet, total_size, 0, (struct sockaddr *)&router->peers[i], sizeof(struct sockaddr_in));
    }
    
    free(packet);
}

void pump_network_incoming(UdpRouter *router, IPCConnection *ipc_conn) {
    char buffer[MAX_DATAGRAM_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    // Boucle pour vider le buffer réseau (non-bloquant)
    while (1) {
        ssize_t received = recvfrom(router->fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, &addr_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
            continue;
        }

        if (received >= (ssize_t)sizeof(NetHeader)) {
            // Enregistre l'adresse comme nouveau peer
            udp_router_add_peer(router, &sender_addr);

            // Vérifie la taille et extrait l'en-tête
            NetHeader *hdr = (NetHeader *)buffer;
            uint32_t type = ntohl(hdr->type);
            uint32_t p_size = ntohl(hdr->payload_size);

            // Construit le message et envoie vers Python via IPC
            if (received >= (ssize_t)(sizeof(NetHeader) + p_size)) {
                IPCMessage out_msg;
                out_msg.type = (IPCMessageType)type;
                out_msg.payload_size = p_size;
                out_msg.payload = (p_size > 0) ? (unsigned char *)(buffer + sizeof(NetHeader)) : NULL;

                ipc_connection_send(ipc_conn, &out_msg);
            }
        }
    }
}

// Affichage de la configuration 
void log_config(const NetAppConfig *config) {
    log_info("Configuration du Daemon");
    log_info("ID Joueur    : %s", config->player_id ? config->player_id : "Inconnu");
    log_info("Endpoint IPC : %s", config->ipc_endpoint);
    log_info("Port réseau  : %d", config->listen_port > 0 ? config->listen_port : DEFAULT_LISTEN_PORT);
    log_info("Pairs init.  : %zu", config->peer_count);
}


// Fonction principale 
int net_app_run(const NetAppConfig *config) {
    if (!config) {
        log_error("Configuration invalide.");
        return EXIT_FAILURE;
    }

    log_config(config);

    // Création de l'IPC 
    IPCConnection *ipc_conn = ipc_connection_create(config->ipc_endpoint);
    if (!ipc_conn) {
        log_error("Impossible de créer l'IPC.");
        return EXIT_FAILURE;
    }

    // Initialisation du routeur (lien distant avec le réseau)
    UdpRouter router;
    udp_router_init(&router, config);
    if (router.fd < 0) {
        log_error("Impossible d'initialiser le routeur UDP.");
        ipc_connection_destroy(ipc_conn);
        return EXIT_FAILURE;
    }

    int running = 1;
    log_info("[Daemon] En cours d'exécution...");

    while (running) {
        // Traitement du réseau entrant vers l'IPC
        pump_network_incoming(&router, ipc_conn);

        // Traitement de l'IPC entrant vers le réseau (Timeout 50ms)
        IPCMessage py_msg;
        if (ipc_connection_poll(ipc_conn, &py_msg, 50) > 0) {
            
            if (py_msg.type == IPC_MESSAGE_SHUTDOWN) {
                // On prévient avant de quitter
                relay_event_to_peers(&router, &py_msg);
                running = 0;
            } 
            else if (py_msg.type == IPC_MESSAGE_EVENT || py_msg.type == IPC_MESSAGE_CONTROL) {
                // Relai de l'événement à tous les joueurs
                relay_event_to_peers(&router, &py_msg);
            } 
            else {
                log_warning("Type de message IPC inconnu: %d", py_msg.type);
            }

            ipc_message_free(&py_msg);
        }
    }

    // Nettoyage complet
    udp_router_close(&router);
    ipc_connection_destroy(ipc_conn);
    log_info("[Daemon] Arrêt propre du programme.");
    
    return EXIT_SUCCESS;
}