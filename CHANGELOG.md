# Changelog

## [1.1.0] - 2024-06-XX
### Added
- Option `CONFIG_SIMULREPILE_SD_FAKE` pour simuler la microSD lors des tests.
- Documentation `docs/BRINGUP_SD.md` détaillant le bring-up SD.
- Rapports d’audit `AUDIT_SUMMARY.md`, `AUDIT_FINDINGS.*`.
- Pipeline CI `.github/workflows/ci.yml` (build + lint + tests hôtes).

### Changed
- Initialisation SDSPI : fréquence limitée à 12 MHz, délai de stabilisation 50 ms, création automatique du point de montage.
- UI LVGL : affichage explicite du statut « microSD simulée » et bypass de l’autotest CS en mode simulation.

### Fixed
- Réinitialisations `TG1WDT_SYS_RST` lors de la phase T4 (sd_mount) grâce au séquencement SDSPI sécurisé.
