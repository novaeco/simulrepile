# SimulRepile Firmware Skeleton

Ce répertoire contient un squelette complet pour le firmware ESP-IDF (≥5.5) ciblant la carte Waveshare ESP32-S3-Touch-LCD-7B.
Il fournit l'architecture logicielle, les points d'entrée et les stubs nécessaires pour développer le simulateur éducatif.

## Organisation

- `main/` : application principale et sous-modules (BSP, UI, simulation, persistance, i18n, assets, docs).
- `components/` : composants réutilisables (`lvgl_port`, `compression_if`).
- `data/` : exemples de ressources destinées à la carte SD (documents réglementaires, i18n, sauvegardes).
- `sdkconfig.defaults` : configuration par défaut orientée ESP32-S3 avec PSRAM et LVGL.
- `partitions.csv` : partitionnement dual-OTA + stockage FAT pour la carte SD virtuelle.

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
