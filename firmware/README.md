# SimulRepile Firmware Skeleton

Ce répertoire contient un squelette complet pour le firmware ESP-IDF (≥5.5) ciblant la carte Waveshare ESP32-S3-Touch-LCD-7B.
Il fournit l'architecture logicielle, les points d'entrée et les stubs nécessaires pour développer le simulateur éducatif.

## Organisation

- `main/` : application principale et sous-modules (BSP, UI, simulation, persistance, i18n, assets, docs).
- `components/` : composants réutilisables (`lvgl_port`, `compression_if`).
- `data/` : exemples de ressources destinées à la carte SD (documents réglementaires, i18n, sauvegardes).
- `sdkconfig.defaults` : configuration par défaut orientée ESP32-S3 avec PSRAM et LVGL.
- `partitions.csv` : partitionnement dual-OTA + stockage FAT pour la carte SD virtuelle.

## Architecture maître/esclave

La branche courante implémente l’option **B** : la carte Waveshare reste un terminal graphique/entrée tactile tandis que toute
la logique métier tourne sur une ESP32-S3-DevKitC-1 (module WROOM-2). Les deux cartes sont reliées par un lien UART haute
vitesse (par défaut 2 Mbps sur UART1, GPIO43↔GPIO44) encapsulant un protocole binaire minimal (`link/core_link.*`).

- **Handshake & capabilities** : au démarrage, le DevKitC émet `HELLO`, la Waveshare acquitte avec `HELLO_ACK` et publie sa
  résolution dans un message `DISPLAY_READY`.
- **Synchronisation d’état** : le DevKitC diffuse un `STATE_FULL` initial puis des trames `STATE_DELTA` compactes (masques de
  champs) pour ne pousser que les variations. L’afficheur fusionne incrémentalement ces deltas, conserve un cache local et
  redemande un `STATE_FULL` en cas d’incohérence.
- *Note :* un `STATE_FULL` rafraîchit périodiquement la base (≈30 s ou sur demande) afin de garantir une re-synchronisation
  robuste même si des deltas sont perdus.
- **Événements tactiles** : l’API `core_link_send_touch_event()` prépare l’intégration future du driver I²C pour remonter les
  contacts vers le cœur.

Les paramètres (port UART, broches, timeout handshake) sont accessibles dans `idf.py menuconfig → SimulRepile Application
Configuration → Core Link Bridge` et possèdent des valeurs par défaut adaptées au câblage direct entre les deux cartes.

Le firmware maître (DevKitC) se trouve dans `../core_firmware/`. Il publie les instantanés simulés sur la même structure de
trames (`common/include/link/core_link_protocol.h`) et gère les événements tactiles pour ajuster la simulation.

## Pré-requis

1. Installer ESP-IDF 5.5 ou supérieur et activer l'environnement (`. ./export.sh`).
2. Récupérer LVGL 9.x et LovyanGFX via `idf.py add-dependency` ou en plaçant les bibliothèques dans `components/`.
3. Optionnel : connecter un écran Waveshare ESP32-S3-Touch-LCD-7B ou utiliser un émulateur LVGL.

## Compilation

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash & moniteur

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## Tests unitaires

Les squelettes Unity peuvent être ajoutés ultérieurement dans `components/` ou `main/tests`. Prévoir `idf.py unity-test`.

## Prochaines étapes

- Remplacer les stubs BSP par les pilotes réels (RGB LCD, toucher I²C, CH422G, SDMMC, batterie, USB/CAN).
- Connecter LVGL 9.x et LovyanGFX pour la pile graphique complète.
- Implémenter la simulation comportementale et la persistance JSON/CRC.
- Finaliser l'interface utilisateur accessible et multilingue.
