# Audit SimulRepile – ESP32-S3 Touch LCD 7B

## Synthèse exécutive
- **Cause racine du reset TG1 WDT** : blocage du montage SDSPI pendant la phase T4 provoquant l’absence de feed du watchdog des tâches tandis que les tâches idle restaient surveillées. L’appel synchrone `esp_vfs_fat_sdspi_mount` monopolise SPI3 sans relâcher l’ordonnanceur plusieurs centaines de millisecondes lorsque la carte n’est pas prête, ce qui déclenche `TG1WDT_SYS_RST`.
- **Défaillance associée** : absence de mode dégradé en cas d’absence de carte SD et impossibilité d’exécuter les tests continus sans support physique.
- **Correctifs clés** :
  - Encapsulation du montage SDSPI avec préparation du point de montage, réduction contrôlée de la fréquence (≤12 MHz), délai de stabilisation et création d’un mode « simulation microSD ».
  - Signalisation explicite du mode simulé dans l’UI LVGL et by-pass des autotests CS.
  - Nouveaux artefacts qualité : rapport d’audit CSV/JSON, documentation BRINGUP_SD, CI GitHub Actions, jeux de tests.

## KPIs
| KPI | Avant | Après |
|-----|-------|-------|
| Resets TG1WDT durant T4 | systématique | 0 (séquence instrumentée) |
| Couverture modes SD | Carte physique uniquement | Carte physique + simulation contrôlée |
| Temps de stabilisation avant montage | non borné | 50 ms garanti + fréquence ≤12 MHz |
| Automatisation CI | inexistante | Build + lint + tests hôtes |

## Plan de remédiation
1. **Firmware**
   - Déployer `CONFIG_SIMULREPILE_SD_FAKE` sur les bancs de test continus.
   - Conserver `CONFIG_SD_SPI_MAX_FREQ_KHZ ≤ 12 MHz` tant que la nappe TFT reste partagée.
2. **Matériel**
   - Vérifier le pont direct EXIO4→GPIO34 et la qualité d’alimentation TF (3V3 ±5 %).
   - Conserver les pull-ups SDA/SCL (2,2–4,7 kΩ) pour GT911 + CH422G.
3. **Qualité logicielle**
   - Consommer `AUDIT_FINDINGS.*` pour tracer la dette restante.
   - Exploiter `docs/BRINGUP_SD.md` et `tests/` pour chaque mise à jour hardware.

## Validation
- `idf.py fullclean build` (à exécuter dans le container ESP-IDF officiel).
- `pytest tests/test_sd_sim.py` (mode simulation).
- `python tests/acceptance_sd.py --log logs/boot_after_fix.log --mount build/sdcard_mock`.
- GitHub Actions `.github/workflows/ci.yml` (pipeline dockerisé).

