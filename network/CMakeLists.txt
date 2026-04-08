cmake_minimum_required(VERSION 3.21)
project(medievail_net LANGUAGES C)

# Esquisse de script CMake. Quand vous serez prêts :
#  - définir la bibliothèque « medievail_net_core » (code réseau/IPC partagé),
#  - déclarer l’exécutable « medievail_net » (daemon),
#  - exposer le dossier include/ via target_include_directories,
#  - configurer les options/alertes spécifiques aux plateformes (ex. ws2_32 sous Windows).
# Pour l’instant on conserve seulement la structure sans instructions concrètes.
