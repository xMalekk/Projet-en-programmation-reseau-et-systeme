#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

#include "medievail_net/ipc.h"

#define NET_PORT 8080

//redéclaration de la structure ici pour avoir le droit de lire fd
struct IPCConnection {
    int fd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    int remote_known;
};

volatile int is_running = 1;

void handle_sigint(int sig) {
    printf("\n[Daemon] Signal d'arrêt reçu.\n");
    is_running = 0;
}

// Format strict et minimaliste envoyé sur le réseau Internet/LAN (8 octets + données)
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t payload_size;
} NetHeader;

// initialisation du socket réseau public
int init_network_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("[-] Erreur socket réseau");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // passage en non-bloquant
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[-] Erreur bind réseau");
        exit(EXIT_FAILURE);
    }

    return fd;
}

// fonction principale (boucle)
int net_app_run(void) {
    signal(SIGINT, handle_sigint);
    printf("[Daemon] Démarrage. En attente sur le port réseau %d...\n", NET_PORT);

    // initialisation du réseau externe (vers l'autre PC)
    int network_fd = init_network_socket(NET_PORT);
    struct sockaddr_in remote_peer_addr;
    int has_remote_peer = 0;

    // initialisation du réseau interne 
    IPCConnection *ipc_conn = ipc_connection_create("127.0.0.1:21000");
    if (!ipc_conn) {
        fprintf(stderr, "[-] Impossible de créer l'IPC.\n");
        close(network_fd);
        return EXIT_FAILURE;
    }

    char buffer[65535]; // buffer réseau

    while (is_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        
        int max_fd = network_fd;
        FD_SET(network_fd, &readfds);

        if (ipc_conn->fd >= 0) {
            FD_SET(ipc_conn->fd, &readfds);
            if (ipc_conn->fd > max_fd) {
                max_fd = ipc_conn->fd;
            }
        }

        struct timeval tv = {1, 0};
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) break;
        if (activity == 0) continue;
     
        // FLUX 1 : Réseau (distant) ---> IPC (local)
   
        if (FD_ISSET(network_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            ssize_t received = recvfrom(network_fd, buffer, sizeof(buffer), 0, 
                                        (struct sockaddr *)&client_addr, &client_len);
            
            if (received >= (ssize_t)sizeof(NetHeader)) {
                // sauvegarde de l'adresse de l'adversaire s'il nous parle
                if (!has_remote_peer) {
                    remote_peer_addr = client_addr;
                    has_remote_peer = 1;
                    printf("[Réseau] Connecté à l'adversaire : %s:%d\n", 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                }

                // lecture de l'en-tête (type et taille)
                NetHeader *hdr = (NetHeader *)buffer;
                uint32_t type = ntohl(hdr->type);
                uint32_t p_size = ntohl(hdr->payload_size);

                // vérification de sécurité
                if (received >= (ssize_t)(sizeof(NetHeader) + p_size)) {
                    IPCMessage out_msg;
                    out_msg.type = (IPCMessageType)type;
                    out_msg.payload_size = p_size;
                    out_msg.payload = (p_size > 0) ? (unsigned char *)(buffer + sizeof(NetHeader)) : NULL;

                    // envoi vers le code de ton camarade
                    ipc_connection_send(ipc_conn, &out_msg);
                    printf("[Routage] PC distant -> Python (Type: %d, Taille: %u)\n", type, p_size);
                }
            }
        }

        // FLUX 2 : IPC (local) ---> Réseau (distant)

        if (ipc_conn->fd >= 0 && FD_ISSET(ipc_conn->fd, &readfds)) {
            IPCMessage msg;
            
            // eécupère l'information depuis ton Python
            if (ipc_connection_poll(ipc_conn, &msg, 0) > 0) {
                
                if (msg.type == IPC_MESSAGE_SHUTDOWN) {
                    is_running = 0;
                    ipc_message_free(&msg);
                    break;
                }

                if (has_remote_peer) {
                    size_t total_size = sizeof(NetHeader) + msg.payload_size;
                    char *net_packet = malloc(total_size);
                    
                    if (net_packet) {
                        // préparation de l'en-tête pour le réseau
                        NetHeader hdr;
                        hdr.type = htonl((uint32_t)msg.type);
                        hdr.payload_size = htonl(msg.payload_size);
                        
                        memcpy(net_packet, &hdr, sizeof(NetHeader));
                        
                        // ajout des données Python
                        if (msg.payload_size > 0 && msg.payload) {
                            memcpy(net_packet + sizeof(NetHeader), msg.payload, msg.payload_size);
                        }

                        // envoi sur Internet / LAN
                        sendto(network_fd, net_packet, total_size, 0, 
                               (struct sockaddr *)&remote_peer_addr, sizeof(remote_peer_addr));
                        
                        printf("[Routage] Python -> PC distant (Type: %d, Taille: %u)\n", msg.type, msg.payload_size);
                        free(net_packet);
                    }
                } else {
                    printf("[Alerte] Python veut envoyer, mais aucun adversaire n'est encore connecté.\n");
                }
                
                ipc_message_free(&msg);
        }
    }

    close(network_fd);
    ipc_connection_destroy(ipc_conn);
    printf("[Daemon] Arrêt propre.\n");
    
    return EXIT_SUCCESS;
}