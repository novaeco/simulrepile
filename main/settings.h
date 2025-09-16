#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t temp_threshold;     // Temperature threshold in °C
    int32_t humidity_threshold; // Humidity threshold in %
    bool    sleep_default;      // Enable sleep by default
    esp_log_level_t log_level;  // Logging verbosity
} app_settings_t;

extern app_settings_t g_settings;

esp_err_t settings_init(void);
esp_err_t settings_save(void);
void settings_apply(void);
void settings_screen_show(void);

#ifdef __cplusplus
}
#endif

