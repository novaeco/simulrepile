# SimulRepile Core Firmware (ESP32-S3-DevKitC-1)

Ce projet ESP-IDF cible l'ESP32-S3-DevKitC-1 (module ESP32-S3-WROOM-2-N32R16V). Il implémente le "cœur"
maître de l'architecture SimulRepile option B :

- Génération de l'état simulé des terrariums (jusqu'à 4) via `state/core_state_manager.*`.
- Publication périodique et sur demande des instantanés vers la carte Waveshare via le protocole UART
  `core_link` partagé (`common/include/link/core_link_protocol.h`). Lorsque l’afficheur annonce une version de protocole ≥ 1,
  le DevKitC encode et transmet uniquement les champs modifiés (`STATE_DELTA`). Sinon il se rabat automatiquement sur des trames
  complètes.
- Gestion des événements tactiles remontés par la Waveshare afin d'ajuster la simulation.

## Compilation

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash & moniteur

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## Paramètres clé (menuconfig)

`SimulRepile Core Configuration` expose :

- Ports/broches UART (`UART port`, `TX pin`, `RX pin`, `Baud rate`).
- Delai de handshake et intervalle d'émission.
- Epoch de référence des timestamps.
- Chemins des profils terrarium (SD principal + fallback SPIFFS).

Les valeurs par défaut correspondent à un câblage croisé direct UART1 entre DevKitC et Waveshare :

| DevKitC (GPIO) | Waveshare (GPIO) | Fonction |
| --- | --- | --- |
| 17 | 44 | TX -> RX |
| 18 | 43 | RX <- TX |
| GND | GND | Masse commune |

## Dépendances

Le projet s'appuie sur ESP-IDF ≥ 5.5. Aucun composant externe n'est nécessaire ; les structures de
protocole partagées se trouvent dans `../firmware/common/include/`.

## Profils terrarium (JSON)

Le cœur charge automatiquement jusqu'à quatre profils en recherchant des fichiers `*.json`
dans le répertoire configuré (`/sdcard/profiles` par défaut, `/spiffs/profiles` en secours).
Chaque fichier doit contenir un objet JSON avec au minimum les clés `scientific_name`,
`common_name` et `environment`.

Exemple de profil :

```json
{
  "id": 0,
  "scientific_name": "Python regius",
  "common_name": "Python royal",
  "environment": {
    "temp_day_c": 31.0,
    "temp_night_c": 24.0,
    "humidity_day_pct": 60.0,
    "humidity_night_pct": 70.0,
    "lux_day": 400.0,
    "lux_night": 5.0
  },
  "cycle_speed": 0.03,
  "phase_offset": 0.0,
  "enrichment_factor": 1.0,
  "metrics": {
    "hydration_pct": 88.0,
    "stress_pct": 15.0,
    "health_pct": 94.0,
    "activity_score": 0.5,
    "feeding": {
      "last_timestamp": 1704074400,
      "interval_hours": 72,
      "intake_pct": 80.0
    }
  }
}
```

Les champs non renseignés sont complétés avec des valeurs cohérentes (cycle circadien,
hydratation/stress/santé initiaux, fréquence de nourrissage). Les fichiers sont triés
alphabétiquement avant chargement pour garantir un ordre déterministe. Les anciennes clés
au niveau racine (`hydration_pct`, `stress_pct`, `health_pct`, `activity_score`,
`last_feeding_timestamp`) restent acceptées pour la rétrocompatibilité.

Résumé des principales clés :

| Champ | Type | Obligatoire | Description |
| --- | --- | --- | --- |
| `scientific_name` | Chaîne | Oui | Nom scientifique affiché dans l'UI et transmis au pont Core Link. |
| `common_name` | Chaîne | Oui | Nom commun lisible par l'utilisateur. |
| `environment.temp_day_c` | Nombre | Oui | Température diurne de référence (°C). |
| `environment.temp_night_c` | Nombre | Oui | Température nocturne de référence (°C). |
| `environment.humidity_day_pct` | Nombre | Oui | Humidité relative diurne (%). |
| `environment.humidity_night_pct` | Nombre | Oui | Humidité relative nocturne (%). |
| `environment.lux_day` | Nombre | Oui | Niveau lumineux diurne (lux). |
| `environment.lux_night` | Nombre | Oui | Niveau lumineux nocturne (lux). |
| `cycle_speed` | Nombre | Non | Vitesse de cycle circadien (rad/s) ; défaut selon le slot. |
| `phase_offset` | Nombre | Non | Décalage de phase appliqué aux oscillations. |
| `enrichment_factor` | Nombre | Non | Influence de l'enrichissement sur le stress/activité. |
| `metrics.hydration_pct` | Nombre | Non | Hydratation initiale forcée (%). |
| `metrics.stress_pct` | Nombre | Non | Stress initial forcé (%). |
| `metrics.health_pct` | Nombre | Non | Santé initiale forcée (%). |
| `metrics.activity_score` | Nombre | Non | Score d'activité initial (0.0 – 1.0). |
| `metrics.feeding.interval_hours` | Nombre | Non | Intervalle de nourrissage simulé (heures). |
| `metrics.feeding.intake_pct` | Nombre | Non | Intensité du repas (impact hydratation/stress). |
| `metrics.feeding.last_timestamp` | Nombre | Non | Timestamp Unix du dernier repas enregistré. |

Les profils peuvent être rechargés à chaud soit en invoquant directement
`core_state_manager_reload_profiles("/nouveau/chemin")` côté DevKitC, soit via la commande
UART `CORE_LINK_CMD_RELOAD_PROFILES` (exposée côté afficheur par
`core_link_request_profile_reload()`).
