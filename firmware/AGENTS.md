Règles de contribution (firmware)
================================

* Respecter l'organisation des dossiers détaillée dans AGENTS.md racine.
* Tout nouveau module doit disposer d'un en-tête dans `include/` ou le sous-dossier concerné.
* Utiliser ESP-IDF ≥ 5.5, LVGL 9, LovyanGFX. Les fichiers ici ne sont que des squelettes : conserver les stubs si les implémentations complètes ne sont pas encore disponibles.
* Préférer `esp_err_t` pour les codes de retour et journaliser via `ESP_LOGx`.
* Les fichiers `.c` doivent inclure leur `.h` associé en premier.
* Les structures de données exposées dans `/sim` et `/persist` doivent rester compatibles avec les formats de sauvegarde décrits.
