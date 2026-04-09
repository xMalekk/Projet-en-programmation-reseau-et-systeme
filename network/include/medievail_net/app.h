// Gabarit pour l’API publique du démon réseau.
// On y décrira plus tard NetAppConfig (endpoint IPC, identifiant joueur, liste de pairs)
// ainsi que la signature de net_app_run.

#pragma once

#include <stddef.h>

// Structure de configuration du routeur réseau
typedef struct NetAppConfig {
    const char *ipc_endpoint;          // Chaîne "host:port" pour le canal UDP Python ↔ daemon (ex: "127.0.0.1:21000")
    const char *player_id;             // Identifiant local (utilisé pour les logs/rapports)
    const char *const *peer_addresses; // Tableau de chaînes "host:port" des autres joueurs distants
    size_t peer_count;                 // Taille du tableau peer_addresses
    int listen_port;                   // Port UDP réseau local (<= 0 implique la valeur par défaut, ex: 20000)
    int enable_ipv6;                   // Réservé pour extensions futures (0 par défaut)
} NetAppConfig;

// Démarre la boucle principale du démon réseau en fonction de la configuration fournie
int net_app_run(const NetAppConfig *config);