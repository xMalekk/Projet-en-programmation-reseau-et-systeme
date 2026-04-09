// API publique du démon réseau medievail_net
// Décrit la configuration nécessaire et la fonction d'entrée principale.

#pragma once

#include <stddef.h>

typedef struct NetAppConfig {
    const char *ipc_endpoint;          // chaîne "host:port" pour le canal UDP Python↔daemon (ex: 127.0.0.1:21000)
    const char *player_id;             // identifiant local (utilisé pour les logs/rapports)
    const char *const *peer_addresses; // tableau de chaînes "host:port" des autres joueurs
    size_t peer_count;                 // taille du tableau peer_addresses
    int listen_port;                   // port UDP réseau local (<=0 => valeur par défaut)
    int enable_ipv6;                   // réservé pour extensions futures
} NetAppConfig;

int net_app_run(const NetAppConfig *config);
