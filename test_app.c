#include "medievail_net/app.h"
#include <stdio.h>

int main() {
    // Configuration manuelle pour le test
    char *peers[] = {"127.0.0.1:20001"}; // On simule un autre joueur sur le port 20001
    NetAppConfig config = {
        .player_id = "Player1",
        .ipc_endpoint = "127.0.0.1:21000", // Port où Python va se connecter
        .listen_port = 20000,              // Port où le démon écoute le réseau
        .peer_addresses = peers,
        .peer_count = 1
    };

    // Lance la boucle infinie du démon
    return net_app_run(&config);
}