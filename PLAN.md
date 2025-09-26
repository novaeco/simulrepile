# Plan de traitement des gaps

1. **GAP-001 – Compléter `sim/models.c`** : blocage de build immédiat, condition préalable à toute intégration continue.
2. **GAP-007 – Introduire le Kconfig applicatif/BSP** : sans symboles déclarés, les options `CONFIG_APP_*`/`CONFIG_BSP_*` référencées restent inertes.
3. **GAP-002 – Ajouter `save_manager_list_slots()`/`save_manager_validate_slot()`** : priorité persistance (AGENTS §9) afin de sécuriser les sauvegardes.
4. **GAP-003 – Créer les tests Unity de persistance** : toujours dans le périmètre persistance, garantir CRC et rollback avant d’aller plus loin.
5. **GAP-004 – Implémenter le chargement i18n JSON** : priorité suivante (Accessibilité & i18n) pour activer le multilingue dynamique.
6. **GAP-005 – Finaliser le lecteur de documents** : dépend des précédents (accès fichiers + i18n pour les libellés) et débloque la navigation pédagogique.
7. **GAP-006 – Mettre en place le cache d’assets LRU** : amélioration perf/robustesse après les fonctionnalités utilisateur critiques.
8. **GAP-008 – Implémenter la mise à jour par carte SD** : dernière étape (MAJ & fiabilité) une fois le socle application stabilisé.
