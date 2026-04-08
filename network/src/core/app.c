// daemon de routage UDP (version 1)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define NET_PORT 8080       // port pour communiquer avec les autres joueurs
#define IPC_PORT 9090       // port local pour recevoir les messages de ton Python

// non blocking
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

// initialise un socket UDP sur un port donné
int init_udp_socket(int port, const char* ip_address) {
    int fd;
    struct sockaddr_in address;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("Échec de la création du socket UDP.");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(fd);

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    // Si ip_address est "0.0.0.0", on écoute tout 
    // Si c'est "127.0.0.1", on écoute seulement la machine locale (IPC).
    address.sin_addr.s_addr = inet_addr(ip_address); 
    if (address.sin_addr.s_addr == INADDR_NONE) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Échec du bind");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    return fd;
}

// boucle principale
int net_app_run() {
    printf("[Daemon] Démarrage du routeur ...\n");

    // socket réseau (écoute Internet/LAN sur 8080)
    int network_fd = init_udp_socket(NET_PORT, "0.0.0.0");
    struct sockaddr_in remote_peer_addr;
    int has_remote_peer = 0;

    // socket IPC (écoute python local sur 9090)
    int ipc_fd = init_udp_socket(IPC_PORT, "127.0.0.1");
    struct sockaddr_in python_addr;
    int has_python_peer = 0;

    int is_running = 1;
    char buffer[4096]; // buffer partagé pour lire les paquets

    printf("[Daemon] Prêt. Réseau:%d | IPC:%d\n", NET_PORT, IPC_PORT);

    while (is_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = 0;

        FD_SET(network_fd, &readfds);
        if (network_fd > max_fd) max_fd = network_fd;

        FD_SET(ipc_fd, &readfds);
        if (ipc_fd > max_fd) max_fd = ipc_fd;

        struct timeval tv = {1, 0};
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) break;
        if (activity == 0) continue;

        // FLUX 1 : RÉSEAU -> IPC (Adversaire vers Python local)

        if (FD_ISSET(network_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            ssize_t bytes_recvd = recvfrom(network_fd, buffer, sizeof(buffer), 0, 
                                           (struct sockaddr *)&client_addr, &client_len);
            
            if (bytes_recvd > 0) {
                // sauvegarde de l'adresse de l'adversaire
                if (!has_remote_peer || remote_peer_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr) {
                    remote_peer_addr = client_addr;
                    has_remote_peer = 1;
                    printf("[Network] Nouvel adversaire détecté : %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                }

                // transfert vers le Python local 
                if (has_python_peer) {
                    sendto(ipc_fd, buffer, bytes_recvd, 0, (struct sockaddr *)&python_addr, sizeof(python_addr));
                }
            }
        }


        // FLUX 2 : IPC -> RÉSEAU (Python local vers Adversaire)
    
        if (FD_ISSET(ipc_fd, &readfds)) {
            struct sockaddr_in local_client_addr;
            socklen_t client_len = sizeof(local_client_addr);
            
            ssize_t bytes_recvd = recvfrom(ipc_fd, buffer, sizeof(buffer), 0, 
                                           (struct sockaddr *)&local_client_addr, &client_len);
            
            if (bytes_recvd > 0) {
                // sauvegarde de l'adresse du processus Python local
                if (!has_python_peer) {
                    python_addr = local_client_addr;
                    has_python_peer = 1;
                    printf("[IPC] Python local détecté sur le port %d\n", ntohs(local_client_addr.sin_port));
                }

                // transfert vers l'adversaire réseau (s'il est connu)
                if (has_remote_peer) {
                    sendto(network_fd, buffer, bytes_recvd, 0, (struct sockaddr *)&remote_peer_addr, sizeof(remote_peer_addr));
                }
            }
        }
    }

    close(network_fd);
    close(ipc_fd);
    return EXIT_SUCCESS;
}