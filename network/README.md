# Structure medievail_net

Ce dossier sert uniquement de squelette pour préparer le module réseau avant d’écrire le code.

## Organisation
- CMakeLists.txt — expliquera plus tard les cibles CMake (bibliothèque core + exécutable démon) et les drapeaux spécifiques aux plateformes.
- include/medievail_net/app.h — contiendra la structure de configuration publique (NetAppConfig) et la fonction d’entrée 
et_app_run.
- include/medievail_net/ipc.h — décrira les abstractions IPC : types de messages, handle opaque, helpers d’envoi/réception.
- include/medievail_net/log.h — regroupera les prototypes de journalisation communs au démon.
- src/core/log.c — zone prévue pour l’implémentation des logs (horodatage, verbosité, etc.).
- src/core/app.c — accueillera la boucle principale du démon qui fera le lien entre IPC local et réseau.
- src/ipc/win_named_pipe.c — hébergera le backend IPC Windows basé sur les named pipes (et les équivalents plus tard).
- src/daemon_stub.c — point d’entrée CLI chargé de parser les arguments puis de lancer 
et_app_run.

On remplira chaque fichier quand l’architecture aura été validée ; pour l’instant ils fixent seulement la place de chaque responsabilité.

