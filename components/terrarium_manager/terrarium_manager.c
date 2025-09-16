#include "terrarium_manager.h"
#include "esp_log.h"
#include "image.h"
#include "sd.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "terrarium_manager";

static terrarium_t s_terrariums[TERRARIUM_MANAGER_MAX_TERRARIUMS];
static size_t s_active_index = 0;
static bool s_initialized = false;
static bool s_simulation_mode = true;

#define ASSET_PATH_SUB(dir, name) MOUNT_POINT "/assets/" dir "/" name

static const char *const s_substrate_asset_paths[TERRARIUM_SUBSTRATE_MAX] = {
    ASSET_PATH_SUB("substrates", "sable.bin"),
    ASSET_PATH_SUB("substrates", "tropical.bin"),
    ASSET_PATH_SUB("substrates", "roche.bin"),
};

static const char *const s_decor_asset_paths[TERRARIUM_DECOR_MAX] = {
    ASSET_PATH_SUB("decors", "lianes.bin"),
    ASSET_PATH_SUB("decors", "rochers.bin"),
    ASSET_PATH_SUB("decors", "caverne.bin"),
};

static const lv_image_dsc_t *const s_substrate_icons[TERRARIUM_SUBSTRATE_MAX] = {
    &gImage_substrate_sable,
    &gImage_substrate_tropical,
    &gImage_substrate_roche,
};

static const lv_image_dsc_t *const s_decor_icons[TERRARIUM_DECOR_MAX] = {
    &gImage_decor_lianes,
    &gImage_decor_rochers,
    &gImage_decor_caverne,
};

static void terrarium_reset_runtime(terrarium_t *terrarium) {
  if (!terrarium) {
    return;
  }
  terrarium->last_tick_ms = 0;
  terrarium->update_ms_accum = 0;
  terrarium->soothe_ms_accum = 0;
  terrarium->soothe_time_ms = 0;
}

static void terrarium_config_set_defaults(terrarium_config_t *cfg, uint8_t id) {
  if (!cfg) {
    return;
  }
  snprintf(cfg->name, sizeof(cfg->name), "Terrarium %02u", (unsigned)(id + 1));
  cfg->substrate = TERRARIUM_SUBSTRATE_SABLE;
  cfg->decor = TERRARIUM_DECOR_LIANES;
  snprintf(cfg->reptile_slot, sizeof(cfg->reptile_slot), "terrarium_%02u.bin",
           (unsigned)(id + 1));
  cfg->dimensions.length_cm = 120;
  cfg->dimensions.width_cm = 60;
  cfg->dimensions.height_cm = 60;
  cfg->species_id[0] = '\0';
}

static esp_err_t ensure_config_directory(void) {
  char dir_mode[96];
  int written = snprintf(dir_mode, sizeof(dir_mode), "%s/%s", MOUNT_POINT,
                         s_simulation_mode ? "sim" : "real");
  if (written < 0 || (size_t)written >= sizeof(dir_mode)) {
    return ESP_ERR_INVALID_SIZE;
  }
  if (mkdir(dir_mode, 0777) != 0 && errno != EEXIST) {
    ESP_LOGE(TAG, "Impossible de créer %s (errno=%d)", dir_mode, errno);
    return ESP_FAIL;
  }

  char dir_terrariums[120];
  written = snprintf(dir_terrariums, sizeof(dir_terrariums), "%s/terrariums",
                     dir_mode);
  if (written < 0 || (size_t)written >= sizeof(dir_terrariums)) {
    return ESP_ERR_INVALID_SIZE;
  }
  if (mkdir(dir_terrariums, 0777) != 0 && errno != EEXIST) {
    ESP_LOGE(TAG, "Impossible de créer %s (errno=%d)", dir_terrariums, errno);
    return ESP_FAIL;
  }
  return ESP_OK;
}

static esp_err_t get_config_path(uint8_t id, char *path, size_t len) {
  if (!path || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  int written = snprintf(path, len, "%s/%s/terrariums/terrarium_%02u.cfg",
                         MOUNT_POINT, s_simulation_mode ? "sim" : "real",
                         (unsigned)(id + 1));
  if (written < 0 || (size_t)written >= len) {
    return ESP_ERR_INVALID_SIZE;
  }
  return ESP_OK;
}

static void sanitize_string(char *buf, size_t len) {
  if (!buf || len == 0) {
    return;
  }
  buf[len - 1] = '\0';
  size_t slen = strnlen(buf, len);
  if (slen == len) {
    buf[len - 1] = '\0';
  }
}

static esp_err_t load_config(terrarium_t *terrarium) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  char path[160];
  esp_err_t err = get_config_path(terrarium->id, path, sizeof(path));
  if (err != ESP_OK) {
    return err;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    return ESP_FAIL;
  }
  long file_size = 0;
  if (fseek(f, 0, SEEK_END) == 0) {
    file_size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
  }
  terrarium_config_t cfg = terrarium->config;
  size_t read = fread(cfg.name, 1, sizeof(cfg.name), f);
  if (read != sizeof(cfg.name)) {
    fclose(f);
    return ESP_FAIL;
  }
  uint8_t substrate = 0;
  uint8_t decor = 0;
  if (fread(&substrate, 1, 1, f) != 1 || fread(&decor, 1, 1, f) != 1) {
    fclose(f);
    return ESP_FAIL;
  }
  if (substrate >= TERRARIUM_SUBSTRATE_MAX) {
    substrate = TERRARIUM_SUBSTRATE_SABLE;
  }
  if (decor >= TERRARIUM_DECOR_MAX) {
    decor = TERRARIUM_DECOR_LIANES;
  }
  if (fread(cfg.reptile_slot, 1, sizeof(cfg.reptile_slot), f) !=
      sizeof(cfg.reptile_slot)) {
    fclose(f);
    return ESP_FAIL;
  }

  size_t base_size = sizeof(cfg.name) + sizeof(uint8_t) + sizeof(uint8_t) +
                     sizeof(cfg.reptile_slot);
  size_t dims_size = sizeof(uint16_t) * 3U;
  size_t species_size = sizeof(cfg.species_id);
  bool has_dims = (file_size >= 0) &&
                  ((size_t)file_size >= (base_size + dims_size));
  bool has_species = (file_size >= 0) &&
                     ((size_t)file_size >= (base_size + dims_size + species_size));

  if (has_dims) {
    uint16_t dims_raw[3] = {cfg.dimensions.length_cm, cfg.dimensions.width_cm,
                            cfg.dimensions.height_cm};
    if (fread(dims_raw, sizeof(uint16_t), 3, f) != 3) {
      fclose(f);
      return ESP_FAIL;
    }
    cfg.dimensions.length_cm = dims_raw[0];
    cfg.dimensions.width_cm = dims_raw[1];
    cfg.dimensions.height_cm = dims_raw[2];
  } else {
    cfg.dimensions = terrarium->config.dimensions;
  }

  if (has_species) {
    if (fread(cfg.species_id, 1, sizeof(cfg.species_id), f) !=
        sizeof(cfg.species_id)) {
      fclose(f);
      return ESP_FAIL;
    }
  } else {
    cfg.species_id[0] = '\0';
  }
  fclose(f);
  sanitize_string(cfg.name, sizeof(cfg.name));
  sanitize_string(cfg.reptile_slot, sizeof(cfg.reptile_slot));
  sanitize_string(cfg.species_id, sizeof(cfg.species_id));
  cfg.substrate = (terrarium_substrate_t)substrate;
  cfg.decor = (terrarium_decor_t)decor;
  terrarium->config = cfg;
  return ESP_OK;
}

static esp_err_t persist_config(const terrarium_t *terrarium) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = ensure_config_directory();
  if (err != ESP_OK) {
    return err;
  }
  char path[160];
  err = get_config_path(terrarium->id, path, sizeof(path));
  if (err != ESP_OK) {
    return err;
  }
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Impossible d'ouvrir %s pour écriture", path);
    return ESP_FAIL;
  }
  const terrarium_config_t *cfg = &terrarium->config;
  if (fwrite(cfg->name, 1, sizeof(cfg->name), f) != sizeof(cfg->name)) {
    fclose(f);
    return ESP_FAIL;
  }
  uint8_t substrate = (uint8_t)cfg->substrate;
  uint8_t decor = (uint8_t)cfg->decor;
  if (fwrite(&substrate, 1, 1, f) != 1 || fwrite(&decor, 1, 1, f) != 1) {
    fclose(f);
    return ESP_FAIL;
  }
  if (fwrite(cfg->reptile_slot, 1, sizeof(cfg->reptile_slot), f) !=
      sizeof(cfg->reptile_slot)) {
    fclose(f);
    return ESP_FAIL;
  }
  uint16_t dims[3] = {cfg->dimensions.length_cm, cfg->dimensions.width_cm,
                      cfg->dimensions.height_cm};
  if (fwrite(dims, sizeof(uint16_t), 3, f) != 3) {
    fclose(f);
    return ESP_FAIL;
  }
  if (fwrite(cfg->species_id, 1, sizeof(cfg->species_id), f) !=
      sizeof(cfg->species_id)) {
    fclose(f);
    return ESP_FAIL;
  }
  fclose(f);
  return ESP_OK;
}

esp_err_t terrarium_manager_init(bool simulation) {
  s_simulation_mode = simulation;
  esp_err_t dir_err = ensure_config_directory();
  if (dir_err != ESP_OK) {
    ESP_LOGE(TAG, "Initialisation du répertoire terrarium échouée (err=0x%x)",
             dir_err);
    return dir_err;
  }

  for (size_t i = 0; i < TERRARIUM_MANAGER_MAX_TERRARIUMS; ++i) {
    terrarium_t *terrarium = &s_terrariums[i];
    memset(terrarium, 0, sizeof(*terrarium));
    terrarium->id = (uint8_t)i;
    terrarium_config_set_defaults(&terrarium->config, terrarium->id);
    terrarium_reset_runtime(terrarium);
    if (load_config(terrarium) != ESP_OK) {
      (void)persist_config(terrarium);
    }
    if (terrarium->config.species_id[0] != '\0') {
      const species_db_entry_t *species =
          species_db_get_by_id(terrarium->config.species_id);
      if (species) {
        terrarium->species_profile = species;
      } else {
        ESP_LOGW(TAG,
                 "Espèce configurée '%s' introuvable pour le terrarium %u,"
                 " remise à zéro",
                 terrarium->config.species_id, (unsigned)(terrarium->id + 1));
        terrarium->config.species_id[0] = '\0';
        terrarium->species_profile = NULL;
        (void)persist_config(terrarium);
      }
    } else {
      terrarium->species_profile = NULL;
    }
  }

  s_active_index = 0;
  s_initialized = true;
  return terrarium_manager_select(0);
}

bool terrarium_manager_is_initialized(void) { return s_initialized; }

size_t terrarium_manager_count(void) { return TERRARIUM_MANAGER_MAX_TERRARIUMS; }

terrarium_t *terrarium_manager_get(size_t index) {
  if (index >= TERRARIUM_MANAGER_MAX_TERRARIUMS) {
    return NULL;
  }
  return &s_terrariums[index];
}

const terrarium_t *terrarium_manager_peek(size_t index) {
  if (index >= TERRARIUM_MANAGER_MAX_TERRARIUMS) {
    return NULL;
  }
  return &s_terrariums[index];
}

static esp_err_t load_state_if_needed(terrarium_t *terrarium) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  if (terrarium->state_loaded) {
    return ESP_OK;
  }
  esp_err_t err = reptile_select_save(terrarium->config.reptile_slot,
                                      s_simulation_mode);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Sélection du slot %s impossible (err=0x%x)",
             terrarium->config.reptile_slot, err);
    return err;
  }
  err = reptile_load(&terrarium->reptile);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Chargement de l'état terrarium %u échoué, remise à zéro",
             (unsigned)(terrarium->id + 1));
    terrarium_manager_reset_state(terrarium);
    reptile_save(&terrarium->reptile);
  }
  if (err == ESP_OK) {
    const species_db_entry_t *species = NULL;
    if (terrarium->config.species_id[0] != '\0') {
      species = species_db_get_by_id(terrarium->config.species_id);
    }
    terrarium->species_profile = species;
    const char *stored_species =
        reptile_get_species_id(&terrarium->reptile);
    if (species) {
      if (!stored_species || strcmp(stored_species, species->id) != 0) {
        reptile_apply_species_profile(&terrarium->reptile, species);
        reptile_save(&terrarium->reptile);
      }
    } else if (stored_species && stored_species[0] != '\0') {
      reptile_clear_species_profile(&terrarium->reptile);
      reptile_save(&terrarium->reptile);
    }
  }
  terrarium->state_loaded = true;
  return ESP_OK;
}

esp_err_t terrarium_manager_select(size_t index) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (index >= TERRARIUM_MANAGER_MAX_TERRARIUMS) {
    return ESP_ERR_INVALID_ARG;
  }
  s_active_index = index;
  terrarium_t *terrarium = &s_terrariums[index];
  terrarium_reset_runtime(terrarium);
  return load_state_if_needed(terrarium);
}

terrarium_t *terrarium_manager_get_active(void) {
  if (!s_initialized) {
    return NULL;
  }
  return &s_terrariums[s_active_index];
}

size_t terrarium_manager_get_active_index(void) {
  if (!s_initialized) {
    return SIZE_MAX;
  }
  return s_active_index;
}

esp_err_t terrarium_manager_set_slot(terrarium_t *terrarium,
                                     const char *slot_name) {
  if (!terrarium || !slot_name || slot_name[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  size_t len = strnlen(slot_name, REPTILE_SLOT_NAME_MAX);
  if (len == REPTILE_SLOT_NAME_MAX) {
    return ESP_ERR_INVALID_SIZE;
  }
  memset(terrarium->config.reptile_slot, 0, sizeof(terrarium->config.reptile_slot));
  memcpy(terrarium->config.reptile_slot, slot_name, len);
  terrarium->state_loaded = false;
  esp_err_t err = terrarium_manager_save_config(terrarium);
  if (err != ESP_OK) {
    return err;
  }
  if (s_initialized && terrarium == &s_terrariums[s_active_index]) {
    err = reptile_select_save(slot_name, s_simulation_mode);
    if (err != ESP_OK) {
      return err;
    }
  }
  return ESP_OK;
}

esp_err_t terrarium_manager_reset_state(terrarium_t *terrarium) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  reptile_clear_species_profile(&terrarium->reptile);
  terrarium->reptile.faim = 100;
  terrarium->reptile.eau = 100;
  terrarium->reptile.humeur = 100;
  terrarium->reptile.event = REPTILE_EVENT_NONE;
  terrarium->reptile.last_update = time(NULL);
  terrarium->state_loaded = true;
  if (terrarium->config.species_id[0] != '\0') {
    const species_db_entry_t *species =
        species_db_get_by_id(terrarium->config.species_id);
    if (species) {
      terrarium->species_profile = species;
      reptile_apply_species_profile(&terrarium->reptile, species);
    } else {
      terrarium->species_profile = NULL;
      terrarium->config.species_id[0] = '\0';
      (void)persist_config(terrarium);
    }
  } else {
    terrarium->species_profile = NULL;
  }
  terrarium_reset_runtime(terrarium);
  return ESP_OK;
}

esp_err_t terrarium_manager_save_config(const terrarium_t *terrarium) {
  return persist_config(terrarium);
}

esp_err_t terrarium_manager_reload_config(terrarium_t *terrarium) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = load_config(terrarium);
  if (err != ESP_OK) {
    return err;
  }
  if (terrarium->config.species_id[0] != '\0') {
    terrarium->species_profile =
        species_db_get_by_id(terrarium->config.species_id);
  } else {
    terrarium->species_profile = NULL;
  }
  return ESP_OK;
}

esp_err_t terrarium_manager_set_species(terrarium_t *terrarium,
                                        const species_db_entry_t *species) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err;
  if (species) {
    strncpy(terrarium->config.species_id, species->id,
            sizeof(terrarium->config.species_id) - 1U);
    terrarium->config.species_id[sizeof(terrarium->config.species_id) - 1U] =
        '\0';
    err = reptile_apply_species_profile(&terrarium->reptile, species);
    terrarium->species_profile = species;
  } else {
    terrarium->config.species_id[0] = '\0';
    err = reptile_clear_species_profile(&terrarium->reptile);
    terrarium->species_profile = NULL;
  }
  if (err != ESP_OK) {
    return err;
  }
  terrarium->state_loaded = true;
  err = terrarium_manager_save_config(terrarium);
  if (err != ESP_OK) {
    return err;
  }
  if (s_initialized) {
    err = reptile_save(&terrarium->reptile);
  }
  return err;
}

const species_db_entry_t *
terrarium_manager_get_species(const terrarium_t *terrarium) {
  if (!terrarium) {
    return NULL;
  }
  if (terrarium->species_profile) {
    return terrarium->species_profile;
  }
  if (terrarium->config.species_id[0] == '\0') {
    return NULL;
  }
  return species_db_get_by_id(terrarium->config.species_id);
}

const lv_image_dsc_t *
terrarium_manager_get_substrate_icon(terrarium_substrate_t substrate) {
  if (substrate >= TERRARIUM_SUBSTRATE_MAX) {
    substrate = TERRARIUM_SUBSTRATE_SABLE;
  }
  return s_substrate_icons[substrate];
}

const lv_image_dsc_t *terrarium_manager_get_decor_icon(terrarium_decor_t decor) {
  if (decor >= TERRARIUM_DECOR_MAX) {
    decor = TERRARIUM_DECOR_LIANES;
  }
  return s_decor_icons[decor];
}

const char *
terrarium_manager_get_substrate_asset_path(terrarium_substrate_t substrate) {
  if (substrate >= TERRARIUM_SUBSTRATE_MAX) {
    substrate = TERRARIUM_SUBSTRATE_SABLE;
  }
  return s_substrate_asset_paths[substrate];
}

const char *terrarium_manager_get_decor_asset_path(terrarium_decor_t decor) {
  if (decor >= TERRARIUM_DECOR_MAX) {
    decor = TERRARIUM_DECOR_LIANES;
  }
  return s_decor_asset_paths[decor];
}
