// Gabarit pour l’implémentation de la boucle principale du démon.
// C’est ici que net_app_run sera codé plus tard.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>(§)
#include <fcntl.h>
// #include "medievail_net/app.h"
// #include "medievail_net/ipc.h"
// #include "medievail_net/log.h"

#define PORT 8080 // port réseau pour tout les participants (temporaire pour les tests)

// Initialisation du socket réseau TCP pour écouter les autres serveur
int init_network_server(int port) {
    int server_fd;
    struct sockaddr_in address;

    // CRÉATION DU SOCKET
    // AF_INET = IPv4, SOCK_STREAM = TCP 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("[-] Échec de la création du socket.");
        exit(EXIT_FAILURE);
    }
    printf("[+] Socket créé avec succès.\n");

    // Permet de réutiliser le port immédiatement après avoir fermé le programme
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
    printf("[+] Bind réussi sur le port %d.\n", PORT);

    // LISTEN 
    if (listen(server_fd, 3) < 0) { // // Le "3" est la taille de la file d'attente (nombre de connexions en attente autorisées)
        perror("[-] Échec de l'écoute.");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("[+] Le démon réseau écoute et attend des connexions...\n");

    return server_fd; // On retourne le descripteur de fichier pour l'utiliser plus tard
}

// Boucle principale 

int net_app_run() {
    log_info("[Démon] Démarrage...");

    // Initialisation Réseau (partie locale)
    int network_server_fd = init_network_server(PORT);
    if (network_server_fd < 0) {
        return EXIT_FAILURE;
    }
    int remote_peer_fd = -1; // Socket du joueur distant (quand il sera connecté)

    // Initialisation IPC (partie distante)
    // /!\ Le jeu Python doit d'abord créer ce fichier socket et écouter avant que ce démon C ne s'y connecte
    // Pour l'instant, on suppose que le path par défaut est bon.
    IPCConnection *ipc_conn = ipc_connection_create(NULL);
    if (!ipc_conn) {
        log_error("[Démon] Impossible de se connecter à l'IPC local.");
        close(network_server_fd);
        return EXIT_FAILURE;
    }

    int is_running = 1;

    // LA BOUCLE PRINCIPALE
    while (is_running) {
        fd_set readfds;
        FD_ZERO(&readfds);

        int max_fd = 0;

        // On ajoute le socket IPC aux descripteurs à surveiller
        if (ipc_conn->fd >= 0) {
            FD_SET(ipc_conn->fd, &readfds);
            if (ipc_conn->fd > max_fd) max_fd = ipc_conn->fd;
        }

        // On ajoute le socket serveur réseau (pour les nouvelles connexions)
        FD_SET(network_server_fd, &readfds);
        if (network_server_fd > max_fd) max_fd = network_server_fd;

        // On ajoute le joueur distant s'il est connecté
        if (remote_peer_fd >= 0) {
            FD_SET(remote_peer_fd, &readfds);
            if (remote_peer_fd > max_fd) max_fd = remote_peer_fd;
        }

        // (Timeout optionnel)
        struct timeval tv = {1, 0};

        // select() met le processus en pause jusqu'à ce qu'il y ait de l'activité sur au moins un des fds surveillés
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) {
            log_error("[Démon] Erreur select: %s", strerror(errno));
            break;
        }

        if (activity == 0) {
            // Aucune activité/on recommence la boucle
            continue; 
        }

        // GESTION DES ÉVÉNEMENTS

        // Un nouveau joueur essaie de se connecter sur le réseau
        if (FD_ISSET(network_server_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_socket = accept(network_server_fd, (struct sockaddr *)&client_addr, &client_len);
            
            if (new_socket >= 0) {
                log_info("[Network] Nouvelle connexion distante acceptée ! IP: %s", inet_ntoa(client_addr.sin_addr));
                set_nonblocking(new_socket);
                
                // P2P simple à 2 joueurs
                // Si on a plus de joueurs il faudrait un tableau de remote_peer_fd
                if (remote_peer_fd >= 0) {
                    close(remote_peer_fd); // On écrase l'ancien pour l'instant
                }
                remote_peer_fd = new_socket;
            } else {
                log_error("[Network] Erreur accept: %s", strerror(errno));
            }
        }

        // Un message réseau arrive d'un joueur distant
        if (remote_peer_fd >= 0 && FD_ISSET(remote_peer_fd, &readfds)) {
            char buffer[1024];
            ssize_t bytes_read = read(remote_peer_fd, buffer, sizeof(buffer));
            
            if (bytes_read == 0) {
                log_info("[Network] Joueur distant déconnecté.");
                close(remote_peer_fd);
                remote_peer_fd = -1;
            } else if (bytes_read > 0) {
                log_info("[Network] Reçu %zd octets depuis le réseau. A router vers IPC...", bytes_read);
                // TODO: Traiter le message réseau (protocole) 
                // et l'envoyer au Python via: ipc_connection_send(ipc_conn, &msg);
            }
        }

        // Le jeu Python local nous envoie un message via IPC
        if (ipc_conn->fd >= 0 && FD_ISSET(ipc_conn->fd, &readfds)) {
            IPCMessage msg;
            // On a mis 0 en timeout car select a déjà dit qu'il y a des données prêtes
            int rc = ipc_connection_poll(ipc_conn, &msg, 0); 
            
            if (rc < 0) {
                log_error("[IPC] Python local déconnecté.");
                break; // Ou gérer la reconnexion
            } else if (rc > 0) {
                log_info("[IPC] Reçu message (Type: %d, Taille: %u) de Python. A envoyer sur le réseau...", msg.type, msg.payload_size);
                // TODO: Traduire `msg` selon ton protocole réseau et l'envoyer 
                // via TCP au remote_peer_fd avec write() ou send().
                
                ipc_message_free(&msg); // Nettoyage de la mémoire allouée par ipc_connection_poll
            }
        }
    }

    // NETTOYAGE
    log_info("[Démon] Arrêt propre du démon...");
    if (remote_peer_fd >= 0) close(remote_peer_fd);
    close(network_server_fd);
    ipc_connection_destroy(ipc_conn);

    return EXIT_SUCCESS;
}