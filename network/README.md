# Structure medievail_net

Ce dossier sert de base au démon réseau.

## Organisation
- CMakeLists.txt — configure la bibliothèque medievail_net_core et l’exécutable medievail_net.
- include/medievail_net/app.h — contient NetAppConfig (endpoint IPC, port UDP, liste de pairs) et la déclaration de 
et_app_run.
- include/medievail_net/ipc.h — expose l’API IPC commune (IPCMessage, ipc_connection_*).
- include/medievail_net/log.h — prototypes de journalisation partagés.
- src/core/log.c — implémentation de log_*.
- src/core/app.c — boucle principale : relie l’IPC UDP local au réseau UDP entre joueurs.
- src/ipc/udp_local.c — backend IPC basé sur UDP (même protocole que le réseau externe).
- src/daemon_stub.c — point d’entrée CLI qui analysera les arguments et lancera 
et_app_run.

Chaque fichier peut être rempli progressivement en suivant les besoins du projet.
