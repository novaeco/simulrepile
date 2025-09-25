# Procédure de bring-up microSD – SimulRepile ESP32-S3 Touch LCD 7B

## 1. Pré-requis matériels
- Carte **Waveshare ESP32-S3 Touch LCD 7B** (LCD 1024×600, GT911, CH422G).
- Pont direct **EXIO4 → GPIO34** (fil émaillé <5 cm) pour la ligne CS microSD.
- Pull-ups I²C 2,2–4,7 kΩ vers 3V3 sur SDA=GPIO8 / SCL=GPIO9.
- Alimentation 5 V / 2 A minimum, oscillogramme conseillé pour vérifier la stabilité 3V3 lors de l’insertion TF.

## 2. Configuration logicielle
1. Installer **ESP-IDF v5.5** (`espressif/idf:release-v5.5` ou environnement local).
2. Positionner les options `menuconfig` :
   - `Component config → Storage / SD card → Drive SD card CS from an ESP32-S3 GPIO` = **Enable**.
   - `Component config → Storage / SD card → GPIO number controlling the SD card CS` = **34**.
   - `Component config → Storage / SD card → SD SPI max frequency` ≤ **12000 kHz**.
   - `Project config → SIMULREPILE_SD_FAKE` = **Enable** uniquement pour les tests sans carte.
3. Vérifier `sdkconfig.defaults` (déjà paramétré dans ce dépôt).

## 3. Séquence d’initialisation attendue (`idf.py monitor`)
```
T1 LCD init start … done
T2 GT911 init start … done
T3 CH422G init start … done
T4 SD init start
sd: Tentative 1: fréquence SDSPI 12000 kHz (point de montage /sdcard)
sd: Carte détectée: SDHC/SDXC
main: T4 SD init done (mounted=1)
```

Aucun reset `TG1WDT_SYS_RST` ne doit apparaître.

## 4. Mode simulation microSD
- Activer `CONFIG_SIMULREPILE_SD_FAKE` pour les bancs CI ou les démonstrations sans carte physique.
- Le firmware crée alors `build/sdcard_mock/selftest.txt` contenant `OK SIMULATED <timestamp>`.
- L’UI LVGL affiche « microSD simulée » (fond turquoise) et les autotests CS sont ignorés.

## 5. Tests de validation
| Étape | Commande | Critère |
|-------|----------|---------|
| Compilation | `idf.py fullclean build` | Build réussi, aucun warning critique |
| Simulation | `pytest tests/test_sd_sim.py` | Résultat PASS, création du sentinel |
| Analyse logs | `python tests/acceptance_sd.py --log logs/boot_after_fix.log --mount build/sdcard_mock` | Pas de timeout, présence `selftest.txt` |

## 6. Dépannage rapide
| Symptôme | Diagnostic | Correctif |
|----------|-----------|-----------|
| `GPIO34 ne répond pas` | Pont EXIO4→GPIO34 rompu | Ressouder, vérifier continuité | 
| `sdmmc_card_init failed (0x107)` | CS pilotée via CH422G ou carte absente | Vérifier pont direct, insertion carte | 
| `TG1WDT_SYS_RST` | Montage SDSPI bloqué >5 s | Vérifier alimentation et logs WDT, utiliser mode simulation pour CI |

## 7. Rétro-documentation
- `docs/troubleshooting/ch422g.md` : diagnostic bus I²C.
- `AUDIT_SUMMARY.md` : synthèse corrective.

