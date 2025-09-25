# Migration Guide 1.0 → 1.1

## Nouveaux symboles Kconfig
- `CONFIG_SIMULREPILE_SD_FAKE` (défaut: OFF)
  - Active le mode simulation microSD. À activer uniquement sur bancs CI.
  - Met à jour l’UI LVGL et court-circuite l’autotest CS.

## Actions requises
1. **Vérifier le câblage** : s’assurer que la ligne CS microSD est reliée à `GPIO34`. Le firmware n’utilise plus le CH422G pour cette ligne.
2. **Reconfigurer la fréquence SDSPI** : limiter `CONFIG_SD_SPI_MAX_FREQ_KHZ` à ≤12 MHz et `CONFIG_SD_SPI_RETRY_FREQ_KHZ` à ≤10 MHz si nécessaire.
3. **Mettre à jour la CI** : intégrer le workflow `.github/workflows/ci.yml` et définir la variable `SIMULREPILE_SD_FAKE=y` pour les builds sans carte.
4. **Tests de non-régression** : exécuter `idf.py fullclean build` et `pytest tests/test_sd_sim.py` avant livraison.

## Compatibilité
- Aucun changement sur les API publiques LVGL/reptile.
- Les sauvegardes microSD restent compatibles ; en mode simulation, les fichiers sont générés dans le répertoire hôte `build/sdcard_mock`.

