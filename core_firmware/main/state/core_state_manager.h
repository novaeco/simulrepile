#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "link/core_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Charge les profils terrarium et initialise l'état courant.
 *
 * Chaque profil est décrit par un fichier JSON individuel dans le répertoire
 * configuré (par défaut `/sdcard/profiles`). Le schéma attendu est le suivant :
 *
 * ```json
 * {
 *   "id": 0,
 *   "scientific_name": "Python regius",
 *   "common_name": "Python royal",
 *   "environment": {
 *     "temp_day_c": 31.0,
 *     "temp_night_c": 24.0,
 *     "humidity_day_pct": 60.0,
 *     "humidity_night_pct": 70.0,
 *     "lux_day": 400.0,
 *     "lux_night": 5.0
 *   },
 *   "cycle_speed": 0.03,
 *   "phase_offset": 0.0,
 *   "enrichment_factor": 1.0,
 *   "metrics": {
 *     "hydration_pct": 88.0,
 *     "stress_pct": 15.0,
 *     "health_pct": 94.0,
 *     "activity_score": 0.5,
 *     "feeding": {
 *       "last_timestamp": 1704074400,
 *       "interval_hours": 72,
 *       "intake_pct": 80.0
 *     }
 *   }
 * }
 * ```
 *
 * Les champs `scientific_name`, `common_name` et le bloc `environment` sont
 * obligatoires. Les métriques (`metrics`) sont optionnelles : lorsqu'elles
 * sont absentes, l'algorithme applique des valeurs plausibles (hydratation,
 * stress, santé, alimentation). Les anciennes clés au niveau racine
 * (`hydration_pct`, `stress_pct`, `health_pct`, `activity_score`,
 * `last_feeding_timestamp`) restent prises en charge pour assurer la
 * rétrocompatibilité.
 *
 * Clés optionnelles supplémentaires :
 * - `feeding_interval_hours` (racine ou `metrics.feeding.interval_hours`) pour
 *   ajuster la fréquence de nourrissage simulée.
 * - `feeding_intake_pct` (racine ou `metrics.feeding.intake_pct`) pour
 *   calibrer l'impact des repas sur l'hydratation.
 * - `metrics.hydration_pct`, `metrics.stress_pct`, `metrics.health_pct` et
 *   `metrics.activity_score` afin d'imposer des valeurs de départ précises.
 *
 * @param base_path Chemin racine à utiliser pour une recharge à chaud. Passer
 *                  `NULL` pour utiliser le chemin courant (menuconfig).
 * @return ESP_OK si des profils ont été chargés depuis un FS, ESP_ERR_NOT_FOUND
 *         lorsque les profils intégrés sont utilisés, ou un code d'erreur IDF.
 */
esp_err_t core_state_manager_reload_profiles(const char *base_path);

void core_state_manager_init(void);
void core_state_manager_update(float delta_seconds);
void core_state_manager_apply_touch(const core_link_touch_event_t *event);
void core_state_manager_build_frame(core_link_state_frame_t *frame);
size_t core_state_manager_get_terrarium_count(void);

#ifdef __cplusplus
}
#endif
