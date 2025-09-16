#ifndef TERRARIUM_MANAGER_H
#define TERRARIUM_MANAGER_H

#include "esp_err.h"
#include "lvgl.h"
#include "reptile_logic.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TERRARIUM_MANAGER_MAX_TERRARIUMS 25
#define TERRARIUM_NAME_MAX_LEN 32

typedef enum {
  TERRARIUM_SUBSTRATE_SABLE = 0,
  TERRARIUM_SUBSTRATE_TROPICAL,
  TERRARIUM_SUBSTRATE_ROCHE,
  TERRARIUM_SUBSTRATE_MAX,
} terrarium_substrate_t;

typedef enum {
  TERRARIUM_DECOR_LIANES = 0,
  TERRARIUM_DECOR_ROCHERS,
  TERRARIUM_DECOR_CAVERNE,
  TERRARIUM_DECOR_MAX,
} terrarium_decor_t;

typedef struct {
  uint16_t length_cm;
  uint16_t width_cm;
  uint16_t height_cm;
} terrarium_dimensions_t;

typedef struct {
  char name[TERRARIUM_NAME_MAX_LEN];
  terrarium_substrate_t substrate;
  terrarium_decor_t decor;
  char reptile_slot[REPTILE_SLOT_NAME_MAX];
  terrarium_dimensions_t dimensions;
  char species_id[REPTILE_SPECIES_ID_MAX_LEN];
} terrarium_config_t;

typedef struct {
  uint8_t id;
  bool state_loaded;
  terrarium_config_t config;
  reptile_t reptile;
  uint32_t last_tick_ms;
  uint32_t update_ms_accum;
  uint32_t soothe_ms_accum;
  uint32_t soothe_time_ms;
  const species_db_entry_t *species_profile;
} terrarium_t;

esp_err_t terrarium_manager_init(bool simulation);
bool terrarium_manager_is_initialized(void);
size_t terrarium_manager_count(void);
terrarium_t *terrarium_manager_get(size_t index);
const terrarium_t *terrarium_manager_peek(size_t index);
esp_err_t terrarium_manager_select(size_t index);
terrarium_t *terrarium_manager_get_active(void);
size_t terrarium_manager_get_active_index(void);
esp_err_t terrarium_manager_set_slot(terrarium_t *terrarium, const char *slot_name);
esp_err_t terrarium_manager_reset_state(terrarium_t *terrarium);
esp_err_t terrarium_manager_save_config(const terrarium_t *terrarium);
esp_err_t terrarium_manager_reload_config(terrarium_t *terrarium);
esp_err_t terrarium_manager_set_species(terrarium_t *terrarium,
                                        const species_db_entry_t *species);
const species_db_entry_t *
terrarium_manager_get_species(const terrarium_t *terrarium);

const lv_image_dsc_t *terrarium_manager_get_substrate_icon(terrarium_substrate_t substrate);
const lv_image_dsc_t *terrarium_manager_get_decor_icon(terrarium_decor_t decor);
const char *terrarium_manager_get_substrate_asset_path(terrarium_substrate_t substrate);
const char *terrarium_manager_get_decor_asset_path(terrarium_decor_t decor);

#ifdef __cplusplus
}
#endif

#endif /* TERRARIUM_MANAGER_H */
