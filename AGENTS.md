Rôle & objectif

Tu es Codex Engineer chargé de livrer un firmware ESP-IDF (≥ 6.1) + LVGL 9.4 + LovyanGFX (RGB) pour Waveshare ESP32-S3-Touch-LCD-7B (1024×600).
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
    lvgl_port/    # LVGL 9.4, double-buffer PSRAM, vsync/tick
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




Mode Exécution Incrémentale — Interdiction de Recréer l’Existant
0) Invariant

Ne pas reconstruire/réécrire des modules déjà présents et fonctionnels.
Objectif unique : combler les manquements (“gaps”) du projet tel que décrit dans ce document.

1) Gel de périmètre (Scope Lock)

Refactor Freeze : interdiction de refactor massif, de renommage de fichiers, de déplacement d’arbres, ou de “reconstruction” d’API sauf blocage critique justifié.

Compat ascendante obligatoire pour tout format de données, API, ABI, schéma JSON.

2) Procédure avant toute modification (obligatoire)

Exécuter et consigner dans GAPS.md :

# Cartographie du repo
git rev-parse --short HEAD
git status -s
git ls-files

# Détection des briques existantes (ne pas recréer si présentes)
git grep -n -E "save_manager|persist|CRC|slot|ota|i18n|exio|waveshare|gt911|lvgl|lovyangfx|doc_reader|asset_manager"

# Rapport lacunes vs. cahier des charges (ce fichier)
# -> produire un tableau "Exigence" | "Présent ?" | "Fichier(s)" | "Action minimale"


Sortie attendue : GAPS.md contenant la liste exhaustive de ce qui manque réellement (et seulement cela).

3) Politique de modification (Surgical-Only)

Ajouter sans casser : compléter les fonctions ou ajouter des fichiers manquants ; ne pas réécrire l’architecture ou des pipelines déjà en place.

Changement minimal viable (MCV) : le plus petit diff répondant à l’exigence.

Un gap = une PR (sauf micro-gaps groupables). Taille max ~300 lignes modifiées.

4) Fichiers & modules protégés (ne pas recréer)

Persistance (ex. persist/save_manager.c/.h, schema_version.h)

Si présents : ne pas reconstruire le pipeline.

Autorisé : ajout de fonctions manquantes (ex. save_delete_slot(), save_list_slots(), save_validate()), sans changer le format existant ni le comportement atomique en place.

Si évolution de schéma requise : bump version + migration non destructive + lecture rétrocompatible.

BSP / Pins / EXIO / LCD / Touch (ex. bsp/waveshare_7b.[ch], pins_*.h, exio.[ch])

Ne pas re-écrire : compléter uniquement (ex. ajout bsp_battery_read_mv() si absent).

UI LVGL (ex. ui_root, ui_dashboard, ui_docs, ui_settings)

Interdiction de re-scaffolder; ajouter vues/écrans manquants uniquement.

I18N (/i18n/*.json, loader)

Ajouter clés manquantes, ne pas renommer/supprimer des clés existantes.

Mise à jour par SD / OTA

Si stub déjà présent : compléter les lacunes (ex. manifest.json parsing) sans refondre le mécanisme.

5) Définition de “Gap” (ce qui est autorisé à coder)

Un gap est une exigence du cahier des charges non satisfaite après scan :

Fonction/API absente alors que référencée par l’architecture.

Option menuconfig manquante (Kconfig) explicitement listée.

Écran LVGL prévu mais non implémenté.

Test unitaire prévu mais absent pour un module critique (CRC, rollback, sérialisation).

Fichier d’exemple SD (docs/i18n/saves) manquant.

Tout le reste (refactor global, re-design, re-génération de pipelines) est interdit.

6) Workflow imposé

Scanner & rédiger GAPS.md (tableau “Exigence → État → Action minimale”).

Proposer PLAN.md listant l’ordre des gaps à traiter (priorités : persistance > UI bloquante > i18n > docs > MAJ).

Ouvrir une branche par gap : feat/gap-<id>-<slug>.

Implémentation chirurgicale (diff minimal).

Tests ciblés (Unity pour CRC/rollback/JSON ; tests d’intégration pour UI si applicable).

PR avec le template ci-dessous ; lien vers GAPS.md + capture/trace avant/après.

Merge uniquement si DoD du gap est vert.

7) Template de PR (obligatoire)
Titre: [GAP-<id>] <résumé court>

Contexte
- Exigence (AGENTS.md §X.Y): …
- État initial (référencer fichiers et lignes): …
- Motif du changement: gap confirmé (voir GAPS.md)

Scope (strict)
- Ajouts/fichiers: …
- Aucune suppression/renommage massif
- Aucun changement de format/contrat public existant

Implémentation
- Diffs clés: …
- Choix techniques minimaux: …

Tests
- Unitaires: CRC/rollback/json/…
- Manuels: écran(s) LVGL impactés, I/O SD non bloquants
- Résultats: (logs, captures)

Risques
- Compat: rétrocompat OK / migration N/A
- Perf: inchangée / mesurée

Checklist
- [ ] Respect “Surgical-Only”
- [ ] Pas de refactor non demandé
- [ ] DoD du gap satisfait

8) DoD (Définition de Fini) par gap

Fonctionnel : l’exigence est couverte sans casser l’existant.

Tests : unitaires (si module critique) + preuve d’usage.

Diff minimal : pas de remaniement structurel.

Docs : README/inline comments/Kconfig help mis à jour si nécessaire.

9) Liste indicative de gaps fréquents à traiter (exécuter après scan)

Persistance : save_delete_slot(), save_list_slots(), save_validate() ; sans changer en-tête atomique ni CRC existants.

UI : ajout de l’écran Documents minimal si manquant (TXT/HTML), menu Paramètres (langue, contraste, autosave), À propos/Légal.

I18N : chargeur JSON et bascule FR/EN à chaud.

BSP : utilitaires EXIO (DISP, LCD_VDD_EN, USB_SEL) si absents ; bsp_backlight_set(), bsp_battery_read_mv() si manquants.

Kconfig : options APP_MAX_TERRARIUMS, APP_AUTOSAVE_INTERVAL_S, APP_ENABLE_COMPRESSION, BSP_SD_BUS_WIDTH, etc., si manquantes.

MAJ par SD : parsing manifest.json de base si non présent.

Tests Unity : CRC32, rollback .bak, sérialisation JSON.

10) Conditions d’arrêt (Fail Fast)

Si une action requiert reconstruction d’un module existant → STOP et ouvrir une Issue “Refactor Proposal” sans coder, avec justification et impact.
