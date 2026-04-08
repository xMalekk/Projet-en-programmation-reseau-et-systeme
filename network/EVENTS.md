# Schéma d’événements réseau

Tous les messages échangés entre Python ⇄ démon C et entre pairs distants utilisent le même gabarit :

```
{
  type: <nom de l’événement>,
  tick: <numéro de tour ou horodatage logique>,
  unit_id: <identifiant unique si pertinent, sinon null>,
  payload: { … données spécifiques … }
}
```

## Identifiants communs
- `tick` : numéro de tour produit par l’Engine (permet de rejouer dans l’ordre et de détecter les trous).
- `unit_id` : composé de l’équipe et d’un compteur local (ex. `R-12`). Il reste constant pendant toute la vie de l’entité.
- `player_id` : chaîne assignée lors du lancement (`BattleCLI`).

## Liste des types d’événements

| Type | Payload attendu | Usage |
| ---- | ----------------| ----- |
| `JOIN` | `{ player_id, scenario, seed }` | Annonce la présence d’un joueur et les paramètres de partie. |
| `UNIT_SPAWN` | `{ team, unit_type, position:{x,y}, hp }` | Synchronise l’apparition d’une nouvelle unité. |
| `UNIT_MOVE` | `{ from:{x,y}, to:{x,y}, speed }` | Informe les pairs d’un déplacement. |
| `UNIT_STATE` | `{ position:{x,y}, hp, status, target }` | Rafraîchit périodiquement l’état pour corriger les divergences. |
| `UNIT_ATTACK` | `{ target_id, damage, attack_type }` | Signale un coup porté (corps-à-corps ou projectile). |
| `PROJECTILE_SPAWN` | `{ origin:{x,y}, direction:{x,y}, projectile_type }` | Optionnel : suivi visuel des tirs. |
| `PROPERTY_REQUEST` | `{ action, args }` | Demande la propriété réseau d’une unité avant d’exécuter l’action. |
| `PROPERTY_GRANT` | `{ owner: player_id, state: { position, hp, status } }` | Transfère la propriété et fournit l’état cohérent. |
| `PROPERTY_DENY` | `{ reason }` | Refus d’une demande (unité détruite, autre action prioritaire, etc.). |
| `PROPERTY_RELEASE` | `{ next_owner?: player_id }` | Rend volontairement la propriété après une action. |
| `REPORT` | `{ kind, data }` | Événements hors combat (rapports, sauvegardes, pause). |
| `SHUTDOWN` | `{}` | Demande d’arrêter la session proprement. |

## Notes d’implémentation
- Les événements sont sérialisés en JSON pour simplifier le prototypage. On pourra passer à un format binaire plus compact plus tard si nécessaire.
- Les horodatages (`tick`) sont obligatoires pour permettre au démon d’ordonner les messages et de détecter les trous (demande de retransmission possible).
- Les champs `payload` doivent rester aussi petits que possible ; si une valeur n’est pas pertinente pour un événement donné, on l’omet.
- Pour la première version « best effort », on peut se limiter à `JOIN`, `UNIT_SPAWN`, `UNIT_MOVE`, `UNIT_ATTACK`, `UNIT_STATE`, `SHUTDOWN`.
- La seconde version (cohérence forte) active `PROPERTY_REQUEST/GRANT/DENY/RELEASE` afin de respecter la « propriété réseau ».
