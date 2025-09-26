# Gaps d'implémentation SimulRepile

| Gap ID | Exigence (AGENTS.md) | État constaté | Action minimale proposée |
| --- | --- | --- | --- |
| GAP-001 | Architecture `/firmware/main/sim` complète (§Architecture) | `firmware/main/CMakeLists.txt` référence `sim/models.c` mais le fichier est absent, empêchant la compilation. | Ajouter `sim/models.c` avec les utilitaires de modèles (ex : helpers d’environnement) et l’enregistrer dans CMake. |
| GAP-002 | Persistance : compléter `save_list_slots()`/`save_validate()` (§9) | `save_manager.c/.h` ne fournissent ni énumération des slots ni fonction de validation hors chargement. | Implémenter `save_manager_list_slots()` (scan + métadonnées) et `save_manager_validate_slot()` (lecture en-tête + CRC). |
| GAP-003 | Persistance : tests Unity CRC/rollback/json (§9) | Aucun test unitaire n’existe pour `persist/` ; flux CRC/.bak non couverts. | Ajouter un composant de tests Unity ciblant `save_manager` (CRC, .bak de secours, sérialisation JSON). |
| GAP-004 | I18N : chargement dynamique `/i18n/{fr,en,de,es}.json` (§Accessibilité & i18n) | `i18n_manager.c` ignore le chemin SD, renvoie des chaînes codées en dur sans parsing JSON. | Implémenter chargement JSON (ex : cJSON), cache en mémoire et bascule à chaud via `i18n_manager_set_language`. |
| GAP-005 | Documents : lecteur TXT/HTML minimal (§Fonctionnalités) | `doc_reader.c` renvoie des stubs, ne liste ni ne lit réellement les fichiers SD. | Parcourir les dossiers par catégorie, filtrer extensions, lire contenu TXT/HTML (fallback texte). |
| GAP-006 | Assets : cache LRU + backend SD (§Perf & robustesse) | `asset_cache.c` ne fait que journaliser sans stockage ni LRU. | Introduire un mini-cache (liste chaînée/queue) avec limite configurable, comptage références et lecture sur SD. |
| GAP-007 | Kconfig : symboles APP/BSP (§9) | `sdkconfig.defaults` référence des `CONFIG_APP_*`/`CONFIG_BSP_*` inexistants faute de fichier `Kconfig`. | Ajouter `Kconfig`/sous-Kconfig définissant ces options (valeurs par défaut alignées). |
| GAP-008 | MAJ par SD : parsing `manifest.json` + CRC/rollback (§MAJ & fiabilité) | Aucun module de mise à jour sur SD/OTA n’est présent dans `main/` ou `components/`. | Créer un module `updates` (lecture manifeste, vérification CRC, copie binaire + rollback .bak). |
| GAP-009 | Build : dépendances ESP-IDF résolues (§Build & configuration) | `idf.py build` échoue faute de composant `espressif/cjson` sur le registry. | Aligner `idf_component.yml` sur le composant IDF natif (`idf::cjson`) et verrouiller la version minimale d'ESP-IDF. |
