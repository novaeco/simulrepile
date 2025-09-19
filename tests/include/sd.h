#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

typedef struct sdmmc_card_t sdmmc_card_t;

#define SD_MOUNT_POINT "./sdcard"
#define MOUNT_POINT SD_MOUNT_POINT

static inline esp_err_t sd_spi_cs_selftest(void) { return ESP_OK; }

static inline bool sd_is_mounted(void) {
  struct stat st;
  return stat(SD_MOUNT_POINT, &st) == 0;
}

static inline esp_err_t sd_mount(sdmmc_card_t **out_card) {
  (void)out_card;
  mkdir(SD_MOUNT_POINT, 0777);
  FILE *f = fopen(SD_MOUNT_POINT "/selftest.txt", "w");
  if (f) {
    fprintf(f, "OK %ld\n", (long)time(NULL));
    fclose(f);
  }
  return ESP_OK;
}

static inline esp_err_t sd_unmount(void) { return ESP_OK; }

static inline esp_err_t sd_card_print_info_stream(FILE *stream) {
  (void)stream;
  return ESP_OK;
}

static inline esp_err_t sd_card_print_info(void) {
  return sd_card_print_info_stream(stdout);
}

static inline esp_err_t sd_mmc_init(void) { return sd_mount(NULL); }
static inline esp_err_t sd_mmc_unmount(void) { return sd_unmount(); }
