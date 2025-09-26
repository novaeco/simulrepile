# SimulRepile Firmware

## Objectif
Plateforme éducative simulant des terrariums réalistes sur carte Waveshare ESP32-S3-Touch-LCD-7B. Tout est virtuel (capteurs/actionneurs simulés) pour sensibiliser le public aux responsabilités légales et éthiques liées aux reptiles.

## Structure
```
firmware/
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  main/
    app_main.c
    Kconfig.projbuild
    bsp/
    ui/
    sim/
    persist/
    assets/
    docs/
    i18n/
    compression/
    update/
    logging/
  components/
  sdcard/
    docs/
    i18n/
```

## Fonctionnalités clés
- Gestion de 4 terrariums virtuels avec modèles espèces, nutrition et soins.
- Simulation environnementale (cycles jour/nuit, saisons, santé, comportements).
- Persistance JSON sur microSD avec CRC32, sauvegardes .bak et compression optionnelle.
- Interface LVGL : tableau de bord, liste de slots, bibliothèque documentaire avec recherche, paramètres d’accessibilité.
- Internationalisation dynamique via `/sdcard/i18n/*.json` (fr/en/de/es extensibles).
- Journal circulaire exportable et sauvegarde automatique configurée via `APP_AUTOSAVE_INTERVAL_S`.
- Vérification d’updates SD (`updates/manifest.json`) avec calcul CRC et support signature.

## Pré-requis
- ESP-IDF v5.5 ou supérieur (`export.sh`).
- LVGL 9.x via composant ESP-IDF (`idf.py add-dependency "lvgl/lvgl^9"`).
- LovyanGFX (RGB) si nécessaire pour couches avancées.
- PSRAM activée (cf. `sdkconfig.defaults`).

## Build & flash
```bash
idf.py set-target esp32s3
idf.py fullclean build
idf.py -p /dev/ttyACM0 flash monitor
```

## Menuconfig (Kconfig)
- `SimulRepile Application → Maximum number of terrariums` (1–4)
- `SimulRepile Application → Waveshare 7B board revision` (ST7262 / AXS15231B)
- `SimulRepile Application → Enable save compression`
- `SimulRepile Application → Default language` (fr/en/de/es)
- `SimulRepile Application → High contrast theme`
- Chemins/buffers SD paramétrables.

## SD card layout
```
/sdcard/
  saves/slot1.sav (et .bak)
  docs/
    reglementaires/
    species/
    guides/
  i18n/fr.json
  updates/update.bin
```

Des exemples de documents sont disponibles dans `sdcard/docs`. Copier le dossier `sdcard/` à la racine de la carte.

## Tests unitaires
Des tests Unity (`firmware/tests`) couvrent CRC, compression, sérialisation et moteur de simulation. Lancer :
```bash
idf.py build -DTEST_COMPONENTS=tests
```

## Qualité & conformité
- Journalisation via `log_manager` (anneau minimal, stub).
- CRC32 (esp_rom_crc32) + rollback .bak.
- Architecture modulaire : UI LVGL, moteur simulation, persistance, BSP.
- Stub OTA/SD update prêt.

## Processus (ISO 9001)
1. `idf.py fullclean build`
2. Tests unitaires (`idf.py build -DTEST_COMPONENTS=tests`)
3. Flash + validation physique
4. Revue code & documentation
5. Tag release (`git tag vX.Y.Z`)
