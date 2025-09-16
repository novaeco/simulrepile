#pragma once

#include "esp_err.h"
#include <sys/stat.h>

#define MOUNT_POINT "./sdcard"

static inline esp_err_t sd_mmc_init(void) {
  mkdir(MOUNT_POINT, 0777);
  return ESP_OK;
}

static inline esp_err_t sd_mmc_unmount(void) { return ESP_OK; }

static inline esp_err_t sd_card_print_info(void) { return ESP_OK; }

static inline esp_err_t read_sd_capacity(size_t *total_capacity,
                                         size_t *available_capacity) {
  if (total_capacity)
    *total_capacity = 0;
  if (available_capacity)
    *available_capacity = 0;
  return ESP_OK;
}
