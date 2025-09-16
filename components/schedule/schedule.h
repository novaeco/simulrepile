#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCHEDULE_SLOTS_PER_ACTUATOR 2

typedef enum {
    SCHEDULE_ACTUATOR_HEATING = 0,
    SCHEDULE_ACTUATOR_UV,
    SCHEDULE_ACTUATOR_LIGHTING,
    SCHEDULE_ACTUATOR_VENTILATION,
    SCHEDULE_ACTUATOR_COUNT
} schedule_actuator_t;

typedef struct {
    bool enabled;
    uint16_t start_minute; /**< Minute de départ dans la journée (0-1439) */
    uint16_t end_minute;   /**< Minute de fin dans la journée (0-1439) */
} schedule_slot_t;

typedef struct {
    schedule_slot_t heating[SCHEDULE_SLOTS_PER_ACTUATOR];
    schedule_slot_t uv[SCHEDULE_SLOTS_PER_ACTUATOR];
    schedule_slot_t lighting[SCHEDULE_SLOTS_PER_ACTUATOR];
    schedule_slot_t ventilation[SCHEDULE_SLOTS_PER_ACTUATOR];
} schedule_config_t;

typedef struct {
    bool heating;
    bool uv;
    bool lighting;
    bool ventilation;
} schedule_state_t;

/**
 * @brief Initialiser le planificateur (chargement NVS, valeurs par défaut).
 */
esp_err_t schedule_init(void);

/**
 * @brief Récupérer la configuration courante.
 */
void schedule_get_config(schedule_config_t *cfg);

/**
 * @brief Mettre à jour la configuration et la persister.
 */
esp_err_t schedule_set_config(const schedule_config_t *cfg);

/**
 * @brief Calculer l'état à une minute donnée dans la journée.
 *
 * @param minute_of_day Minute (0-1439)
 * @param out           Structure résultat
 * @return true si la sortie est valide, false sinon
 */
bool schedule_get_state_for_minute(uint16_t minute_of_day, schedule_state_t *out);

/**
 * @brief Calculer l'état pour l'heure courante du système.
 */
bool schedule_get_current_state(schedule_state_t *out);

#ifdef __cplusplus
}
#endif

