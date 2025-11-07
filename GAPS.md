# Gaps d'implémentation SimulRepile

| Gap ID | Exigence (AGENTS.md) | État constaté | Action minimale proposée |
| --- | --- | --- | --- |
| GAP-016 | Tests persistance (§9 Tests) | Les nouveaux modules `persist/save_service.*` et les flux de restauration (`sim_engine_export/restore`) ne disposent d'aucun test Unity couvrant autosave, files d'attente et parsing JSON. | Ajouter une batterie de tests Unity (mocks LVGL/tts) validant sauvegarde automatique, restauration multi-slots et gestion d'erreurs JSON/CRC. |
| GAP-017 | Accessibilité audio (§Accessibilité & i18n) | Le stub TTS loggue correctement, mais aucun pipeline audio réel n'est branché (pas de sortie I2S/USB audio, pas de configuration menuconfig). | Introduire une implémentation TTS optionnelle (par ex. PicoTTS ou synthèse embarquée) et exposer les options audio dans Kconfig/UI en conservant le stub comme fallback. |
| GAP-018 | Vérification système complète (§Perf & robustesse) | Aucun scénario d'intégration automatisé ne valide l'enchaînement Core Link → autosave → rechargement sur cible réelle (ni mesure de latence/bande passante). | Écrire un test matériel ou un banc d'intégration (pytest + ESPTool) qui déclenche sauvegarde/restauration via UART et vérifie les journaux + CRC sur carte SD. |
