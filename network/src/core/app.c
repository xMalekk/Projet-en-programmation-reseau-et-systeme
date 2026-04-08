// Gabarit pour l’implémentation de la boucle principale du démon.
// C’est ici que net_app_run sera codé plus tard.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "medievail_net/ipc.h"
#include "medievail_net/log.h"
// #include "medievail_net/app.h"
// #include "medievail_net/ipc.h"
// #include "medievail_net/log.h"

#define PORT 8080 // port réseau pour tout les participants (temporaire pour les tests)

// set_nonblocking si elle n'est pas un des headers
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

// initialisation du socket réseau UDP pour écouter les autres participants
int init_network_server(int port) {
    int server_fd;
    struct sockaddr_in address;

    // CRÉATION DU SOCKET
    // AF_INET = IPv4, SOCK_DGRAM = UDP 
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("[-] Échec de la création du socket UDP.");
        exit(EXIT_FAILURE);
    }
    printf("[+] Socket UDP créé avec succès.\n");

    // permet de réutiliser le port immédiatement après avoir fermé le programme
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(server_fd); // pour select()

    // CONFIGURATION DE L'ADRESSE
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // écoute sur toutes les adresses IP de la machine
    address.sin_port = htons(PORT);       // htons convertion binaire

    // BIND au port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[-] Échec du bind.");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("[+] Bind réussi sur le port %d. En écoute de datagrammes UDP...\n", PORT);

    return server_fd; // retourne le descripteur de fichier pour l'utiliser plus tard
}

// boucle principale 

int net_app_run() {
    log_info("[Démon] Démarrage (Mode UDP)...");

    // initialisation réseau (partie locale)
    int network_fd = init_network_server(PORT);
    if (network_fd < 0) {
        return EXIT_FAILURE;
    }
    
    // en UDP, pas de socket dédié pour le client (remote_peer_fd n'existe plus)
    // sauvegarde l'adresse de la dernière personne qui nous a parlé
    struct sockaddr_in remote_peer_addr;
    int has_remote_peer = 0; // Flag pour savoir si on connait un adversaire

    // initialisation IPC (partie locale)
    // /!\ le jeu Python doit d'abord créer ce fichier socket et écouter avant que ce démon C ne s'y connecte
    // pour l'instant, on suppose que le path par défaut est bon.
    IPCConnection *ipc_conn = ipc_connection_create(NULL);
    if (!ipc_conn) {
        log_error("[Démon] Impossible de se connecter à l'IPC local.");
        close(network_fd);
        return EXIT_FAILURE;
    }

    int is_running = 1;

    // BOUCLE PRINCIPALE
    while (is_running) {
        fd_set readfds;
        FD_ZERO(&readfds);

        int max_fd = 0;

        // ajoute le socket IPC aux descripteurs à surveiller
        if (ipc_conn->fd >= 0) {
            FD_SET(ipc_conn->fd, &readfds);
            if (ipc_conn->fd > max_fd) max_fd = ipc_conn->fd;
        }

        // ajoute le socket réseau unique UDP
        FD_SET(network_fd, &readfds);
        if (network_fd > max_fd) max_fd = network_fd;

        // (Timeout optionnel)
        struct timeval tv = {1, 0};

        // select() met le processus en pause jusqu'à ce qu'il y ait de l'activité sur au moins un des fds surveillés
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) {
            log_error("[Démon] Erreur select: %s", strerror(errno));
            break;
        }

        if (activity == 0) {
            // aucune activité/on recommence la boucle
            continue; 
        }

        // GESTION DES ÉVÉNEMENTS

        // un message UDP arrive du réseau (ce qui fait aussi office d'identification du joueur distant)
        if (FD_ISSET(network_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            char buffer[1024]; // On prévoit un buffer pour lire le datagramme

            // recvfrom extrait le message et l'adresse source (client_addr)
            ssize_t bytes_received = recvfrom(network_fd, buffer, sizeof(buffer), 0, 
                                              (struct sockaddr *)&client_addr, &client_len);
            
            if (bytes_received > 0) {
                // si on reçoit un paquet, on met à jour notre contact distant si c'est quelqu'un de nouveau
                if (!has_remote_peer || remote_peer_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr || remote_peer_addr.sin_port != client_addr.sin_port) {
                    remote_peer_addr = client_addr;
                    has_remote_peer = 1;
                    log_info("[Network] Nouveau contact distant enregistré ! IP: %s, Port: %d", 
                             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                }

                log_info("[Network] Reçu %zd octets depuis le réseau UDP. A router vers IPC...", bytes_received);
                // TODO: Traiter le message réseau dans "buffer" (protocole) et l'envoyer au Python via: ipc_connection_send(ipc_conn, &msg);

            } else if (bytes_received < 0) {
                log_error("[Network] Erreur recvfrom: %s", strerror(errno));
            }
        }

        // le jeu Python local nous envoie un message via IPC
        if (ipc_conn->fd >= 0 && FD_ISSET(ipc_conn->fd, &readfds)) {
            IPCMessage msg;
            // On a mis 0 en timeout car select a déjà dit qu'il y a des données prêtes
            int rc = ipc_connection_poll(ipc_conn, &msg, 0); 
            
            if (rc < 0) {
                log_error("[IPC] Python local déconnecté.");
                break; // Ou gérer la reconnexion
            } else if (rc > 0) {
                log_info("[IPC] Reçu message (Type: %d, Taille: %u) de Python. A envoyer sur le réseau...", msg.type, msg.payload_size);
                
                // Si on a l'adresse d'un adversaire, on lui envoie les données
                if (has_remote_peer) {
                    // TODO: Traduire `msg` selon ton protocole réseau et l'envoyer 
                    // via UDP avec sendto(network_fd, buffer_reseau, taille, 0, (struct sockaddr *)&remote_peer_addr, sizeof(remote_peer_addr)).
                } else {
                    log_warn("[Network] Impossible d'envoyer, aucun pair distant connu pour le moment.");
                }
                
                ipc_message_free(&msg); // Nettoyage de la mémoire allouée par ipc_connection_poll
            }
        }
    }

    // NETTOYAGE
    log_info("[Démon] Arrêt propre du démon...");
    close(network_fd);
    ipc_connection_destroy(ipc_conn);

    return EXIT_SUCCESS;
}
