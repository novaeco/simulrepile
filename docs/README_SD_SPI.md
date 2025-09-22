# Lien SPI microSD (GPIO34)

## Câblage recommandé

| Signal microSD | ESP32-S3 | Remarques |
|----------------|----------|-----------|
| CS             | GPIO34   | Relier en fil court (<5 cm) depuis l'adaptateur FFC (EXIO4) vers le module ESP32-S3. Ajouter, si possible, une résistance série de 33–47 Ω pour amortir les fronts. |
| CLK            | GPIO12   | Bus SPI2 partagé avec MOSI/MISO (niveau 3V3). |
| MOSI           | GPIO11   | Configuré par `spi_bus_initialize()` avec `SDSPI_HOST_DEFAULT()`. |
| MISO           | GPIO13   | Prévoir une résistance de tirage 100 kΩ vers 3V3 si le lecteur est amovible. |
| VDD            | 3V3      | Alimentation stabilisée (tolérance ±5 %). |
| GND            | GND      | Masse commune. |

> ⚠️ **Important :** ne pas relier la broche CS à l’extenseur CH422G. Toute transaction I²C déclenchée dans une ISR provoquerait des délais d’accès supérieurs à 2 ms et des erreurs `0x107` lors des commandes `CMD13`/`CMD17`.

## Justification technique

- Le firmware utilise `esp_vfs_fat_sdspi_mount()` avec `SDSPI_HOST_DEFAULT()` et une fréquence d’amorçage de 400 kHz.
- Après montage, la fonction `set_card_clk()` pousse le lien SPI à 20 MHz pour les transferts séquentiels.
- Piloter la CS via un GPIO natif garantit :
  - Aucune dépendance au CH422G pendant les ISR `gpio_isr_handler_add` ou dans les tâches temps réel.
  - Des fronts plus rapides et reproductibles (pas de latence I²C ni de risques de `ESP_ERR_TIMEOUT`).
  - La compatibilité avec les cartes SDHC/SDXC nécessitant des timings <100 ns sur la CS.

## Checklist de validation

1. `idf.py fullclean build` → pas de warning bloquant.
2. Boot : présence des logs
   - `CH422G prêt @0x24 (SDA=8 SCL=9)` (permet de vérifier l’extenseur pour les autres fonctions).
   - `SD: bus SPI2 MOSI=11 MISO=13 SCLK=12 CS=34`
   - `SD: carte montée /sdcard (type=SDHC/SDXC, CSD/CID ok)`
3. Absence des erreurs `0x107` ou `CH422G transaction fallback…` dans `idf.py monitor`.
4. Fichier `/sdcard/selftest.txt` créé avec le timestamp courant.

## Notes de mise à jour

- `CONFIG_SD_USE_FALLBACK_GPIO_CS=y` dans `sdkconfig.defaults` force le pilotage direct.
- La documentation `docs/troubleshooting/ch422g.md` reflète désormais le fait que la CS n’est plus pilotée par l’extenseur.
- Pour les cartes custom, adapter `CONFIG_SD_FALLBACK_CS_GPIO` et recompiler.
