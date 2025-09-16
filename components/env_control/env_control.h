#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int temp_setpoint;     // Desired temperature in °C
    int humidity_setpoint; // Desired humidity in %
} reptile_env_thresholds_t;

typedef struct {
    float temperature; // Last measured temperature
    float humidity;    // Last measured humidity
    bool heating;      // Heating actuator active
    bool pumping;      // Pump actuator active
} reptile_env_state_t;

typedef void (*reptile_env_update_cb_t)(const reptile_env_state_t *state, void *user_ctx);

esp_err_t reptile_env_start(const reptile_env_thresholds_t *thr,
                            reptile_env_update_cb_t cb,
                            void *user_ctx);
void reptile_env_stop(void);
void reptile_env_set_thresholds(const reptile_env_thresholds_t *thr);
void reptile_env_get_state(reptile_env_state_t *out);

void reptile_env_manual_pump(void);
void reptile_env_manual_heat(void);

#ifdef __cplusplus
}
#endif

