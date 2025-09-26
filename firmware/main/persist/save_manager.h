#pragma once

#include "app_config.h"
#include "sim/sim_models.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_SLOT_COUNT APP_MAX_TERRARIUMS

typedef struct {
    uint8_t compression_enabled;
    uint8_t reserved[3];
    uint32_t crc32;
} save_header_t;

void save_manager_init(void);
int save_manager_save_slot(size_t slot, const sim_terrarium_state_t *state);
int save_manager_load_slot(size_t slot, sim_terrarium_state_t *out_state);
int save_manager_rollback(size_t slot);
int save_manager_internal_crc_validate(const uint8_t *data, size_t len, uint32_t expected);

#ifdef __cplusplus
}
#endif
