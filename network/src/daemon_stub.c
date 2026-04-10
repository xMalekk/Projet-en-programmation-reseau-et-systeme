// Gabarit pour le point d'entrée CLI du démon.
// Il analysera les arguments et invoquera net_app_run plus tard.

// Point d'entrée CLI du démon medievail_net.
// Il parse les arguments, construit NetAppConfig et appelle net_app_run().

#include "medievail_net/app.h"
#include "medievail_net/log.h"


#define _GNU_SOURCE

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Affiche l'aide d'utilisation du démon et liste les options acceptées.
static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  --ipc-endpoint <host:port>   Canal UDP local Python<->daemon (default 127.0.0.1:21000)\n");
    printf("  --listen-port <port>         Port UDP réseau pour les pairs (default 20000)\n");
    printf("  --peer <host:port>           Peer distant à contacter (option répétable)\n");
    printf("  --player-id <name>           Identifiant local (default anonymous)\n");
    printf("  --verbose                    Active les logs DEBUG\n");
    printf("  -h, --help                   Affiche cette aide\n");
}

// Duplique une chaîne en mémoire dynamique et quitte le programme si l'allocation échoue.
static char *copy_string_or_die(const char *value) {
    char *copy = strdup(value);
    if (!copy) {
        fprintf(stderr, "Erreur mémoire lors de la copie de chaîne.\n");
        exit(EXIT_FAILURE);
    }
    return copy;
}

// Point d'entrée principal. Parse la ligne de commande, construit la configuration
// NetAppConfig, appelle net_app_run() et effectue le nettoyage mémoire.
int main(int argc, char *argv[]) {
    const char *default_ipc = "127.0.0.1:21000";
    const char *default_player = "anonymous";
    char *ipc_endpoint = copy_string_or_die(default_ipc);
    char *player_id = copy_string_or_die(default_player);
    char **peer_storage = NULL;
    size_t peer_count = 0;
    int listen_port = 20000;
    bool verbose = false;

    // Déclare les options longues acceptées par le programme pour le parsing.
    static struct option long_options[] = {
        {"ipc-endpoint", required_argument, NULL, 0},
        {"listen-port", required_argument, NULL, 0},
        {"peer", required_argument, NULL, 0},
        {"player-id", required_argument, NULL, 0},
        {"verbose", no_argument, NULL, 0},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    // Parcourt les arguments de ligne de commande et met à jour les variables de configuration.
    int option_index = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, &option_index)) != -1) {
        if (opt == 'h') {
            print_usage(argv[0]);
            free(ipc_endpoint);
            free(player_id);
            return EXIT_SUCCESS;
        }

        if (opt == 0) {
            const char *name = long_options[option_index].name;
            if (strcmp(name, "ipc-endpoint") == 0) {
                free(ipc_endpoint);
                ipc_endpoint = copy_string_or_die(optarg);
            } else if (strcmp(name, "listen-port") == 0) {
                listen_port = atoi(optarg);
                if (listen_port <= 0 || listen_port > 65535) {
                    fprintf(stderr, "Port invalide : %s\n", optarg);
                    goto failure;
                }
            } else if (strcmp(name, "peer") == 0) {
                char *peer_value = copy_string_or_die(optarg);
                char **next = realloc(peer_storage, (peer_count + 1) * sizeof(*next));
                if (!next) {
                    fprintf(stderr, "Erreur mémoire lors de l'ajout de peer.\n");
                    free(peer_value);
                    goto failure;
                }
                peer_storage = next;
                peer_storage[peer_count++] = peer_value;
            } else if (strcmp(name, "player-id") == 0) {
                free(player_id);
                player_id = copy_string_or_die(optarg);
            } else if (strcmp(name, "verbose") == 0) {
                verbose = true;
            }
        }
    }

    // Initialise le logging puis active le mode verbeux si demandé.
    log_init(0);
    if (verbose) {
        log_set_verbose(1);
    }

    // Affiche le résumé de la configuration du démon pour vérification.
    printf("[daemon] ipc-endpoint=%s\n", ipc_endpoint);
    printf("[daemon] listen-port=%d\n", listen_port);
    printf("[daemon] player-id=%s\n", player_id);
    printf("[daemon] peers=%zu\n", peer_count);
    for (size_t i = 0; i < peer_count; ++i) {
        printf("  - %s\n", peer_storage[i]);
    }

    // Assemble la structure NetAppConfig avec les valeurs lues.
    NetAppConfig config = {
        .ipc_endpoint = ipc_endpoint,
        .player_id = player_id,
        .peer_addresses = (const char * const *)peer_storage,
        .peer_count = peer_count,
        .listen_port = listen_port,
        .enable_ipv6 = 0,
    };

    int rc = net_app_run(&config);

    // Libère toutes les ressources allouées avant la sortie.
    for (size_t i = 0; i < peer_count; ++i) {
        free(peer_storage[i]);
    }
    free(peer_storage);
    free(ipc_endpoint);
    free(player_id);

    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

failure:
    for (size_t i = 0; i < peer_count; ++i) {
        free(peer_storage[i]);
    }
    free(peer_storage);
    free(ipc_endpoint);
    free(player_id);
    return EXIT_FAILURE;
}

