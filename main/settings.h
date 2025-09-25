#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "env_control.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    reptile_env_config_t env_config;
    bool sleep_default;
    esp_log_level_t log_level;
} app_settings_t;

extern app_settings_t g_settings;

esp_err_t settings_init(void);
esp_err_t settings_save(void);
void settings_apply(void);
void settings_screen_show(void);

#ifdef __cplusplus
}
#endif

