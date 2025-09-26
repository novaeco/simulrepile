# SimulRepile Firmware

## Objectif
Plateforme éducative simulant des terrariums réalistes sur carte Waveshare ESP32-S3-Touch-LCD-7B. Tout est virtuel (capteurs/actionneurs simulés) pour sensibiliser le public aux responsabilités légales et éthiques liées aux reptiles.

## Compatibilité carte Waveshare ESP32-S3-Touch-LCD-7B
- Résolution et timings : le BSP configure l'afficheur RGB parallèle en 16 bits avec une horloge PCLK de 16 MHz et une résolution 1024x600 conforme aux deux révisions ST7262 (rev. A) et AXS15231B (rev. B).
- Sélection de la révision matérielle via `menuconfig` (choix entre rev. A et rev. B) modifiant automatiquement l'affectation des broches de l'afficheur, de la dalle tactile GT911 et du lecteur microSD.
- Initialisation matérielle : les fonctions `bsp_display_init`, `bsp_touch_init` et `bsp_sdcard_init` sont actuellement des squelettes qui journalisent les étapes à réaliser. Il reste à intégrer les appels `esp_lcd_new_rgb_panel`, le pilote I2C GT911 et la pile SDMMC pour disposer d'une application totalement opérationnelle.
- LVGL : la configuration par défaut prépare une tâche LVGL avec une pile de 8 kio et une période de tick de 5 ms (PSRAM obligatoire). L'intégration effective nécessite toujours l'appel à `esp_lvgl_port_init()` avant `lv_init()`.

> 📌 **À faire pour un support complet :** implémenter le pilote LCD RGB (`esp_lcd_new_rgb_panel`), initialiser le contrôleur tactile GT911 (I2C) et monter le lecteur microSD via `esp_vfs_fat_sdmmc_mount`. Ces éléments sont indispensables avant de flasher sur la carte.

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
