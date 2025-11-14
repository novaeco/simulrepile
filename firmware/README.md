# SimulRepile – Firmware afficheur

Ce firmware cible l'écran Waveshare ESP32-S3-Touch-LCD-7B et fournit l'interface utilisateur complète du simulateur.

## Fonctionnalités principales

- LVGL 9.4 + LovyanGFX avec thème haut contraste et navigation par onglets (Dashboard, Sauvegardes, Documents, Paramètres, À propos).
- Internationalisation dynamique (FR/EN/DE/ES) chargée depuis `/sdcard/i18n/*.json` avec bascule à chaud.
- Service d'autosauvegarde et sauvegardes manuelles (`persist/save_service.*`) avec journalisation, TTS optionnel et restauration instantanée.
- Lecteur documentaire SD (TXT/HTML) adossé au cache d'assets PSRAM.
- Mise à jour applicative via carte SD (`updates/updates_manager.*`).
- Surveillance du lien Core Link (UART propriétaire) et reprise locale si la carte cœur est indisponible.

## Organisation

- `main/app_main.c` : séquence d'initialisation (BSP, cache, i18n, autosave, LVGL, Core Link).
- `main/ui/` : vues LVGL (dashboard, slots, documents, paramètres, à propos) et thème.
- `main/persist/` : gestionnaire de sauvegardes (`save_manager`) + service autosave (`save_service`).
- `main/sim/` : moteur de simulation côté afficheur (export/import d'état).
- `main/docs/` : lecteur documentaire SD adossé au cache d'assets.
- `main/tts/` : stub TTS (journalisation, activable via Kconfig/paramètres).
- `data/` : contenu carte SD d'exemple (i18n, documents, sauvegardes).
- `components/` : port LVGL et interface de compression.

## Pré-requis

1. ESP-IDF ≥ 6.1 (`. ./export.sh`).
2. LVGL 9.4 (esp_lvgl_port 2.6.x) et LovyanGFX fournis via les composants inclus.
3. Carte SD formatée FAT contenant au minimum les dossiers `i18n/`, `docs/`, `saves/`.

## Build & flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Tests

- Tests Unity à ajouter (voir `GAPS.md`, GAP-016) pour couvrir autosave/restauration.
- Pour l'instant la validation est manuelle : naviguer dans l'UI, déclencher sauvegarde/rechargement, vérifier les journaux (`save_service`).

## Données carte SD

```
/sdcard
├── i18n/fr.json, en.json, de.json, es.json
├── docs/
│   ├── reglementaires/
│   ├── species/
│   └── guides/
└── saves/           # fichiers générés par l'autosave/manual save
```

## Accessibilité

- Thème haut contraste activable via les paramètres.
- Stub TTS activable/désactivable depuis l'UI (`Paramètres > Accessibilité`). Une implémentation audio complète reste à intégrer (GAP-017).
