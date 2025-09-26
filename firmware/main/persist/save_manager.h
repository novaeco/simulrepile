#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t crc32;
    uint32_t payload_length;
    char reserved[32];
} save_metadata_t;

typedef struct {
    save_metadata_t meta;
    uint8_t *payload;
} save_slot_t;

esp_err_t save_manager_init(const char *root_path);
esp_err_t save_manager_load_slot(int slot_index, save_slot_t *out_slot);
esp_err_t save_manager_save_slot(int slot_index, const save_slot_t *slot_data, bool make_backup);
void save_manager_free_slot(save_slot_t *slot);

#ifdef __cplusplus
}
#endif
