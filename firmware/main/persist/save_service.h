#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t save_service_init(void);
esp_err_t save_service_set_interval(uint32_t seconds);
esp_err_t save_service_trigger_manual_save(uint32_t slot_mask);
esp_err_t save_service_trigger_manual_load(uint32_t slot_mask);
void save_service_notify_language_changed(void);

#ifdef __cplusplus
}
#endif
