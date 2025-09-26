# Dossier de conception initiale — Projet « SimulRepile »

## 1. Objectifs techniques consolidés
- Traduire le cahier des charges v1.0.0 en architecture matérielle et logicielle exécutable sur carte Waveshare ESP32-S3-Touch-LCD-7B.
- Définir les modules, APIs et schémas de données assurant la simulation multi-terrariums et la conformité réglementaire.
- Préparer les livrables buildables (ESP-IDF 5.1) : arborescence, CMakeLists, sdkconfig.defaults, partitions, tests automatisés.
- Garantir l'extensibilité (plugins WASM, packs de documentation) et la maintenabilité (CI/CD, coverage >80 %).

## 2. Architecture système globale
### 2.1 Vue couches
```
+----------------------------------------------------------------------------------+
| Application UX (LVGL 9.x)                                                        |
|  - Scènes terrarium, planning alimentaire, documentation, conformité             |
|  - Widgets custom (chart climat, timeline soins, viewer PDF)                     |
+------------------------------------↑---------------------------------------------+
| Services applicatifs (app_core)             |  Sécurité & conformité              |
|  - Event bus (publisher/subscriber)         |  - Audit RGPD, disclaimers          |
|  - Scheduler scénarios simulation           |  - Gestion ACL contenus sensibles   |
+------------------------------------↑---------------------------------------------+
| Moteurs métiers (sim_engine, data_store, docs, net, audio, power)                |
|  - Simulation physique + IA comportementale                                      |
|  - Persistence JSON+LZ4, CRC32, chiffrement AES-GCM                              |
|  - Lecteur documentaire MuPDF/HTML                                              |
|  - OTA TLS1.3 + packages SD signés                                              |
+------------------------------------↑---------------------------------------------+
| HAL ESP-IDF (drivers, FreeRTOS SMP, LovyanGFX, ESP-SR)                           |
+------------------------------------↑---------------------------------------------+
| Matériel : ESP32-S3, écran RGB, GT911, microSD, audio I²S, capteurs virtuels     |
+----------------------------------------------------------------------------------+
```

### 2.2 Bus de communication interne
- **Event Bus** (`app_core/event_bus.h`) : file d’évènements lock-free (port queue FreeRTOS) avec topics hiérarchiques (`sim/*`, `ui/*`, `storage/*`).
- **MessagePack interne** pour sérialiser les payloads d’évènements (structure compacte, mapping direct vers JSON export).
- **Shared Memory** PSRAM : double buffer LVGL (2× 1024×600×16 bits), ring buffers audio 16 kHz 16-bit.

## 3. Arborescence logicielle proposée
```
simulrepile/
├── firmware/
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   ├── main/
│   │   ├── app_core/
│   │   │   ├── app_core.c/.h
│   │   │   ├── event_bus.c/.h
│   │   │   └── scheduler.c/.h
│   │   ├── ui/
│   │   │   ├── screens/
│   │   │   ├── theme/
│   │   │   ├── widgets/
│   │   │   └── ui_controller.c/.h
│   │   ├── sim_engine/
│   │   │   ├── climate_model.c/.h
│   │   │   ├── species_profile.c/.h
│   │   │   └── feeding_cycle.c/.h
│   │   ├── data_store/
│   │   │   ├── storage_manager.c/.h
│   │   │   ├── backup_rotation.c/.h
│   │   │   └── json_lz4_codec.c/.h
│   │   ├── docs/
│   │   │   ├── document_index.c/.h
│   │   │   └── viewer_controller.c/.h
│   │   ├── net/
│   │   │   ├── provisioner.c/.h
│   │   │   ├── ota_manager.c/.h
│   │   │   └── tls_client.c/.h
│   │   ├── audio/
│   │   │   ├── tts_pipeline.c/.h
│   │   │   └── sound_effects.c/.h
│   │   ├── compliance/
│   │   │   ├── legal_disclaimer.c/.h
│   │   │   ├── audit_logger.c/.h
│   │   │   └── parental_control.c/.h
│   │   ├── power/
│   │   │   ├── battery_monitor.c/.h
│   │   │   └── power_manager.c/.h
│   │   └── main.c
│   └── tests/
│       ├── host/
│       │   ├── CMakeLists.txt
│       │   └── test_sim_engine.cpp
│       └── target/
│           ├── unity/
│           └── integration/
├── assets/
│   ├── ui/
│   └── audio/
└── docs/
    ├── cahier_des_charges.md
    ├── conception_systeme.md
    └── architecture_logicielle.drawio (à créer)
```

## 4. Modèle de données principal
### 4.1 Schéma JSON `terrarium_state`
```json
{
  "id": "terrarium-1",
  "species_profile": "python_regius",
  "environment": {
    "temperature": {"current": 29.5, "target_day": 31.0, "target_night": 26.0},
    "humidity": {"current": 65, "target": 70},
    "uv_index": {"current": 3.2, "target": 3.5},
    "photoperiod": {"day_hours": 12, "next_switch": "2025-09-27T08:00:00Z"}
  },
  "automation": {
    "heating": {"state": "ON", "duty": 0.42},
    "misting": {"state": "OFF", "next_cycle": "2025-09-27T13:00:00Z"},
    "ventilation": {"state": "ON", "rpm": 1200},
    "uvb": {"state": "ON", "timer_remaining": 7200}
  },
  "feeding_plan": {
    "next_feeding": "2025-09-28",
    "prey_type": "rat_weaned",
    "quantity": 1,
    "stock_warning": false
  },
  "health": {
    "weight_g": 950,
    "last_shed": "2025-09-10",
    "clinical_flags": ["reduced_appetite"],
    "veterinary_alert": false
  },
  "logs": {
    "last_cleaning": "2025-09-20",
    "notes": "Enrichissement ajouté"
  },
  "compliance": {
    "documents_ack": ["cites_appendix_II", "arrete_20181008"],
    "minor_access": false
  }
}
```
- Serialisation via **cJSON** + compression **LZ4** (débit visé : <10 ms par terrarium sur PSRAM).
- Sauvegarde en `saves/<terrarium_id>/state_%Y%m%d_%H%M%S.json.lz4` avec CRC32 en suffixe (`.crc`).

### 4.2 Index documentaire (`docs_index.json`)
- Structure B-tree triée par domaines (`legislation`, `species`, `guides`).
- Entrées comportant `path`, `hash_ed25519`, `locale`, `age_rating`, `summary`.
- Réside sur microSD, mis à jour via packages `.srpkg` (ZIP + manifeste signé).

## 5. Simulation environnementale
### 5.1 Modèle climatique
- **Équations thermiques** : schéma Euler explicite avec différentiel `dT = (Q_heating - Q_loss)/C`, `Q_loss = k*(T_internal - T_external)`.
- **Hygrométrie** : modèle d’évaporation proportionnel à la surface humide virtuelle + injection brumisation.
- **Photopériode** : machine d’état `DAY`, `TRANSITION`, `NIGHT` synchronisée RTC.
- **Ventilation** : PID (PI) contrôlant humidité et CO₂ (valeur virtuelle).

### 5.2 Comportement animal
- Automates à états finis par espèce (repos, chasse, mue, reproduction) avec transitions paramétrées.
- Module ML optionnel : inférence TinyML (TensorFlow Lite Micro) pour prédire appétit en fonction historiques.

## 6. Interface utilisateur LVGL
- **Layout principal** : dashboard 4 cartes terrarium (widgets gauges, sparkline température).
- **Navigation** : barre latérale persistante (Simulations, Alimentation, Santé, Documentation, Paramètres).
- **Accessibilité** : 3 thèmes (standard, contraste élevé, daltonien) sélectionnés via menu `Paramètres > Accessibilité`.
- **Input** : driver GT911 en mode interrupt, gestuelles mappées sur LVGL (`lv_indev_gesture_dir_t`).
- **Synthèse vocale** : icône TTS sur chaque écran, pipeline audio `tts_pipeline.c` -> I²S 16 kHz.

## 7. Sécurité & conformité
- **Chiffrement sauvegardes** : AES-256-GCM (clé stockée en NVS chiffré). Tag 16 octets, nonce 96 bits dérivé RNG.
- **Signatures packages** : EdDSA25519, verification via libsodium (port ESP32).
- **RGPD** : menu consentement, purge logs > 365 jours, anonymisation via hash salted.
- **Contrôle parental** : code PIN 6 digits, ACL sur documents `age_rating >= 16`.
- **Audit** : `audit_logger` écrit en NVS partition `audit` en format CBOR.

## 8. Stratégie d'alimentation et gestion d'énergie
- **Modes** : `ACTIVE`, `IDLE`, `SLEEP`, `DEEP_SLEEP`. Transition orchestrée par `power_manager` selon inactivité UI et SoC.
- **Monitoring batterie** : CS8501 -> ADC1 ch3, filtre Kalman pour estimation SOC.
- **Optimisation** : rétro-éclairage PWM adaptatif, underclock 80 MHz en veille, désactivation Wi-Fi en mode local.

## 9. Plan de tests et QA
- **Tests unitaires** : Unity (target) + GoogleTest (host) pour modèles de simulation.
- **Tests intégration** : banc automatisé (pytest + idf.py) vérifiant cycles sauvegarde, OTA fictif, TTS offline.
- **Tests UX** : capture LVGL (headless) via `lv_port_indev` + scripts Python pour snapshots.
- **Couverture** : `idf.py test` + `gcovr` -> objectif 80 %.
- **Static analysis** : clang-tidy, cppcheck, ESP-IDF `idf.py analyze`. GitHub Actions multi-stage.

## 10. Feuille de route technique (24 semaines)
| Semaine | Livrables techniques | Jalons |
|---------|----------------------|--------|
| 0-2 | Mise en place repo, CI, arborescence firmware, drivers basiques. | M0 |
| 3-4 | Prototypes LVGL (dashboard, navigation), mock simulation. | M1 |
| 5-8 | Implémentation `sim_engine`, `data_store`, tests host. | M2 |
| 9-12 | Lecteur documentaire MuPDF, import packages SD, TTS baseline. | M3 |
| 13-16 | Sécurité (chiffrement, signatures), RGPD, parental control. | M4 |
| 17-20 | Optimisation performance, endurance tests, packaging OTA. | M5 |
| 21-24 | Documentation finale, pré-série, audit conformité. | M6 |

## 11. Risques & mitigations techniques
| Risque | Impact | Probabilité | Mitigation |
|--------|--------|-------------|------------|
| Saturation PSRAM LVGL | Crash UI | Moyen | Double buffering adaptatif (1024×600×16 bits), compression assets, `LV_MEM_SIZE` calibré. |
| Corruption SD | Perte données | Moyen | Journaling, double sauvegarde, watchdog + auto-réparation (fsck). |
| Latence TTS | UX dégradée | Faible | Pré-génération prompts critiques, pipeline DMA double buffer. |
| OTA échoue | Blocage update | Faible | Partition OTA_A/B, fallback SD, CRC + signature. |
| Obsolescence docs | Non conformité | Moyen | Process mise à jour trimestriel, packages signés. |

## 12. Prochaines étapes
1. Initialiser projet ESP-IDF (`idf.py create-project`) dans `firmware/` selon arborescence.
2. Rédiger `sdkconfig.defaults` (activations PSRAM, LVGL, ESP-SR, FatFs LFN, TLS 1.3).
3. Définir interface `event_bus` et squelette tâches FreeRTOS.
4. Prototyper UI dashboard avec données simulées.
5. Mettre en place pipeline CI (GitHub Actions) pour build + tests host.

