#include "reptile_logic.h"
#include "regulations.h"
#include "sd.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_random.h"
#else
#include <stdlib.h>
#include <time.h>
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...)                                                 \
  fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGW
#define ESP_LOGW(tag, fmt, ...)                                                 \
  fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGE
#define ESP_LOGE(tag, fmt, ...)                                                 \
  fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#endif

#include <errno.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define FACILITY_MAGIC 0x52544643u /* 'RTFC' */
#define FACILITY_VERSION 3u
#define WEEKLY_SUBSIDY_DEFAULT_CENTS 125000 /* 1 250 € (cahier des charges) */

#define COST_FEEDING_CENTS 180
#define COST_WATER_CENTS 40
#define COST_SUBSTRATE_CENTS 950
#define COST_UV_BULB_CENTS 1600
#define COST_DECOR_KIT_CENTS 4500
#define VET_INTERVENTION_CENTS 12500
#define INCIDENT_FINE_CERT_CENTS 45000
#define INCIDENT_FINE_ENV_CENTS 20000
#define INCIDENT_FINE_REGISTER_CENTS 15000
#define INCIDENT_FINE_DIMENSION_CENTS 30000
#define INCIDENT_FINE_AUDIT_CENTS 60000

#define HOURS_PER_DAY 24.0f

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

typedef struct {
  uint32_t magic;
  uint32_t version;
} facility_blob_header_t;

typedef struct {
  facility_blob_header_t header;
  reptile_facility_t facility;
} facility_blob_t;

typedef struct {
  int64_t cash_cents;
  int64_t daily_income_cents;
  int64_t daily_expenses_cents;
  int64_t fines_cents;
  uint32_t days_elapsed;
} reptile_economy_v2_t;

typedef struct {
  terrarium_t terrariums[REPTILE_MAX_TERRARIUMS];
  uint8_t terrarium_count;
  reptile_inventory_t inventory;
  reptile_economy_v2_t economy;
  reptile_day_cycle_t cycle;
  bool simulation_mode;
  bool sensors_available;
  char slot[16];
  game_mode_t mode;
  uint32_t alerts_active;
  uint32_t pathology_active;
  uint32_t compliance_alerts;
  uint32_t mature_count;
  float average_growth;
  time_t last_persist_time;
} reptile_facility_v2_t;

static const char *TAG = "reptile_logic";
static bool s_storage_warned = false;

static void copy_string(char *dst, size_t len, const char *src);
static void terrarium_reset(terrarium_t *terrarium);
static void facility_reset(reptile_facility_t *facility, uint32_t limit);
static uint32_t facility_effective_limit(bool simulation_mode);
static uint32_t facility_scale_initial_resource(uint32_t base, uint32_t limit);
static bool facility_purge_slots_above_limit(reptile_facility_t *facility,
                                             uint32_t limit);
static void facility_refresh_aggregates(reptile_facility_t *facility);
static const char *mode_dir(game_mode_t mode);
static esp_err_t ensure_storage_ready(const char *context);
static esp_err_t ensure_directories(const reptile_facility_t *facility);
static const char *facility_get_save_path(const reptile_facility_t *facility);
static float clampf(float value, float min_val, float max_val);
static uint32_t random_u32(void);
static float random_uniform(float min, float max);
static void degrade_uv(terrarium_t *terrarium, float hours);
static bool certificates_valid(const terrarium_t *terrarium, time_t now,
                               bool *expired_out);
static void update_growth(terrarium_t *terrarium,
                          const species_profile_t *profile, float hours,
                          bool environment_ok, bool needs_ok);
static void terrarium_set_compliance_message(terrarium_t *terrarium,
                                             const char *fmt, ...);
static void terrarium_init_dimensions_for_rule(terrarium_t *terrarium,
                                               const regulation_rule_t *rule);
static int64_t incident_fine(reptile_incident_t incident);
static int incident_priority(reptile_incident_t incident);

static const species_profile_t s_species_db[REPTILE_SPECIES_COUNT] = {
    {
        .id = REPTILE_SPECIES_GECKO,
        .name = "Gecko léopard",
        .day_temp_min = 28.0f,
        .day_temp_max = 32.0f,
        .night_temp_min = 24.0f,
        .night_temp_max = 27.0f,
        .humidity_min = 40.0f,
        .humidity_max = 60.0f,
        .uv_min = 2.0f,
        .uv_max = 3.5f,
        .growth_rate_per_hour = 0.018f,
        .adult_weight_g = 80.0f,
        .lifespan_days = 3650,
        .food_per_day = 6,
        .water_ml_per_day = 150,
        .ticket_price_cents = 1200,
        .upkeep_cents_per_day = 900,
    },
    {
        .id = REPTILE_SPECIES_PYTHON,
        .name = "Python regius",
        .day_temp_min = 30.0f,
        .day_temp_max = 34.0f,
        .night_temp_min = 26.0f,
        .night_temp_max = 28.0f,
        .humidity_min = 55.0f,
        .humidity_max = 75.0f,
        .uv_min = 2.5f,
        .uv_max = 4.0f,
        .growth_rate_per_hour = 0.015f,
        .adult_weight_g = 1500.0f,
        .lifespan_days = 5475,
        .food_per_day = 2,
        .water_ml_per_day = 400,
        .ticket_price_cents = 2200,
        .upkeep_cents_per_day = 2400,
    },
    {
        .id = REPTILE_SPECIES_TORTOISE,
        .name = "Tortue d'Hermann",
        .day_temp_min = 27.0f,
        .day_temp_max = 32.0f,
        .night_temp_min = 20.0f,
        .night_temp_max = 24.0f,
        .humidity_min = 50.0f,
        .humidity_max = 70.0f,
        .uv_min = 3.0f,
        .uv_max = 4.5f,
        .growth_rate_per_hour = 0.012f,
        .adult_weight_g = 900.0f,
        .lifespan_days = 9125,
        .food_per_day = 8,
        .water_ml_per_day = 250,
        .ticket_price_cents = 1800,
        .upkeep_cents_per_day = 1500,
    },
    {
        .id = REPTILE_SPECIES_CHAMELEON,
        .name = "Caméléon panthère",
        .day_temp_min = 29.0f,
        .day_temp_max = 33.0f,
        .night_temp_min = 22.0f,
        .night_temp_max = 25.0f,
        .humidity_min = 55.0f,
        .humidity_max = 85.0f,
        .uv_min = 4.0f,
        .uv_max = 5.5f,
        .growth_rate_per_hour = 0.020f,
        .adult_weight_g = 150.0f,
        .lifespan_days = 2555,
        .food_per_day = 10,
        .water_ml_per_day = 180,
        .ticket_price_cents = 2100,
        .upkeep_cents_per_day = 1700,
    },
    {
        .id = REPTILE_SPECIES_CUSTOM,
        .name = "Profil personnalisé",
        .day_temp_min = 26.0f,
        .day_temp_max = 32.0f,
        .night_temp_min = 22.0f,
        .night_temp_max = 28.0f,
        .humidity_min = 45.0f,
        .humidity_max = 70.0f,
        .uv_min = 2.0f,
        .uv_max = 4.0f,
        .growth_rate_per_hour = 0.016f,
        .adult_weight_g = 500.0f,
        .lifespan_days = 3650,
        .food_per_day = 4,
        .water_ml_per_day = 200,
        .ticket_price_cents = 1500,
        .upkeep_cents_per_day = 1100,
    },
};

const species_profile_t *reptile_species_get(reptile_species_id_t id) {
  if (id >= REPTILE_SPECIES_COUNT) {
    return NULL;
  }
  return &s_species_db[id];
}

static void copy_string(char *dst, size_t len, const char *src) {
  if (!dst || len == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t copy_len = strnlen(src, len - 1);
  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';
}

static uint32_t random_u32(void) {
#ifdef ESP_PLATFORM
  return esp_random();
#else
  return (uint32_t)rand();
#endif
}

static float random_uniform(float min, float max) {
  if (max <= min) {
    return min;
  }
  float ratio = (float)random_u32() / (float)UINT32_MAX;
  return min + (max - min) * ratio;
}

static float clampf(float value, float min_val, float max_val) {
  if (value < min_val) {
    return min_val;
  }
  if (value > max_val) {
    return max_val;
  }
  return value;
}

static void terrarium_reset(terrarium_t *terrarium) {
  memset(terrarium, 0, sizeof(*terrarium));
  terrarium->temperature_c = 28.0f;
  terrarium->humidity_pct = 55.0f;
  terrarium->uv_index = 3.0f;
  terrarium->satiety = 0.85f;
  terrarium->hydration = 0.85f;
  terrarium->growth = 0.0f;
  terrarium->stage = REPTILE_GROWTH_HATCHLING;
  terrarium->weight_g = 0.0f;
  terrarium->age_days = 0;
  terrarium->age_fraction = 0.0f;
  terrarium->feed_debt = 0.0f;
  terrarium->water_debt = 0.0f;
  terrarium->uv_wear = 0.0f;
  terrarium->pathology = REPTILE_PATHOLOGY_NONE;
  terrarium->incident = REPTILE_INCIDENT_NONE;
  terrarium->pathology_timer_h = 0.0f;
  terrarium->compliance_timer_h = 0.0f;
  terrarium->needs_maintenance = false;
  terrarium->audit_locked = false;
  terrarium->maintenance_hours = 0;
  terrarium->operating_cost_cents_per_day = 0;
  terrarium->revenue_cents_per_day = 0;
  terrarium->last_update = time(NULL);
  copy_string(terrarium->config.substrate, sizeof(terrarium->config.substrate),
              "Terreau tropical");
  copy_string(terrarium->config.heating, sizeof(terrarium->config.heating),
              "Câble 25W");
  copy_string(terrarium->config.decor, sizeof(terrarium->config.decor),
              "Branches + cachettes");
  copy_string(terrarium->config.uv_setup, sizeof(terrarium->config.uv_setup),
              "UVB T5 5%");
  terrarium_set_compliance_message(terrarium,
                                   "Terrarium disponible (aucune espèce attribuée)");
}

static uint32_t facility_effective_limit(bool simulation_mode) {
  return simulation_mode ? SIMULATION_TERRARIUM_LIMIT : REPTILE_MAX_TERRARIUMS;
}

static uint32_t facility_scale_initial_resource(uint32_t base, uint32_t limit) {
  if (base == 0U || limit == 0U) {
    return 0U;
  }
  uint64_t scaled = (uint64_t)base * (uint64_t)limit;
  uint32_t value = (uint32_t)(scaled / REPTILE_MAX_TERRARIUMS);
  if (value == 0U) {
    value = 1U;
  }
  return value;
}

static bool facility_purge_slots_above_limit(reptile_facility_t *facility,
                                             uint32_t limit) {
  if (!facility) {
    return false;
  }
  if (limit > REPTILE_MAX_TERRARIUMS) {
    limit = REPTILE_MAX_TERRARIUMS;
  }
  bool modified = false;
  const terrarium_t blank = {0};
  for (uint32_t i = limit; i < REPTILE_MAX_TERRARIUMS; ++i) {
    terrarium_t *terrarium = &facility->terrariums[i];
    if (!modified &&
        memcmp(terrarium, &blank, sizeof(*terrarium)) != 0) {
      modified = true;
    }
    memset(terrarium, 0, sizeof(*terrarium));
  }
  return modified;
}

/* Recompute aggregate counters without mutating the simulation state.  This is
 * only required when terrariums are truncated (e.g. loading a save with more
 * slots than the active limit).  facility_reset() and similar paths already
 * initialise these fields explicitly, so they intentionally skip this helper to
 * avoid redundant work.
 */
static void facility_refresh_aggregates(reptile_facility_t *facility) {
  if (!facility) {
    return;
  }

  uint32_t limit = facility->terrarium_count;
  if (limit > REPTILE_MAX_TERRARIUMS) {
    limit = REPTILE_MAX_TERRARIUMS;
  }

  uint32_t pathology_count = 0;
  uint32_t incident_count = 0;
  uint32_t compliance_count = 0;
  uint32_t mature_count = 0;
  uint32_t occupied_count = 0;
  float growth_sum = 0.0f;
  time_t now = time(NULL);
  const reptile_day_cycle_t *cycle = &facility->cycle;

  for (uint32_t i = 0; i < limit; ++i) {
    const terrarium_t *terrarium = &facility->terrariums[i];
    if (!terrarium->occupied) {
      continue;
    }

    occupied_count++;
    growth_sum += terrarium->growth;

    const species_profile_t *profile = &terrarium->species;
    if (profile->name[0] == '\0') {
      continue;
    }

    bool expired_cert = false;
    bool cert_ok = certificates_valid(terrarium, now, &expired_cert);
    bool compliance_issue = false;
    bool education_issue = false;

    const regulation_rule_t *rule = regulations_get_rule((int)profile->id);
    if (rule) {
      regulations_compliance_input_t input = {
          .length_cm = terrarium->config.length_cm,
          .width_cm = terrarium->config.width_cm,
          .height_cm = terrarium->config.height_cm,
          .temperature_c = terrarium->temperature_c,
          .humidity_pct = terrarium->humidity_pct,
          .uv_index = terrarium->uv_index,
          .is_daytime = cycle->is_daytime,
          .certificate_count = terrarium->certificate_count,
          .certificate_valid = cert_ok,
          .certificate_expired = expired_cert,
          .register_present = terrarium->config.register_completed,
          .education_present = terrarium->config.educational_panel_present,
      };
      regulations_compliance_report_t report = {0};
      if (regulations_evaluate(rule, &input, &report) == ESP_OK) {
        if (!report.allowed) {
          compliance_issue = true;
        } else if (!report.dimensions_ok) {
          compliance_issue = true;
        } else if (!report.certificate_ok) {
          compliance_issue = true;
        } else if (!report.register_ok) {
          compliance_issue = true;
        } else if (!report.education_ok) {
          education_issue = true;
        }
      }
    } else if (!cert_ok) {
      compliance_issue = true;
    }

    if (terrarium->stage >= REPTILE_GROWTH_ADULT) {
      mature_count++;
    }
    if (terrarium->pathology != REPTILE_PATHOLOGY_NONE) {
      pathology_count++;
    }
    if (terrarium->incident != REPTILE_INCIDENT_NONE) {
      incident_count++;
    }
    if (compliance_issue || education_issue) {
      compliance_count++;
    }
  }

  facility->alerts_active = incident_count + pathology_count;
  facility->pathology_active = pathology_count;
  facility->compliance_alerts = compliance_count;
  facility->mature_count = mature_count;
  facility->average_growth =
      (occupied_count > 0U) ? (growth_sum / (float)occupied_count) : 0.0f;
}

static void facility_reset(reptile_facility_t *facility, uint32_t limit) {
  if (!facility) {
    return;
  }
  if (limit > REPTILE_MAX_TERRARIUMS) {
    limit = REPTILE_MAX_TERRARIUMS;
  }
  for (uint32_t i = 0; i < limit; ++i) {
    terrarium_reset(&facility->terrariums[i]);
  }
  facility_purge_slots_above_limit(facility, limit);
  facility->terrarium_count = (uint8_t)limit;
  facility->inventory.feeders = facility_scale_initial_resource(180U, limit);
  facility->inventory.supplement_doses =
      facility_scale_initial_resource(120U, limit);
  facility->inventory.substrate_bags =
      facility_scale_initial_resource(24U, limit);
  facility->inventory.uv_bulbs = facility_scale_initial_resource(12U, limit);
  facility->inventory.decor_kits = facility_scale_initial_resource(10U, limit);
  facility->inventory.water_reserve_l =
      facility_scale_initial_resource(300U, limit);
  facility->economy.cash_cents = 350000; /* 3 500 € */
  facility->economy.daily_income_cents = 0;
  facility->economy.daily_expenses_cents = 0;
  facility->economy.fines_cents = 0;
  facility->economy.days_elapsed = 0;
  facility->economy.weekly_subsidy_cents = WEEKLY_SUBSIDY_DEFAULT_CENTS;
  facility->cycle.is_daytime = true;
  facility->cycle.day_ms = 8U * 60U * 1000U;
  facility->cycle.night_ms = 4U * 60U * 1000U;
  facility->cycle.elapsed_in_phase_ms = 0;
  facility->cycle.cycle_index = 0;
  facility->alerts_active = 0;
  facility->pathology_active = 0;
  facility->compliance_alerts = 0;
  facility->mature_count = 0;
  facility->last_persist_time = 0;
  facility->average_growth = 0.0f;
}

static const char *mode_dir(game_mode_t mode) {
  (void)mode;
  return "sim";
}

static esp_err_t ensure_storage_ready(const char *context) {
#ifndef ESP_PLATFORM
  if (!sd_is_mounted()) {
    if (mkdir(SD_MOUNT_POINT, 0777) == 0) {
      s_storage_warned = false;
      return ESP_OK;
    }
  }
  if (sd_is_mounted()) {
    s_storage_warned = false;
    return ESP_OK;
  }
#else
  if (sd_is_mounted()) {
    s_storage_warned = false;
    return ESP_OK;
  }
#endif

  if (!s_storage_warned) {
    if (context && context[0] != '\0') {
      ESP_LOGW(TAG,
               "Support SD non monté - %s ignorée. Progression maintenue uniquement en RAM.",
               context);
    } else {
      ESP_LOGW(TAG,
               "Support SD non monté - opération ignorée. Progression maintenue uniquement en RAM.");
    }
    s_storage_warned = true;
  }
  return ESP_ERR_INVALID_STATE;
}

static esp_err_t ensure_directories(const reptile_facility_t *facility) {
  (void)facility;
  esp_err_t ready = ensure_storage_ready("préparation des dossiers de sauvegarde");
  if (ready != ESP_OK) {
    return ready;
  }

  esp_err_t status = ESP_OK;
  const char *base = MOUNT_POINT;
  if (mkdir(base, 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "Création du dossier %s impossible (%d)", base, errno);
    status = ESP_FAIL;
  }

  char sim_dir[64];
  snprintf(sim_dir, sizeof(sim_dir), "%s/sim", base);
  if (mkdir(sim_dir, 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "Création du dossier %s impossible (%d)", sim_dir, errno);
    status = ESP_FAIL;
  }
  return status;
}

static const char *facility_get_save_path(const reptile_facility_t *facility) {
  static char path[96];
  const char *slot = (facility->slot[0] != '\0') ? facility->slot : "slot_a";
  snprintf(path, sizeof(path), "%s/%s/%s.bin", MOUNT_POINT,
           mode_dir(facility->mode), slot);
  return path;
}

esp_err_t reptile_facility_save(const reptile_facility_t *facility) {
  if (!facility) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t dir_err = ensure_directories(facility);
  if (dir_err != ESP_OK) {
    return dir_err;
  }

  facility_blob_t blob = {
      .header =
          {
              .magic = FACILITY_MAGIC,
              .version = FACILITY_VERSION,
          },
      .facility = *facility,
  };
  blob.facility.last_persist_time = time(NULL);

  const char *path = facility_get_save_path(facility);
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Impossible d'ouvrir %s en écriture", path);
    return ESP_FAIL;
  }
  size_t written = fwrite(&blob, sizeof(blob), 1, f);
  fclose(f);
  if (written != 1) {
    ESP_LOGE(TAG, "Écriture incomplète pour %s", path);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "État sauvegardé dans %s", path);
  return ESP_OK;
}

esp_err_t reptile_facility_load(reptile_facility_t *facility) {
  if (!facility) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ready = ensure_storage_ready("chargement de l'élevage");
  if (ready != ESP_OK) {
    return ready;
  }

  const char *path = facility_get_save_path(facility);
  FILE *f = fopen(path, "rb");
  if (!f) {
    return ESP_FAIL;
  }

  facility_blob_header_t header = {0};
  size_t read = fread(&header, sizeof(header), 1, f);
  if (read != 1) {
    ESP_LOGW(TAG, "Lecture du header de %s impossible", path);
    fclose(f);
    return ESP_FAIL;
  }
  if (header.magic != FACILITY_MAGIC) {
    ESP_LOGW(TAG, "Fichier de sauvegarde %s invalide (magic)", path);
    fclose(f);
    return ESP_FAIL;
  }
  if (header.version > FACILITY_VERSION || header.version < 2u) {
    ESP_LOGW(TAG, "Version de sauvegarde %u non supportée pour %s",
             header.version, path);
    fclose(f);
    return ESP_FAIL;
  }

  reptile_facility_t loaded;
  memset(&loaded, 0, sizeof(loaded));

  if (header.version == FACILITY_VERSION) {
    size_t fac_read = fread(&loaded, sizeof(loaded), 1, f);
    if (fac_read != 1) {
      ESP_LOGW(TAG, "Fichier de sauvegarde %s incomplet (v%u)", path,
               header.version);
      fclose(f);
      return ESP_FAIL;
    }
  } else {
    reptile_facility_v2_t legacy;
    memset(&legacy, 0, sizeof(legacy));
    size_t fac_read = fread(&legacy, sizeof(legacy), 1, f);
    if (fac_read != 1) {
      ESP_LOGW(TAG, "Fichier de sauvegarde %s incomplet (v%u)", path,
               header.version);
      fclose(f);
      return ESP_FAIL;
    }
    memcpy(&loaded, &legacy, sizeof(legacy));
    loaded.economy.weekly_subsidy_cents = WEEKLY_SUBSIDY_DEFAULT_CENTS;
    ESP_LOGI(TAG,
             "Migration sauvegarde v2 -> v3 : subvention hebdomadaire fixée à %.2f €",
             (double)WEEKLY_SUBSIDY_DEFAULT_CENTS / 100.0);
  }

  fclose(f);

  bool simulation = facility->simulation_mode;
  bool sensors_available = facility->sensors_available;
  game_mode_t mode = facility->mode;
  char slot_copy[sizeof(facility->slot)];
  copy_string(slot_copy, sizeof(slot_copy), facility->slot);

  *facility = loaded;
  facility->simulation_mode = simulation;
  facility->sensors_available = sensors_available;
  facility->mode = mode;
  copy_string(facility->slot, sizeof(facility->slot), slot_copy);

  if (facility->terrarium_count > REPTILE_MAX_TERRARIUMS) {
    facility->terrarium_count = REPTILE_MAX_TERRARIUMS;
  }

  uint32_t limit = facility_effective_limit(facility->simulation_mode);
  if (limit > REPTILE_MAX_TERRARIUMS) {
    limit = REPTILE_MAX_TERRARIUMS;
  }
  bool reduced = false;
  if (facility->terrarium_count > limit) {
    facility->terrarium_count = (uint8_t)limit;
    reduced = true;
  }
  if (facility_purge_slots_above_limit(facility, limit)) {
    reduced = true;
  }
  if (reduced) {
    facility_refresh_aggregates(facility);
    /* Persist counters that now match the truncated state so future loads do
     * not reintroduce stale aggregates.
     */
    ESP_LOGI(TAG,
             "Réduction automatique de l'élevage à %u terrariums (mode simulation)",
             (unsigned)limit);
    esp_err_t save_err = reptile_facility_save(facility);
    if (save_err != ESP_OK) {
      ESP_LOGW(TAG,
               "Impossible de persister la réduction automatique (err=%d)",
               save_err);
    }
  }

  ESP_LOGI(TAG, "État chargé depuis %s", path);
  return ESP_OK;
}

esp_err_t reptile_facility_set_slot(reptile_facility_t *facility,
                                    const char *slot_name) {
  if (!facility) {
    return ESP_ERR_INVALID_ARG;
  }

  char new_slot[sizeof(facility->slot)];
  if (slot_name && slot_name[0] != '\0') {
    copy_string(new_slot, sizeof(new_slot), slot_name);
  } else {
    copy_string(new_slot, sizeof(new_slot), "slot_a");
  }
  copy_string(facility->slot, sizeof(facility->slot), new_slot);

  uint32_t limit = facility_effective_limit(facility->simulation_mode);

  if (reptile_facility_load(facility) != ESP_OK) {
    facility_reset(facility, limit);
    return reptile_facility_save(facility);
  }
  return ESP_OK;
}

esp_err_t reptile_facility_init(reptile_facility_t *facility, bool simulation,
                                const char *slot_name, game_mode_t mode) {
  if (!facility) {
    return ESP_ERR_INVALID_ARG;
  }

  (void)simulation;
  (void)mode;

#ifndef ESP_PLATFORM
  static bool s_seeded = false;
  if (!s_seeded) {
    s_seeded = true;
    srand((unsigned)time(NULL));
  }
#endif

  memset(facility, 0, sizeof(*facility));
  facility->simulation_mode = true;
  facility->mode = GAME_MODE_SIMULATION;
  facility->sensors_available = true;
  if (!slot_name || slot_name[0] == '\0') {
    copy_string(facility->slot, sizeof(facility->slot), "slot_a");
  } else {
    copy_string(facility->slot, sizeof(facility->slot), slot_name);
  }

  uint32_t limit = facility_effective_limit(facility->simulation_mode);
  facility_reset(facility, limit);

  esp_err_t dir_err = ensure_directories(facility);
  if (dir_err != ESP_OK) {
    if (dir_err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG,
               "Dossiers de sauvegarde indisponibles (err=%d). Fonctionnement en RAM uniquement",
               dir_err);
    }
    return ESP_OK;
  }

  if (reptile_facility_load(facility) != ESP_OK) {
    ESP_LOGI(TAG, "Initialisation d'un nouvel élevage (%s)",
             facility_get_save_path(facility));
    reptile_facility_save(facility);
  }
  return ESP_OK;
}

static void degrade_uv(terrarium_t *terrarium, float hours) {
  terrarium->uv_wear += hours / (24.0f * 30.0f);
  if (terrarium->uv_wear >= 1.0f) {
    terrarium->uv_index -= 0.4f;
    terrarium->uv_wear -= 1.0f;
  }
  terrarium->uv_index = clampf(terrarium->uv_index, 0.0f, 12.0f);
}

static bool certificates_valid(const terrarium_t *terrarium, time_t now,
                               bool *expired_out) {
  bool valid = false;
  bool expired = false;
  for (uint32_t i = 0; i < terrarium->certificate_count; ++i) {
    const reptile_certificate_t *cert = &terrarium->certificates[i];
    if (!cert->valid) {
      continue;
    }
    if (cert->expiry_date == 0 || cert->expiry_date > now) {
      valid = true;
    } else {
      expired = true;
    }
  }
  if (expired_out) {
    *expired_out = expired && !valid;
  }
  return valid;
}

static void terrarium_set_compliance_message(terrarium_t *terrarium,
                                             const char *fmt, ...) {
  if (!terrarium || !fmt) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  vsnprintf(terrarium->compliance_message,
            sizeof(terrarium->compliance_message), fmt, args);
  va_end(args);
}

static void terrarium_init_dimensions_for_rule(terrarium_t *terrarium,
                                               const regulation_rule_t *rule) {
  if (!terrarium || !rule) {
    return;
  }
  terrarium->config.length_cm =
      MAX(terrarium->config.length_cm, rule->min_length_cm);
  terrarium->config.width_cm =
      MAX(terrarium->config.width_cm, rule->min_width_cm);
  terrarium->config.height_cm =
      MAX(terrarium->config.height_cm, rule->min_height_cm);
}

static int64_t incident_fine(reptile_incident_t incident) {
  switch (incident) {
  case REPTILE_INCIDENT_CERTIFICATE_MISSING:
  case REPTILE_INCIDENT_CERTIFICATE_EXPIRED:
    return INCIDENT_FINE_CERT_CENTS;
  case REPTILE_INCIDENT_REGISTER_MISSING:
    return INCIDENT_FINE_REGISTER_CENTS;
  case REPTILE_INCIDENT_DIMENSION_NON_CONFORM:
    return INCIDENT_FINE_DIMENSION_CENTS;
  case REPTILE_INCIDENT_AUDIT_LOCK:
    return INCIDENT_FINE_AUDIT_CENTS;
  default:
    return 0;
  }
}

static int incident_priority(reptile_incident_t incident) {
  switch (incident) {
  case REPTILE_INCIDENT_AUDIT_LOCK:
    return 6;
  case REPTILE_INCIDENT_DIMENSION_NON_CONFORM:
    return 5;
  case REPTILE_INCIDENT_CERTIFICATE_EXPIRED:
    return 4;
  case REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE:
    return 4;
  case REPTILE_INCIDENT_CERTIFICATE_MISSING:
    return 3;
  case REPTILE_INCIDENT_REGISTER_MISSING:
    return 2;
  case REPTILE_INCIDENT_EDUCATION_MISSING:
    return 1;
  case REPTILE_INCIDENT_NONE:
  default:
    return 0;
  }
}

static void update_growth(terrarium_t *terrarium,
                          const species_profile_t *profile, float hours,
                          bool environment_ok, bool needs_ok) {
  if (environment_ok && needs_ok &&
      terrarium->pathology == REPTILE_PATHOLOGY_NONE) {
    float delta = profile->growth_rate_per_hour * hours;
    terrarium->growth = clampf(terrarium->growth + delta, 0.0f, 1.2f);
  } else {
    terrarium->growth = clampf(terrarium->growth - 0.01f * hours, 0.0f, 1.2f);
  }

  terrarium->age_fraction += hours / HOURS_PER_DAY;
  if (terrarium->age_fraction >= 1.0f) {
    uint32_t add_days = (uint32_t)terrarium->age_fraction;
    terrarium->age_days += add_days;
    terrarium->age_fraction -= (float)add_days;
  }

  if (terrarium->age_days > profile->lifespan_days &&
      terrarium->growth >= 0.8f) {
    terrarium->stage = REPTILE_GROWTH_SENIOR;
  } else if (terrarium->growth >= 0.6f) {
    terrarium->stage = REPTILE_GROWTH_ADULT;
  } else if (terrarium->growth >= 0.25f) {
    terrarium->stage = REPTILE_GROWTH_JUVENILE;
  } else {
    terrarium->stage = REPTILE_GROWTH_HATCHLING;
  }

  terrarium->weight_g = profile->adult_weight_g * MIN(terrarium->growth, 1.0f);
}

void reptile_facility_tick(reptile_facility_t *facility, uint32_t elapsed_ms) {
  if (!facility || elapsed_ms == 0) {
    return;
  }

  float hours = (float)elapsed_ms / 3600000.0f;
  reptile_day_cycle_t *cycle = &facility->cycle;
  uint32_t phase_target = cycle->is_daytime ? cycle->day_ms : cycle->night_ms;
  cycle->elapsed_in_phase_ms += elapsed_ms;
  while (phase_target > 0 && cycle->elapsed_in_phase_ms >= phase_target) {
    cycle->elapsed_in_phase_ms -= phase_target;
    cycle->is_daytime = !cycle->is_daytime;
    cycle->cycle_index++;
    phase_target = cycle->is_daytime ? cycle->day_ms : cycle->night_ms;
    if (cycle->is_daytime) {
      facility->economy.days_elapsed++;
      facility->economy.daily_income_cents = 0;
      facility->economy.daily_expenses_cents = 0;
      if (facility->economy.weekly_subsidy_cents != 0 &&
          facility->economy.days_elapsed != 0U &&
          (facility->economy.days_elapsed % 7U) == 0U) {
        facility->economy.cash_cents +=
            facility->economy.weekly_subsidy_cents;
      }
    }
  }

  uint32_t pathology_count = 0;
  uint32_t incident_count = 0;
  uint32_t compliance_count = 0;
  uint32_t mature_count = 0;
  uint32_t occupied_count = 0;
  float growth_sum = 0.0f;
  time_t now = time(NULL);

  for (uint32_t i = 0; i < facility->terrarium_count; ++i) {
    terrarium_t *terrarium = &facility->terrariums[i];
    if (!terrarium->occupied) {
      continue;
    }
    occupied_count++;
    growth_sum += terrarium->growth;

    const species_profile_t *profile = &terrarium->species;
    if (profile->name[0] == '\0') {
      continue;
    }

    const regulation_rule_t *rule = regulations_get_rule((int)profile->id);
    bool compliance_alert = false;

    float target_temp_min =
        cycle->is_daytime ? profile->day_temp_min : profile->night_temp_min;
    float target_temp_max =
        cycle->is_daytime ? profile->day_temp_max : profile->night_temp_max;
    float target_temp_mid = (target_temp_min + target_temp_max) * 0.5f;

    if (facility->simulation_mode) {
      terrarium->temperature_c +=
          (target_temp_mid - terrarium->temperature_c) * 0.12f;
      terrarium->temperature_c += random_uniform(-0.3f, 0.3f);
    }
    terrarium->temperature_c =
        clampf(terrarium->temperature_c, target_temp_min - 3.0f,
               target_temp_max + 3.0f);

    float humidity_mid = (profile->humidity_min + profile->humidity_max) * 0.5f;
    if (facility->simulation_mode) {
      terrarium->humidity_pct +=
          (humidity_mid - terrarium->humidity_pct) * 0.10f;
      terrarium->humidity_pct += random_uniform(-1.5f, 1.5f);
    }
    terrarium->humidity_pct = clampf(terrarium->humidity_pct, 0.0f, 100.0f);

    float uv_mid = (profile->uv_min + profile->uv_max) * 0.5f;
    if (facility->simulation_mode) {
      terrarium->uv_index += (uv_mid - terrarium->uv_index) * 0.15f;
      terrarium->uv_index += random_uniform(-0.08f, 0.08f);
    }
    degrade_uv(terrarium, hours);

    if (terrarium->uv_index < profile->uv_min - 0.1f &&
        facility->inventory.uv_bulbs > 0) {
      facility->inventory.uv_bulbs--;
      terrarium->uv_index = uv_mid;
      facility->economy.daily_expenses_cents += COST_UV_BULB_CENTS;
      facility->economy.cash_cents -= COST_UV_BULB_CENTS;
    }

    float satiety_loss = (0.02f + (float)profile->food_per_day * 0.0025f) * hours;
    terrarium->satiety = clampf(terrarium->satiety - satiety_loss, 0.0f, 1.0f);
    terrarium->feed_debt +=
        (float)profile->food_per_day * hours / HOURS_PER_DAY;
    if (terrarium->satiety < 0.40f || terrarium->feed_debt >= 1.0f) {
      uint32_t required = (uint32_t)floorf(terrarium->feed_debt);
      if (terrarium->satiety < 0.40f && required == 0) {
        required = 1;
      }
      if (required > 0 && facility->inventory.feeders >= required) {
        facility->inventory.feeders -= required;
        terrarium->satiety = clampf(terrarium->satiety + 0.38f + 0.06f * required,
                                    0.0f, 1.0f);
        terrarium->feed_debt -= (float)required;
        int64_t cost = (int64_t)required * COST_FEEDING_CENTS;
        facility->economy.daily_expenses_cents += cost;
        facility->economy.cash_cents -= cost;
      }
      terrarium->feed_debt = clampf(terrarium->feed_debt, 0.0f, 5.0f);
    }

    float hydration_loss =
        (0.018f + (float)profile->water_ml_per_day * 0.0008f) * hours;
    terrarium->hydration =
        clampf(terrarium->hydration - hydration_loss, 0.0f, 1.0f);
    terrarium->water_debt +=
        (float)profile->water_ml_per_day * hours / 1000.0f;
    if (terrarium->hydration < 0.40f || terrarium->water_debt >= 0.5f) {
      uint32_t liters = (uint32_t)ceilf(terrarium->water_debt);
      if (liters == 0) {
        liters = 1;
      }
      if (facility->inventory.water_reserve_l >= liters) {
        facility->inventory.water_reserve_l -= liters;
        terrarium->hydration = clampf(
            terrarium->hydration + 0.35f + 0.05f * (float)liters, 0.0f, 1.0f);
        terrarium->water_debt -= (float)liters;
        int64_t cost = (int64_t)liters * COST_WATER_CENTS;
        facility->economy.daily_expenses_cents += cost;
        facility->economy.cash_cents -= cost;
      }
      if (terrarium->water_debt < 0.0f) {
        terrarium->water_debt = 0.0f;
      }
    }

    terrarium->maintenance_hours += (uint32_t)lroundf(hours);
    if (terrarium->maintenance_hours > 144) {
      terrarium->needs_maintenance = true;
    }

    bool temp_ok = terrarium->temperature_c >= target_temp_min &&
                   terrarium->temperature_c <= target_temp_max;
    bool humidity_ok = terrarium->humidity_pct >= profile->humidity_min &&
                       terrarium->humidity_pct <= profile->humidity_max;
    bool uv_ok = terrarium->uv_index >= profile->uv_min &&
                 terrarium->uv_index <= profile->uv_max;
    bool needs_ok = terrarium->satiety > 0.35f && terrarium->hydration > 0.35f;
    bool environment_ok = temp_ok && humidity_ok && uv_ok;

    reptile_pathology_t previous_pathology = terrarium->pathology;
    if (!environment_ok || !needs_ok) {
      terrarium->pathology_timer_h += hours;
      if (terrarium->pathology_timer_h > 4.0f &&
          terrarium->pathology == REPTILE_PATHOLOGY_NONE) {
        if (!temp_ok || !humidity_ok) {
          terrarium->pathology = REPTILE_PATHOLOGY_RESPIRATORY;
        } else if (!needs_ok) {
          terrarium->pathology = REPTILE_PATHOLOGY_METABOLIC;
        } else {
          terrarium->pathology = REPTILE_PATHOLOGY_PARASITIC;
        }
        facility->economy.daily_expenses_cents += VET_INTERVENTION_CENTS;
        facility->economy.cash_cents -= VET_INTERVENTION_CENTS;
      }
    } else {
      terrarium->pathology_timer_h =
          MAX(0.0f, terrarium->pathology_timer_h - hours * 2.5f);
      if (terrarium->pathology != REPTILE_PATHOLOGY_NONE &&
          terrarium->pathology_timer_h < 1.0f) {
        terrarium->pathology = REPTILE_PATHOLOGY_NONE;
      }
    }

    reptile_incident_t previous_incident = terrarium->incident;
    bool expired_cert = false;
    bool cert_ok = certificates_valid(terrarium, now, &expired_cert);

    regulations_compliance_report_t reg_report = {0};
    reptile_incident_t compliance_incident = REPTILE_INCIDENT_NONE;
    bool compliance_issue = false;
    bool education_issue = false;

    if (rule) {
      regulations_compliance_input_t input = {
          .length_cm = terrarium->config.length_cm,
          .width_cm = terrarium->config.width_cm,
          .height_cm = terrarium->config.height_cm,
          .temperature_c = terrarium->temperature_c,
          .humidity_pct = terrarium->humidity_pct,
          .uv_index = terrarium->uv_index,
          .is_daytime = cycle->is_daytime,
          .certificate_count = terrarium->certificate_count,
          .certificate_valid = cert_ok,
          .certificate_expired = expired_cert,
          .register_present = terrarium->config.register_completed,
          .education_present = terrarium->config.educational_panel_present,
      };
      if (regulations_evaluate(rule, &input, &reg_report) == ESP_OK) {
        terrarium->audit_locked = reg_report.blocking;
        if (!reg_report.allowed) {
          compliance_incident = REPTILE_INCIDENT_AUDIT_LOCK;
          compliance_issue = true;
          terrarium_set_compliance_message(terrarium,
                                           "Espèce interdite (%s)",
                                           rule->legal_reference);
        } else if (!reg_report.dimensions_ok) {
          compliance_incident = REPTILE_INCIDENT_DIMENSION_NON_CONFORM;
          compliance_issue = true;
          terrarium_set_compliance_message(
              terrarium,
              "Dimensions mini %.0fx%.0fx%.0f cm (%s)",
              rule->min_length_cm, rule->min_width_cm, rule->min_height_cm,
              rule->legal_reference);
        } else if (!reg_report.certificate_ok) {
          compliance_incident = expired_cert
                                    ? REPTILE_INCIDENT_CERTIFICATE_EXPIRED
                                    : REPTILE_INCIDENT_CERTIFICATE_MISSING;
          compliance_issue = true;
          terrarium_set_compliance_message(terrarium, "%s",
                                           rule->certificate_text);
        } else if (!reg_report.register_ok) {
          compliance_incident = REPTILE_INCIDENT_REGISTER_MISSING;
          compliance_issue = true;
          terrarium_set_compliance_message(terrarium,
                                           "Registre obligatoire absent (%s)",
                                           rule->legal_reference);
        } else if (!reg_report.education_ok) {
          compliance_incident = REPTILE_INCIDENT_EDUCATION_MISSING;
          education_issue = true;
          terrarium_set_compliance_message(terrarium,
                                           "Pédagogie à compléter : %s",
                                           rule->education_text);
        } else {
          terrarium->audit_locked = false;
          terrarium_set_compliance_message(terrarium, "Conforme (%s)",
                                           rule->legal_reference);
        }
      }
    } else {
      terrarium->audit_locked = !cert_ok;
      if (!cert_ok) {
        compliance_incident = expired_cert ? REPTILE_INCIDENT_CERTIFICATE_EXPIRED
                                           : REPTILE_INCIDENT_CERTIFICATE_MISSING;
        compliance_issue = true;
        terrarium_set_compliance_message(terrarium,
                                         "Certificat requis pour %s",
                                         profile->name);
      } else if (terrarium->compliance_message[0] == '\0') {
        terrarium_set_compliance_message(terrarium,
                                         "Contrôle documentaire à jour");
      }
    }

    if (!compliance_issue && !education_issue) {
      terrarium->compliance_timer_h = 0.0f;
    } else if (compliance_incident != REPTILE_INCIDENT_EDUCATION_MISSING) {
      terrarium->compliance_timer_h += hours;
      if (terrarium->compliance_timer_h > 6.0f &&
          previous_incident != compliance_incident) {
        int64_t fine = incident_fine(compliance_incident);
        if (fine > 0) {
          facility->economy.fines_cents += fine;
          facility->economy.cash_cents -= fine;
        }
      }
    } else {
      terrarium->compliance_timer_h = 0.0f;
    }

    terrarium->incident = compliance_incident;
    compliance_alert = compliance_issue || education_issue;

    bool environment_violation =
        (!environment_ok && terrarium->pathology_timer_h > 8.0f);
    reptile_incident_t final_incident = terrarium->incident;
    if (environment_violation) {
      if (incident_priority(REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE) >
          incident_priority(final_incident)) {
        if (previous_incident != REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE) {
          facility->economy.fines_cents += INCIDENT_FINE_ENV_CENTS;
          facility->economy.cash_cents -= INCIDENT_FINE_ENV_CENTS;
        }
        final_incident = REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE;
      }
    } else if (final_incident == REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE) {
      final_incident = REPTILE_INCIDENT_NONE;
    }
    terrarium->incident = final_incident;

    update_growth(terrarium, profile, hours, environment_ok, needs_ok);

    if (terrarium->stage >= REPTILE_GROWTH_ADULT) {
      mature_count++;
    }

    if (terrarium->pathology != REPTILE_PATHOLOGY_NONE) {
      pathology_count++;
    }
    if (terrarium->incident != REPTILE_INCIDENT_NONE) {
      incident_count++;
    }
    if (compliance_alert) {
      compliance_count++;
    }

    int64_t op_cost = terrarium->operating_cost_cents_per_day;
    if (op_cost == 0) {
      op_cost = profile->upkeep_cents_per_day;
    }
    int64_t op_expense = (int64_t)llroundf(op_cost * hours / HOURS_PER_DAY);
    facility->economy.daily_expenses_cents += op_expense;
    facility->economy.cash_cents -= op_expense;

    if (terrarium->stage >= REPTILE_GROWTH_ADULT) {
      int64_t revenue = (int64_t)llroundf(
          terrarium->revenue_cents_per_day * hours / HOURS_PER_DAY);
      if (revenue == 0) {
        revenue = (int64_t)llroundf(
            profile->ticket_price_cents * hours / HOURS_PER_DAY);
      }
      facility->economy.daily_income_cents += revenue;
      facility->economy.cash_cents += revenue;
    }

    terrarium->last_update = now;
    if (previous_pathology != terrarium->pathology &&
        terrarium->pathology == REPTILE_PATHOLOGY_NONE) {
      terrarium->pathology_timer_h = 0.0f;
    }
  }

  facility->alerts_active = incident_count + pathology_count;
  facility->pathology_active = pathology_count;
  facility->compliance_alerts = compliance_count;
  facility->mature_count = mature_count;

  facility->average_growth =
      (occupied_count > 0) ? (growth_sum / (float)occupied_count) : 0.0f;
}

bool reptile_facility_sensors_available(const reptile_facility_t *facility) {
  return facility && facility->sensors_available;
}

terrarium_t *reptile_facility_get_terrarium(reptile_facility_t *facility,
                                            uint8_t index) {
  if (!facility || index >= facility->terrarium_count) {
    return NULL;
  }
  return &facility->terrariums[index];
}

const terrarium_t *reptile_facility_get_terrarium_const(
    const reptile_facility_t *facility, uint8_t index) {
  if (!facility || index >= facility->terrarium_count) {
    return NULL;
  }
  return &facility->terrariums[index];
}

void reptile_facility_compute_metrics(const reptile_facility_t *facility,
                                      reptile_facility_metrics_t *out) {
  if (!facility || !out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  for (uint32_t i = 0; i < facility->terrarium_count; ++i) {
    const terrarium_t *terrarium = &facility->terrariums[i];
    if (!terrarium->occupied) {
      continue;
    }
    out->occupied++;
    out->avg_growth += terrarium->growth;
    if (terrarium->pathology != REPTILE_PATHOLOGY_NONE) {
      out->pathologies++;
    }
    if (terrarium->incident != REPTILE_INCIDENT_NONE) {
      out->incidents++;
    }
    if (terrarium->stage >= REPTILE_GROWTH_ADULT) {
      out->mature++;
    }
  }
  out->free_slots = facility->terrarium_count - out->occupied;
  if (out->occupied > 0) {
    out->avg_growth /= (float)out->occupied;
  }
}

void reptile_facility_reset_statistics(reptile_facility_t *facility) {
  if (!facility) {
    return;
  }
  facility->economy.daily_expenses_cents = 0;
  facility->economy.daily_income_cents = 0;
  facility->economy.fines_cents = 0;
}

void reptile_facility_reset_state(reptile_facility_t *facility) {
  if (!facility) {
    return;
  }
  uint32_t limit = facility_effective_limit(facility->simulation_mode);
  facility_reset(facility, limit);
}

void reptile_facility_reset_with_limit(reptile_facility_t *facility,
                                       uint32_t limit) {
  facility_reset(facility, limit);
}

esp_err_t reptile_terrarium_set_species(terrarium_t *terrarium,
                                        const species_profile_t *profile,
                                        const char *nickname) {
  if (!terrarium || !profile) {
    return ESP_ERR_INVALID_ARG;
  }
  char reason[REPTILE_COMPLIANCE_MSG_LEN];
  esp_err_t reg =
      regulations_validate_species((int)profile->id, reason, sizeof(reason));
  if (reg != ESP_OK) {
    terrarium_set_compliance_message(terrarium, "%s", reason);
    return reg;
  }
  terrarium_reset(terrarium);
  terrarium->occupied = true;
  terrarium->species = *profile;
  if (nickname && nickname[0] != '\0') {
    copy_string(terrarium->nickname, sizeof(terrarium->nickname), nickname);
  } else {
    copy_string(terrarium->nickname, sizeof(terrarium->nickname),
                profile->name);
  }
  terrarium->temperature_c =
      (profile->day_temp_min + profile->day_temp_max) * 0.5f;
  terrarium->humidity_pct =
      (profile->humidity_min + profile->humidity_max) * 0.5f;
  terrarium->uv_index = (profile->uv_min + profile->uv_max) * 0.5f;
  terrarium->operating_cost_cents_per_day = profile->upkeep_cents_per_day;
  terrarium->revenue_cents_per_day = profile->ticket_price_cents;
  const regulation_rule_t *rule = regulations_get_rule((int)profile->id);
  if (rule) {
    terrarium_init_dimensions_for_rule(terrarium, rule);
    terrarium->audit_locked = rule->certificate_required || rule->register_required;
    terrarium_set_compliance_message(
        terrarium, "%s | %s",
        regulations_status_to_string(rule->status),
        rule->certificate_text ? rule->certificate_text : "Pas de certificat");
  } else {
    terrarium->audit_locked = false;
    terrarium_set_compliance_message(terrarium,
                                     "Aucune règle trouvée pour %s",
                                     profile->name);
  }
  terrarium->last_update = time(NULL);
  return ESP_OK;
}

void reptile_terrarium_set_config(terrarium_t *terrarium,
                                  const reptile_terrarium_config_t *config) {
  if (!terrarium || !config) {
    return;
  }
  copy_string(terrarium->config.substrate, sizeof(terrarium->config.substrate),
              config->substrate);
  copy_string(terrarium->config.heating, sizeof(terrarium->config.heating),
              config->heating);
  copy_string(terrarium->config.decor, sizeof(terrarium->config.decor),
              config->decor);
  copy_string(terrarium->config.uv_setup, sizeof(terrarium->config.uv_setup),
              config->uv_setup);
  terrarium->needs_maintenance = false;
  terrarium->maintenance_hours = 0;
}

static esp_err_t update_config_field(char *field, size_t len,
                                     const char *value) {
  if (!field || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!value || value[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  copy_string(field, len, value);
  return ESP_OK;
}

esp_err_t reptile_terrarium_set_substrate(terrarium_t *terrarium,
                                          const char *substrate) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err =
      update_config_field(terrarium->config.substrate,
                          sizeof(terrarium->config.substrate), substrate);
  if (err == ESP_OK) {
    terrarium->needs_maintenance = false;
    terrarium->maintenance_hours = 0;
  }
  return err;
}

esp_err_t reptile_terrarium_set_heating(terrarium_t *terrarium,
                                        const char *heating) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  return update_config_field(terrarium->config.heating,
                             sizeof(terrarium->config.heating), heating);
}

esp_err_t reptile_terrarium_set_decor(terrarium_t *terrarium,
                                      const char *decor) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = update_config_field(
      terrarium->config.decor, sizeof(terrarium->config.decor), decor);
  if (err == ESP_OK) {
    terrarium->needs_maintenance = false;
  }
  return err;
}

esp_err_t reptile_terrarium_set_uv(terrarium_t *terrarium, const char *uv) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  return update_config_field(terrarium->config.uv_setup,
                             sizeof(terrarium->config.uv_setup), uv);
}

esp_err_t reptile_terrarium_add_certificate(
    terrarium_t *terrarium, const reptile_certificate_t *certificate) {
  if (!terrarium || !certificate) {
    return ESP_ERR_INVALID_ARG;
  }
  if (terrarium->certificate_count >= REPTILE_MAX_CERTIFICATES) {
    return ESP_ERR_NO_MEM;
  }
  terrarium->certificates[terrarium->certificate_count++] = *certificate;
  return ESP_OK;
}

esp_err_t reptile_terrarium_set_dimensions(terrarium_t *terrarium,
                                           float length_cm, float width_cm,
                                           float height_cm) {
  if (!terrarium || length_cm <= 0.f || width_cm <= 0.f || height_cm <= 0.f) {
    return ESP_ERR_INVALID_ARG;
  }
  const regulation_rule_t *rule =
      regulations_get_rule((int)terrarium->species.id);
  if (rule) {
    if (length_cm < rule->min_length_cm || width_cm < rule->min_width_cm ||
        height_cm < rule->min_height_cm) {
      return ESP_ERR_INVALID_ARG;
    }
  }
  terrarium->config.length_cm = length_cm;
  terrarium->config.width_cm = width_cm;
  terrarium->config.height_cm = height_cm;
  return ESP_OK;
}

void reptile_terrarium_set_education(terrarium_t *terrarium, bool present) {
  if (!terrarium) {
    return;
  }
  terrarium->config.educational_panel_present = present;
}

esp_err_t reptile_terrarium_set_register(terrarium_t *terrarium,
                                         bool recorded,
                                         const char *reference) {
  if (!terrarium) {
    return ESP_ERR_INVALID_ARG;
  }
  terrarium->config.register_completed = recorded;
  if (recorded) {
    if (!reference || reference[0] == '\0') {
      return ESP_ERR_INVALID_ARG;
    }
    copy_string(terrarium->config.register_reference,
                sizeof(terrarium->config.register_reference), reference);
  } else {
    terrarium->config.register_reference[0] = '\0';
  }
  return ESP_OK;
}

esp_err_t reptile_facility_export_regulation_report(
    const reptile_facility_t *facility, const char *relative_path) {
  if (!facility) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ready = ensure_storage_ready("export réglementaire");
  if (ready != ESP_OK) {
    return ready;
  }

  char reports_dir[128];
  snprintf(reports_dir, sizeof(reports_dir), "%s/reports", MOUNT_POINT);
  if (mkdir(reports_dir, 0777) != 0 && errno != EEXIST) {
    return ESP_FAIL;
  }

  char path[256];
  if (relative_path && relative_path[0] != '\0') {
    if (relative_path[0] == '/') {
      snprintf(path, sizeof(path), "%s", relative_path);
    } else {
      snprintf(path, sizeof(path), "%s/%s", reports_dir, relative_path);
    }
  } else {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm_info);
    snprintf(path, sizeof(path), "%s/compliance_%s.csv", reports_dir, stamp);
  }

  FILE *f = fopen(path, "w");
  if (!f) {
    return ESP_FAIL;
  }

  fprintf(f,
          "terrarium;espece;statut;dimensions_cm;certificat;registre;education;incident;message\n");

  for (uint32_t i = 0; i < facility->terrarium_count; ++i) {
    const terrarium_t *terrarium = &facility->terrariums[i];
    if (!terrarium->occupied) {
      continue;
    }
    const regulation_rule_t *rule =
        regulations_get_rule((int)terrarium->species.id);
    bool expired = false;
    bool cert_ok = certificates_valid(terrarium, time(NULL), &expired);
    regulations_compliance_input_t input = {
        .length_cm = terrarium->config.length_cm,
        .width_cm = terrarium->config.width_cm,
        .height_cm = terrarium->config.height_cm,
        .temperature_c = terrarium->temperature_c,
        .humidity_pct = terrarium->humidity_pct,
        .uv_index = terrarium->uv_index,
        .is_daytime = facility->cycle.is_daytime,
        .certificate_count = terrarium->certificate_count,
        .certificate_valid = cert_ok,
        .certificate_expired = expired,
        .register_present = terrarium->config.register_completed,
        .education_present = terrarium->config.educational_panel_present,
    };
    regulations_compliance_report_t report = {0};
    if (rule) {
      regulations_evaluate(rule, &input, &report);
    }

    const char *status =
        rule ? regulations_status_to_string(rule->status) : "Non défini";
    const char *incident_str = "Aucun";
    switch (terrarium->incident) {
    case REPTILE_INCIDENT_CERTIFICATE_MISSING:
      incident_str = "Certificat manquant";
      break;
    case REPTILE_INCIDENT_CERTIFICATE_EXPIRED:
      incident_str = "Certificat expiré";
      break;
    case REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE:
      incident_str = "Environnement non conforme";
      break;
    case REPTILE_INCIDENT_REGISTER_MISSING:
      incident_str = "Registre absent";
      break;
    case REPTILE_INCIDENT_DIMENSION_NON_CONFORM:
      incident_str = "Dimensions insuffisantes";
      break;
    case REPTILE_INCIDENT_EDUCATION_MISSING:
      incident_str = "Pédagogie manquante";
      break;
    case REPTILE_INCIDENT_AUDIT_LOCK:
      incident_str = "Blocage administratif";
      break;
    case REPTILE_INCIDENT_NONE:
    default:
      break;
    }

    fprintf(f,
            "T%02lu;%s;%s;%.0fx%.0fx%.0f;%s;%s;%s;%s;%s\n",
            (unsigned long)(i + 1U), terrarium->species.name, status,
            terrarium->config.length_cm, terrarium->config.width_cm,
            terrarium->config.height_cm,
            (rule && rule->certificate_required) ? (report.certificate_ok ?
                                                   "OK"
                                                   : "À vérifier")
                                                 : "Non requis",
            terrarium->config.register_completed ? "OK" : "À compléter",
            terrarium->config.educational_panel_present ? "OK"
                                                        : "À afficher",
            incident_str,
            terrarium->compliance_message[0] != '\0'
                ? terrarium->compliance_message
                : "");
  }

  fclose(f);
  return ESP_OK;
}

void reptile_inventory_add_feed(reptile_facility_t *facility,
                                uint32_t quantity) {
  if (!facility || quantity == 0) {
    return;
  }
  facility->inventory.feeders += quantity;
  int64_t cost = (int64_t)quantity * COST_FEEDING_CENTS;
  facility->economy.daily_expenses_cents += cost;
  facility->economy.cash_cents -= cost;
}

void reptile_inventory_add_substrate(reptile_facility_t *facility,
                                     uint32_t quantity) {
  if (!facility || quantity == 0) {
    return;
  }
  facility->inventory.substrate_bags += quantity;
  int64_t cost = (int64_t)quantity * COST_SUBSTRATE_CENTS;
  facility->economy.daily_expenses_cents += cost;
  facility->economy.cash_cents -= cost;
}

void reptile_inventory_add_uv_bulbs(reptile_facility_t *facility,
                                    uint32_t quantity) {
  if (!facility || quantity == 0) {
    return;
  }
  facility->inventory.uv_bulbs += quantity;
  int64_t cost = (int64_t)quantity * COST_UV_BULB_CENTS;
  facility->economy.daily_expenses_cents += cost;
  facility->economy.cash_cents -= cost;
}

void reptile_inventory_add_decor(reptile_facility_t *facility,
                                 uint32_t quantity) {
  if (!facility || quantity == 0) {
    return;
  }
  facility->inventory.decor_kits += quantity;
  int64_t cost = (int64_t)quantity * COST_DECOR_KIT_CENTS;
  facility->economy.daily_expenses_cents += cost;
  facility->economy.cash_cents -= cost;
}

void reptile_inventory_add_water(reptile_facility_t *facility,
                                 uint32_t liters) {
  if (!facility || liters == 0) {
    return;
  }
  facility->inventory.water_reserve_l += liters;
  int64_t cost = (int64_t)liters * COST_WATER_CENTS;
  facility->economy.daily_expenses_cents += cost;
  facility->economy.cash_cents -= cost;
}

