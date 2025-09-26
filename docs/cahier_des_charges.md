# Cahier des charges professionnel — Projet « SimulRepile »

## 1. Présentation générale
- **Nom du projet :** SimulRepile — faux terrarium éducatif et simulateur de reptiles.
- **Version du document :** v1.0.0
- **Responsable produit :** Association pédagogique / MOA
- **Responsable technique :** Équipe embarquée ESP32 / MOE
- **Date de rédaction :** 2025-09-26

## 2. Objectifs pédagogiques et sociétaux
1. **Sensibilisation** à la responsabilité liée à la détention de reptiles en captivité.
2. **Réduction des achats impulsifs** en simulant la complexité du maintien.
3. **Diffusion réglementaire** : mise à disposition des textes FR/EU/internationaux (CITES, I-FAP, CDC, RGPD).
4. **Support éducatif multi-publics** : associations, écoles, animaleries spécialisées, particuliers.
5. **Conformité normative** : ISO/IEC 25010 (qualité logicielle), ISO 9001 (gestion projet), directives jouets/éducatives, normes CE.

## 3. Périmètre fonctionnel
### 3.1 Gestion multi-terrariums (jusqu'à 4 en parallèle)
- Paramètres environnementaux : température (jour/nuit), hygrométrie, UV/visible, cycle photopériodique.
- Automates de régulation virtuelle : chauffage, brumisation, ventilation, UVB.
- Profil d'espèces : serpent, lézard, tortue, amphibien (extensible).
- Cycle alimentaire : planification, suivi ingestion, gestion stocks proies.
- Santé & bien-être : poids, comportement, signes cliniques, alertes vétérinaires.
- Journal de soins : interventions, nettoyage, enrichissement, reproduction.

### 3.2 Sauvegardes & synchronisation
- Sauvegardes auto/manuelles sur microSD au format JSON compressé (LZ4) + CRC32.
- Rotation de sauvegardes (N=5) + journalisation minimaliste.
- Import/export inter-appareils via microSD ou OTA sécurisé (TLS 1.3, WPA2-Enterprise support).

### 3.3 Base documentaire réglementaire et pédagogique
- Moteur de consultation PDF/HTML/TXT sur carte SD.
- Indexation thématique (législation, fiches espèces, guides pratiques).
- Mises à jour modulaires via packages `*.srpkg` signés (EdDSA25519).

### 3.4 Conformité légale et éthique
- Disclaimer initial (responsabilité, bien-être animal, sources officielles).
- Logs d'accès aux documents sensibles (RGPD-ready, anonymisés).
- Filtrage de contenus pour public <16 ans (contrôle parental).

### 3.5 Accessibilité et UX
- Interface LVGL 9.x 1024×600 responsive.
- Thèmes contraste élevé, mode daltonien, taille police ajustable.
- Synthèse vocale offline (ESP-SR) multilingue FR/EN/DE/ES.
- Navigation tactile GT911, gestuelle (swipe, pinch, long-press).

### 3.6 Performances et fiabilité
- Temps de boot < 6 s (cold), < 2 s (warm).
- Consommation moyenne < 400 mW en interaction, < 150 mW en veille profonde.
- Robustesse fichiers : vérification CRC, double écriture, watchdog logicielle.

## 4. Architecture matérielle
| Composant | Référence | Rôle | Interfaces |
|-----------|-----------|------|------------|
| MCU | ESP32-S3 (Waveshare ESP32-S3-Touch-LCD-7B) | CPU, Wi-Fi, BT | SPI, I²C, I²S, USB OTG |
| Écran | TFT 7" 1024×600 + AXS15231B/ST7262 | Affichage RGB | 16-bit RGB parallèle |
| Tactile | GT911 capacitif | Interface utilisateur | I²C @400 kHz |
| Stockage | microSD (SPI) | Sauvegardes, docs | SPI DMA |
| Audio | Amplificateur + HP 1 W | Feedback & TTS | I²S + DAC externe |
| Alim | CS8501 + Li-Ion 3.7 V | Gestion batterie | ADC monitoring |
| Extensions | RS485, CAN, USB | Mode conférence, instrumentation | UART, TWAI, USB-CDC |

### 4.1 Bloc diagramme logique
1. **ESP-IDF** (FreeRTOS SMP) orchestrant tâches : UI, simulation, stockage, réseau, audio.
2. **LVGL** + **LovyanGFX** : rendu UI accéléré, double buffering PSRAM.
3. **Gestion SD** : VFS + FatFs, partition `sdcard0`.
4. **Sécurité** : chiffrement AES-256-GCM des sauvegardes, signatures EdDSA.
5. **Mise à jour** : OTA via HTTPS + fallback carte SD (`update.bin`).

## 5. Architecture logicielle
### 5.1 Modules
- `app_core` : initialisation, scheduler, services (event bus, logging).
- `ui` : écrans LVGL, thème, layouts responsive, widgets custom.
- `sim_engine` : calcul environnemental, comportements IA (state machines + ML léger optionnel).
- `data_store` : serialization JSON/LZ4, CRC, rotation, import/export.
- `docs` : lecteur réglementaire (PDF -> rendu bitmap via MuPDF port, HTML/TXT parsing).
- `net` : Wi-Fi provisioning (ESP BLE Mesh + SoftAP), HTTPS client, OTA.
- `audio` : pipeline TTS (ESP-SR), effets sonores.
- `compliance` : disclaimers, contrôles légaux, journaux d'audit.
- `power` : monitoring batterie, modes veille, optimisation.

### 5.2 Threads/tâches (priorités FreeRTOS)
| Tâche | Priorité | Périodicité | Description |
|-------|----------|-------------|-------------|
| `ui_task` | 6 | 16 ms | LVGL tick & render, input GT911.
| `sim_task` | 5 | 100 ms | Physique terrarium, IA reptile.
| `storage_task` | 4 | 1 s | Sauvegardes différées, CRC.
| `doc_task` | 3 | Async | Streaming PDF/HTML.
| `net_task` | 3 | Event-driven | Wi-Fi/OTA.
| `audio_task` | 2 | Event-driven | TTS & sons.
| `power_task` | 2 | 1 s | Battery & thermal.
| `watchdog_task` | 7 | 5 s | Supervision, redémarrage.

## 6. Exigences non fonctionnelles
- **Qualité logicielle** : couverture tests > 80 %, static analysis (clang-tidy, cppcheck), CI GitHub Actions.
- **Sécurité** : chiffrement TLS 1.3, stockage credentials NVS chiffré, partition OTA signée.
- **Maintenance** : documentation Doxygen, manuel utilisateur PDF, guides réglementaires à jour.
- **Évolutivité** : configuration par fichiers YAML sur SD, plug-ins comportementaux en WASM (Wasm3).
- **Localisation** : i18n via fichiers `.po` sur SD.
- **RGPD** : anonymisation, consentement explicite, purge automatique.

## 7. Livrables attendus
1. **Code source complet** ESP-IDF 5.x, LVGL 9.x, LovyanGFX intégré.
2. **CMakeLists**, `sdkconfig.defaults`, `partitions.csv`, scripts build (idf.py).
3. **Ressources SD** : `/saves`, `/docs`, `/assets`, `/updates`.
4. **Documentation** : manuel utilisateur, guide installation, dossier réglementaire, schémas électroniques.
5. **Plans QA** : matrices tests, rapport conformité ISO 25010.
6. **Plan de déploiement** : procédure OTA, packages SD.

## 8. Planning & jalons
| Jalons | Délai | Livrables |
|--------|-------|-----------|
| M0 — Kick-off | Semaine 0 | Cahier des charges validé |
| M1 — Prototype UI | Semaine 4 | Maquettes LVGL, navigation |
| M2 — Moteur simulation | Semaine 8 | Prototype Terrarium, sauvegarde |
| M3 — Intégration documentaire | Semaine 12 | Lecteur PDF/HTML |
| M4 — Sécurité & conformité | Semaine 16 | Audits, disclaimers, RGPD |
| M5 — Pré-série | Semaine 20 | Release candidate, tests QA |
| M6 — Release | Semaine 24 | Livraison, documentation finale |

## 9. Gestion des risques
- **R1 :** Saturation mémoire PSRAM — *Mitigation* : profils LVGL optimisés, streaming textures.
- **R2 :** Corruption carte SD — *Mitigation* : journaling, double sauvegarde, watchdog.
- **R3 :** Obsolescence réglementaire — *Mitigation* : process veille légale trimestrielle.
- **R4 :** Échec OTA — *Mitigation* : fallback SD, partitions OTA_A/B.
- **R5 :** Latence tactile — *Mitigation* : calibration GT911, filtre IIR.

## 10. Annexes
- **Normes** : CITES, I-FAP, Arrêté 08/10/2018, Directive 2009/48/CE, ISO 12100.
- **Outils recommandés** : ESP-IDF 5.1, LVGL 9.1, LovyanGFX 1.1, MuPDF ESP port, ESP-SR 2.x.
- **Tests** : banc climatic virtuel, test endurance 72h, test sauvegarde 100 cycles.

