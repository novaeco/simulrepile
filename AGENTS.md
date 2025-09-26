Rôle & objectif

Tu es Codex Engineer chargé de livrer un firmware ESP-IDF (≥ 5.5) + LVGL 9.x + LovyanGFX (RGB) pour Waveshare ESP32-S3-Touch-LCD-7B (1024×600).
Cible : simulateur éducatif façon Tamagotchi réaliste pour sensibiliser et réduire les achats impulsifs. Le système doit être fiable, évolutif, portable, et respecter les réglementations FR/UE/International (élevage, vente/cession, bien-être animal) applicables aux jeux éducatifs.

Clé fonctionnelle : tout est simulé (capteurs/actionneurs virtuels). Aucune dépendance à des capteurs réels. Toutes les données, docs et médias résident sur carte SD.

Plateforme & ressources matérielles (officielles)

Module : ESP32-S3-WROOM-1-N16R8 (16 MB Flash, 8 MB PSRAM).

Écran : LCD RGB 7″ 1024×600, interface RGB parallèle.

Tactile : capacitif I²C, 5 points, IRQ.

Stockage : TF/microSD (obligatoire).

Interfaces : Full-Speed USB, USB-UART (CH343) via switch, CAN, RS485, I²C, UART, capteur header.

Alim & batterie : Li-ion (charge intégrée), lecture tension batt et réglage luminosité backlight supportés.

Lignes clés (pinouts Waveshare) → à implémenter dans le BSP, source unique de vérité :

LCD RGB

HSYNC: GPIO46 VSYNC: GPIO3 DE: GPIO5 PCLK: GPIO7

R: R3 = GPIO1, R4 = GPIO2, R5 = GPIO42, R6 = GPIO41, R7 = GPIO40

G: G2 = GPIO39, G3 = GPIO0, G4 = GPIO45, G5 = GPIO48, G6 = GPIO47, G7 = GPIO21

B: B3 = GPIO14, B4 = GPIO38, B5 = GPIO18, B6 = GPIO17, B7 = GPIO10

EXIO (CH422G) : EXIO2=DISP (backlight enable), EXIO6=LCD_VDD_EN (VCOM enable)

Touch I²C : TP_IRQ=GPIO4, TP_SDA=GPIO8, TP_SCL=GPIO9, TP_RST=EXIO1

USB natif : USB_DN=GPIO19, USB_DP=GPIO20, USB_SEL=EXIO5 (tirer bas → mode USB, sinon CAN)
(Ces mappages proviennent de la section Pinouts de la page officielle.) 
Waveshare

Note SD : la carte intègre un slot TF. Implémente le driver sdmmc (ESP-IDF) en 1-bit ou 4-bits selon câblage réel. Expose les pins via Kconfig et centralise-les dans bsp/pins_sd.h. (Par défaut, aligne-toi sur le projet Waveshare “07_SD”.) 
Waveshare

Fonctionnalités (MVP)

Multi-terrariums (≤ 4)
Modèles Terrarium, Reptile, Environment, Nutrition, Health, CareHistory. Paramètres simulés : T/H/Lux (jour/nuit/saisons), hydratation, mue, stress/activité, alimentation, historique des soins.

Sauvegarde/chargement sur SD
Auto + Manuel. Fichiers JSON encapsulés (en-tête : MAGIC, VERSION, FLAGS, CRC32). .bak redondant. Export/Import par simple copie de fichiers.

Bibliothèque réglementaire & pédagogique (sur SD)
Arbo : /docs/reglementaires/, /docs/species/, /docs/guides/. Lecteur intégré TXT/HTML minimal + navigation catégorisée. (PDF : ouvrir externe/fallback texte si pas de rendu PDF.)

Conformité légale & éthique
Disclaimer obligatoire au premier lancement + page “À propos” (mentions). Aucune incitation à la maltraitance. Rappels réglementaires (élevage amateur/pro, cession, bien-être).

Perf & robustesse
PSRAM pour framebuffers/caches LVGL. I/O SD asynchrones. Asset manager (LRU). Journal circulaire minimal contre corruption. Option compression (abstraction, LZ4/heatshrink/miniz activable).

Accessibilité & i18n
Thème haut contraste, grande police, TTS (stub). Fichiers /i18n/{fr,en,de,es}.json chargeables à chaud.

MAJ & fiabilité
MAJ par SD (/updates/update.bin + manifeste). OTA Wi-Fi possible (désactivé par défaut). CRC à chaque lecture + rollback sur .bak.

Architecture
/firmware
  /main
    app_main.c
    /ui           # LVGL: root, dashboard, slots, docs, settings, thème
    /sim          # moteur de simulation + presets espèces
    /persist      # save_manager (JSON+CRC+.bak) + schema_version.h
    /assets       # cache LRU images/textes
    /docs         # lecteur TXT/HTML minimal
    /bsp          # waveshare_7b.[ch], exio.[ch], pins_lcd.h, pins_sd.h, pins_touch.h, pins_usb.h
    /i18n         # loader i18n
  /components
    lvgl_port/    # LVGL 9, double-buffer PSRAM, vsync/tick
    compression_if/  # abstraction compression (opt.)
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  README.md
  AGENTS.md


Règles BSP

Une seule source de vérité pour les pins (headers pins_*.h), pré-remplie avec les mappages officiels ci-dessus.

EXIO (CH422G) : fournir exio_set(level) pour DISP (EXIO2), LCD_VDD_EN (EXIO6), USB_SEL (EXIO5).

Backlight : bsp_backlight_set(brightness) (LEDC PWM) + enable via EXIO2.

Batterie : exposer bsp_battery_read_mv() (affichage dans UI “A propos”/“Batterie”).

UI/UX (exigences)

Accueil : 4 slots (états synthétiques).

Dashboard : jauges T/H/L (simulées), état santé/comportements (icônes), alimentation, historique, alertes.

Documents : catégories + lecteur TXT/HTML.

Paramètres : langue, thème accessibilité, autosave, modules (Wi-Fi/OTA/TTS), sélecteur USB↔CAN (EXIO5), sélection pins SD (si variable).

A propos / Légal : disclaimer, mentions, versions, licences.

Build & configuration

Cible : idf.py set-target esp32s3

Profil LVGL : 16-bit, double-buffer, 50–60 FPS.

partitions.csv : factory, ota_0, ota_1, nvs, phy.

sdkconfig.defaults : PSRAM ON, log INFO, options i18n/accessibilité.

Kconfig (extraits) :

APP_MAX_TERRARIUMS (def: 4)

APP_AUTOSAVE_INTERVAL_S (def: 120)

APP_ENABLE_COMPRESSION (bool, def: off)

APP_ENABLE_WIFI_OTA (bool, def: off)

APP_ENABLE_TTS_STUB (bool, def: off)

APP_LANG_DEFAULT {fr,en,de,es} (def: fr)

APP_THEME_HIGH_CONTRAST (bool, def: on)

BSP_SD_BUS_WIDTH {1BIT,4BITS} (def: 1BIT)

BSP_USB_CAN_SELECTABLE (bool, def: on) // pilote EXIO5

Qualité, conformité & sécurité

ISO/IEC 25010 (fiabilité, perf, maintenabilité) : clang-format + lint (cppcheck/clang-tidy) + Unity tests.

Traçabilité README (process build/test/release).

Intégrité : CRC32 + rollback .bak.

Légal/éthique : disclaimer au premier boot, section réglementaire accessible.

Tests (DoD)

Affichage 1024×600 OK + tactile I²C opérationnel (IRQ).

EXIO : DISP (BL enable), LCD_VDD_EN, USB_SEL (USB↔CAN) testés.

Sauvegardes : création/lecture 4 slots, CRC OK, rollback testé.

Simulation : dynamique T/H/L et impacts santé/comportements visibles.

Docs : navigation + lecture TXT/HTML depuis /docs/**.

i18n : FR/EN à chaud.

Perf : UI fluide, I/O SD non bloquants.

BSP SD : bus 1-bit/4-bits conforme câblage, montage stable.

Batterie & backlight : lecture tension affichée, luminosité réglable.

Livrables : CMakeLists, sdkconfig.defaults, partitions.csv, README, docs dev + exemples /docs, /i18n, /saves.

Plan d’implémentation

BSP & EXIO (LCD RGB, GT-style touch, SD, USB_SEL, backlight, batterie) → splash LVGL.

Squelette UI (root, slots, dashboard, docs, settings, thème).

Moteur de simulation (tick + modèles + presets).

Persistance JSON+CRC+.bak (+ abstraction compression).

Lecteur docs + arbo SD.

i18n & accessibilité.

Autosave + Export/Import.

MAJ par SD (stub OTA).

Tests Unity + stabilisation perf.