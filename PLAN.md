# Plan de traitement des gaps

1. **GAP-016 – Couverture Unity du service de sauvegarde** : écrire des tests ciblant `save_service` et `sim_engine_restore` pour garantir l'intégrité JSON/CRC avant d'aller plus loin.
2. **GAP-018 – Banc d'intégration Core Link ↔ Autosave** : mettre en place le banc matériel/pytest afin de valider l'enchaînement complet sur cible et collecter les métriques de latence.
3. **GAP-017 – Implémentation audio TTS réelle** : une fois la validation logicielle terminée, intégrer une pile TTS matérielle/logicielle optionnelle pour satisfaire totalement l'exigence accessibilité.
