#include "reptile_logic.h"
#include "esp_log.h"
#include "gpio.h"
#include "sensors.h"
#include "sd.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static const char *TAG = "reptile_logic";
static bool s_sensors_ready = false;
static bool s_simulation_mode = false;
static bool log_once = false;

#define REPTILE_MS_PER_DAY ((uint32_t)REPTILE_MINUTES_PER_DAY * REPTILE_MS_PER_MINUTE)

static const double REPTILE_PI = 3.14159265358979323846;
static const double REPTILE_TWO_PI = 6.28318530717958647692;

static char s_sim_slot[REPTILE_SLOT_NAME_MAX] = "reptile_state.bin";
static char s_real_slot[REPTILE_SLOT_NAME_MAX] = "reptile_state.bin";

static const char *slot_cfg_filename = "slot.cfg";

static void apply_default_slot(bool simulation);
static void load_slot_cfg(bool simulation);
static esp_err_t write_slot_cfg(bool simulation);
static char *get_slot_buffer(bool simulation);
static const char *get_slot_cfg_path(bool simulation);

static const char *get_save_path(void) {
  static char path[96];
  const char *base = MOUNT_POINT;
  const char *slot = s_simulation_mode ? s_sim_slot : s_real_slot;
  const char *mode_dir = s_simulation_mode ? "sim" : "real";
  if (!slot || slot[0] == '\0') {
    slot = "reptile_state.bin";
  }
  snprintf(path, sizeof(path), "%s/%s/%s", base, mode_dir, slot);
  return path;
}

static void reptile_set_defaults(reptile_t *r);
static void reptile_reset_thresholds(reptile_t *r);
static void reptile_apply_thresholds(reptile_t *r,
                                     const reptile_environment_thresholds_t *t);
static uint32_t clamp_u32(uint32_t value, uint32_t min_value,
                          uint32_t max_value);
static uint32_t midpoint_u32(uint32_t min_value, uint32_t max_value);
static void reptile_normalize_clock(reptile_clock_t *clock);
static uint32_t to_u32_positive(double value);

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
} reptile_save_header_t;

typedef struct {
  reptile_save_header_t header;
  reptile_t reptile;
} reptile_save_v2_t;

typedef struct {
  uint32_t faim;
  uint32_t eau;
  uint32_t temperature;
  uint32_t humidite;
  uint32_t uv_index;
  uint32_t humeur;
  reptile_event_t event;
  time_t last_update;
  reptile_environment_thresholds_t thresholds;
  char species_id[REPTILE_SPECIES_ID_MAX_LEN];
} reptile_save_legacy_t;

#define REPTILE_SAVE_MAGIC 0x5250544CUL /* 'RPTL' */
#define REPTILE_SAVE_VERSION 2U

esp_err_t reptile_init(reptile_t *r, bool simulation) {
  if (!r) {
    return ESP_ERR_INVALID_ARG;
  }

  const char *base = MOUNT_POINT;
  char sim_dir[64];
  char real_dir[64];
  snprintf(sim_dir, sizeof(sim_dir), "%s/sim", base);
  snprintf(real_dir, sizeof(real_dir), "%s/real", base);
  if (mkdir(sim_dir, 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "Création du répertoire %s échouée", sim_dir);
  }
  if (mkdir(real_dir, 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "Création du répertoire %s échouée", real_dir);
  }

  s_simulation_mode = simulation;
  load_slot_cfg(simulation);
  if (!simulation) {
    esp_err_t err = sensors_init();
    if (err == ESP_OK) {
      s_sensors_ready = true;
    } else {
      ESP_LOGW(TAG, "Capteurs non initialisés");
      s_sensors_ready = false;
    }
  } else {
    s_sensors_ready = false;
  }

  reptile_set_defaults(r);

  return ESP_OK;
}

static char *get_slot_buffer(bool simulation) {
  return simulation ? s_sim_slot : s_real_slot;
}

static void apply_default_slot(bool simulation) {
  char *buf = get_slot_buffer(simulation);
  strncpy(buf, "reptile_state.bin", REPTILE_SLOT_NAME_MAX - 1);
  buf[REPTILE_SLOT_NAME_MAX - 1] = '\0';
}

static const char *get_slot_cfg_path(bool simulation) {
  static char path[96];
  snprintf(path, sizeof(path), "%s/%s/%s", MOUNT_POINT,
           simulation ? "sim" : "real", slot_cfg_filename);
  return path;
}

static esp_err_t write_slot_cfg(bool simulation) {
  const char *cfg_path = get_slot_cfg_path(simulation);
  FILE *f = fopen(cfg_path, "w");
  if (!f) {
    ESP_LOGW(TAG, "Impossible de persister le slot actif (%s)", cfg_path);
    return ESP_FAIL;
  }
  const char *slot = get_slot_buffer(simulation);
  if (!slot || slot[0] == '\0') {
    slot = "reptile_state.bin";
  }
  int res = fprintf(f, "%s\n", slot);
  fclose(f);
  if (res < 0) {
    ESP_LOGW(TAG, "Échec d'écriture du slot actif (%s)", cfg_path);
    return ESP_FAIL;
  }
  return ESP_OK;
}

static void load_slot_cfg(bool simulation) {
  char *buf = get_slot_buffer(simulation);
  apply_default_slot(simulation);
  const char *cfg_path = get_slot_cfg_path(simulation);
  FILE *f = fopen(cfg_path, "r");
  if (!f) {
    (void)write_slot_cfg(simulation);
    return;
  }
  if (fgets(buf, REPTILE_SLOT_NAME_MAX, f)) {
    size_t len = strcspn(buf, "\r\n");
    buf[len] = '\0';
    if (buf[0] == '\0') {
      apply_default_slot(simulation);
    }
  } else {
    apply_default_slot(simulation);
  }
  fclose(f);
}

esp_err_t reptile_select_save(const char *slot_name, bool simulation) {
  if (!slot_name || slot_name[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (strchr(slot_name, '/') || strchr(slot_name, '\\')) {
    return ESP_ERR_INVALID_ARG;
  }
  char *dest = get_slot_buffer(simulation);
  size_t len = strlen(slot_name);
  if (len >= REPTILE_SLOT_NAME_MAX) {
    len = REPTILE_SLOT_NAME_MAX - 1;
  }
  memcpy(dest, slot_name, len);
  dest[len] = '\0';
  if (dest[0] == '\0') {
    apply_default_slot(simulation);
  }
  return write_slot_cfg(simulation);
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value,
                          uint32_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static uint32_t midpoint_u32(uint32_t min_value, uint32_t max_value) {
  if (max_value < min_value) {
    return min_value;
  }
  return min_value + (max_value - min_value) / 2U;
}

static void reptile_normalize_clock(reptile_clock_t *clock) {
  if (!clock) {
    return;
  }
  uint64_t total_ms = (uint64_t)clock->minutes_of_day * REPTILE_MS_PER_MINUTE +
                      (uint64_t)clock->minute_ms;
  if (total_ms >= REPTILE_MS_PER_DAY) {
    uint32_t extra_days = (uint32_t)(total_ms / REPTILE_MS_PER_DAY);
    clock->days += extra_days;
    total_ms -= (uint64_t)extra_days * REPTILE_MS_PER_DAY;
  }
  clock->minutes_of_day = (uint32_t)(total_ms / REPTILE_MS_PER_MINUTE);
  clock->minute_ms = (uint32_t)(total_ms % REPTILE_MS_PER_MINUTE);
}

static uint32_t to_u32_positive(double value) {
  if (value <= 0.0) {
    return 0U;
  }
  double rounded = value + 0.5;
  if (rounded > (double)UINT32_MAX) {
    return UINT32_MAX;
  }
  return (uint32_t)rounded;
}

static void reptile_apply_thresholds(reptile_t *r,
                                     const reptile_environment_thresholds_t *t) {
  if (!r || !t) {
    return;
  }
  reptile_environment_thresholds_t th = *t;
  if (th.temperature_max_c < th.temperature_min_c) {
    th.temperature_max_c = th.temperature_min_c;
  }
  if (th.humidity_max_pct < th.humidity_min_pct) {
    th.humidity_max_pct = th.humidity_min_pct;
  }
  if (th.uv_index_max < th.uv_index_min) {
    th.uv_index_max = th.uv_index_min;
  }
  r->thresholds = th;
}

static void reptile_reset_thresholds(reptile_t *r) {
  if (!r) {
    return;
  }
  const reptile_environment_thresholds_t defaults = {
      .temperature_min_c = REPTILE_DEFAULT_TEMP_MIN_C,
      .temperature_max_c = REPTILE_DEFAULT_TEMP_MAX_C,
      .humidity_min_pct = REPTILE_DEFAULT_HUM_MIN,
      .humidity_max_pct = REPTILE_DEFAULT_HUM_MAX,
      .uv_index_min = REPTILE_DEFAULT_UV_MIN,
      .uv_index_max = REPTILE_DEFAULT_UV_MAX,
  };
  reptile_apply_thresholds(r, &defaults);
}

static void reptile_set_defaults(reptile_t *r) {
  if (!r) {
    return;
  }
  reptile_reset_thresholds(r);
  r->faim = 100;
  r->eau = 100;
  r->temperature =
      midpoint_u32(r->thresholds.temperature_min_c, r->thresholds.temperature_max_c);
  r->humidite =
      midpoint_u32(r->thresholds.humidity_min_pct, r->thresholds.humidity_max_pct);
  r->uv_index =
      midpoint_u32(r->thresholds.uv_index_min, r->thresholds.uv_index_max);
  r->humeur = 100;
  r->event = REPTILE_EVENT_NONE;
  r->last_update = time(NULL);
  r->species_id[0] = '\0';
  r->economy.solde = 0;
  r->economy.revenus_hebdomadaires = 0;
  r->economy.depenses = 0;
  r->clock.days = 0;
  r->clock.minutes_of_day = 8U * 60U;
  r->clock.minute_ms = 0;
  (void)reptile_cycle_step(r, 0);
}

void reptile_update(reptile_t *r, uint32_t elapsed_ms) {
  if (!r) {
    return;
  }

  uint32_t decay = elapsed_ms / 1000U; /* 1 point per second */

  r->faim = (r->faim > decay) ? (r->faim - decay) : 0;
  r->eau = (r->eau > decay) ? (r->eau - decay) : 0;
  r->humeur = (r->humeur > decay) ? (r->humeur - decay) : 0;

  if (s_simulation_mode || !s_sensors_ready) {
    (void)reptile_cycle_step(r, 0);
  }

  if (!s_simulation_mode && s_sensors_ready) {

    float temp = sensors_read_temperature();
    float hum = sensors_read_humidity();

    if (!isnan(temp)) {
      r->temperature = (uint32_t)temp;
    }

    if (!isnan(hum)) {
      if (hum < 0.f)
        hum = 0.f;
      else if (hum > 100.f)
        hum = 100.f;
      r->humidite = (uint32_t)hum;
    }
  } else if (!s_simulation_mode && !s_sensors_ready && !log_once) {
    ESP_LOGW(TAG, "Capteurs indisponibles");
    log_once = true;
  }

  r->temperature = clamp_u32(r->temperature, 0, 60);
  r->humidite = clamp_u32(r->humidite, 0, 100);
  r->uv_index = clamp_u32(r->uv_index, 0, 15);

  r->last_update += (time_t)decay;
}

esp_err_t reptile_save(reptile_t *r) {
  if (!r) {
    return ESP_ERR_INVALID_ARG;
  }
  reptile_normalize_clock(&r->clock);
  const char *path = get_save_path();
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Impossible d'ouvrir le fichier de sauvegarde SD");
    return ESP_FAIL;
  }
  reptile_save_v2_t blob = {
      .header = {.magic = REPTILE_SAVE_MAGIC,
                 .version = REPTILE_SAVE_VERSION,
                 .reserved = 0},
      .reptile = *r,
  };
  size_t written = fwrite(&blob, sizeof(blob), 1, f);
  fclose(f);
  if (written != 1U) {
    ESP_LOGE(TAG, "Écriture incomplète de la sauvegarde SD");
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t reptile_load(reptile_t *r) {
  if (!r) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *path = get_save_path();
  FILE *f = fopen(path, "rb");
  if (!f) {
    return ESP_FAIL;
  }
  reptile_save_header_t header;
  size_t read = fread(&header, sizeof(header), 1, f);
  if (read == 1U && header.magic == REPTILE_SAVE_MAGIC) {
    if (header.version != REPTILE_SAVE_VERSION) {
      fclose(f);
      return ESP_FAIL;
    }
    reptile_t tmp;
    if (fread(&tmp, sizeof(tmp), 1, f) != 1U) {
      fclose(f);
      return ESP_FAIL;
    }
    fclose(f);
    *r = tmp;
    reptile_normalize_clock(&r->clock);
    return ESP_OK;
  }

  /* Legacy format without header */
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return ESP_FAIL;
  }
  reptile_save_legacy_t legacy;
  if (fread(&legacy, sizeof(legacy), 1, f) != 1U) {
    fclose(f);
    return ESP_FAIL;
  }
  fclose(f);

  reptile_set_defaults(r);
  r->faim = legacy.faim;
  r->eau = legacy.eau;
  r->temperature = legacy.temperature;
  r->humidite = legacy.humidite;
  r->uv_index = legacy.uv_index;
  r->humeur = legacy.humeur;
  r->event = legacy.event;
  r->last_update = legacy.last_update;
  reptile_apply_thresholds(r, &legacy.thresholds);
  memcpy(r->species_id, legacy.species_id, sizeof(r->species_id));
  r->species_id[sizeof(r->species_id) - 1U] = '\0';
  return ESP_OK;
}

void reptile_feed(reptile_t *r) {
  if (!r) {
    return;
  }
  r->faim = (r->faim + 10 > 100) ? 100 : r->faim + 10;
  if (!s_simulation_mode) {
    /* Physically pulse the feeder servo */
    reptile_feed_gpio();
  }
  reptile_save(r);
}

void reptile_give_water(reptile_t *r) {
  if (!r) {
    return;
  }
  r->eau = (r->eau + 10 > 100) ? 100 : r->eau + 10;
  if (!s_simulation_mode) {
    /* Activate the water pump */
    reptile_water_gpio();
  }
  reptile_save(r);
}

void reptile_heat(reptile_t *r) {
  if (!r) {
    return;
  }
  uint32_t target = r->temperature + 5U;
  uint32_t max_temp = r->thresholds.temperature_max_c;
  if (max_temp < r->thresholds.temperature_min_c) {
    max_temp = r->thresholds.temperature_min_c;
  }
  r->temperature = clamp_u32(target, 0, max_temp);
  if (!s_simulation_mode) {
    /* Drive the heating resistor */
    reptile_heat_gpio();
  }
  reptile_save(r);
}

void reptile_soothe(reptile_t *r) {
  if (!r) {
    return;
  }
  /* Petting the reptile improves its mood */
  r->humeur = (r->humeur + 10 > 100) ? 100 : r->humeur + 10;
  reptile_save(r);
}

reptile_event_t reptile_check_events(reptile_t *r) {
  if (!r) {
    return REPTILE_EVENT_NONE;
  }

  reptile_event_t evt = REPTILE_EVENT_NONE;

  const reptile_environment_thresholds_t *th = &r->thresholds;
  bool temp_out_of_range =
      (r->temperature < th->temperature_min_c) ||
      (r->temperature > th->temperature_max_c);
  bool hum_out_of_range =
      (r->humidite < th->humidity_min_pct) ||
      (r->humidite > th->humidity_max_pct);
  bool uv_out_of_range =
      (r->uv_index < th->uv_index_min) ||
      (r->uv_index > th->uv_index_max);

  if (r->faim <= REPTILE_FAMINE_THRESHOLD || r->eau <= REPTILE_EAU_THRESHOLD ||
      temp_out_of_range || hum_out_of_range || uv_out_of_range ||
      r->humeur <= REPTILE_HUMEUR_THRESHOLD) {
    evt = REPTILE_EVENT_MALADIE;
  } else if (r->faim >= 90 && r->eau >= 90 && r->humeur >= 90 &&
             !temp_out_of_range && !hum_out_of_range && !uv_out_of_range) {
    evt = REPTILE_EVENT_CROISSANCE;
  }

  if (evt != r->event) {
    r->event = evt;
  }

  return evt;
}

bool reptile_sensors_available(void) {
  return !s_simulation_mode && s_sensors_ready;
}

void reptile_get_thresholds(const reptile_t *r,
                           reptile_environment_thresholds_t *out) {
  if (!r || !out) {
    return;
  }
  *out = r->thresholds;
}

bool reptile_cycle_step(reptile_t *r, uint32_t elapsed_ms) {
  if (!r) {
    return false;
  }

  reptile_clock_t *clock = &r->clock;
  uint32_t prev_days = clock->days;
  uint32_t prev_minutes = clock->minutes_of_day;

  reptile_normalize_clock(clock);

  uint64_t day_ms = (uint64_t)clock->minutes_of_day * REPTILE_MS_PER_MINUTE +
                    (uint64_t)clock->minute_ms;
  day_ms += (uint64_t)elapsed_ms;
  uint32_t added_days = (uint32_t)(day_ms / REPTILE_MS_PER_DAY);
  uint64_t remainder_ms = day_ms % REPTILE_MS_PER_DAY;
  clock->days += added_days;
  clock->minutes_of_day = (uint32_t)(remainder_ms / REPTILE_MS_PER_MINUTE);
  clock->minute_ms = (uint32_t)(remainder_ms % REPTILE_MS_PER_MINUTE);

  bool time_changed = (clock->minutes_of_day != prev_minutes) ||
                      (clock->days != prev_days);

  bool env_changed = false;
  if (s_simulation_mode || !s_sensors_ready) {
    const reptile_environment_thresholds_t *th = &r->thresholds;
    double day_fraction = (double)remainder_ms / (double)REPTILE_MS_PER_DAY;
    double cos_phase = cos(day_fraction * REPTILE_TWO_PI - REPTILE_PI);

    double temp_mid =
        ((double)th->temperature_min_c + (double)th->temperature_max_c) * 0.5;
    double temp_amp =
        ((double)th->temperature_max_c - (double)th->temperature_min_c) * 0.5;
    double hum_mid =
        ((double)th->humidity_min_pct + (double)th->humidity_max_pct) * 0.5;
    double hum_amp =
        ((double)th->humidity_max_pct - (double)th->humidity_min_pct) * 0.5;
    double uv_base = (double)th->uv_index_min;
    double uv_span = (double)(th->uv_index_max - th->uv_index_min);

    double temp_value = temp_mid + temp_amp * cos_phase;
    double hum_value = hum_mid - hum_amp * cos_phase;
    double daylight = cos_phase;
    if (daylight < 0.0) {
      daylight = 0.0;
    }
    double uv_value = uv_base + uv_span * daylight;

    uint32_t new_temp = clamp_u32(to_u32_positive(temp_value),
                                  th->temperature_min_c,
                                  th->temperature_max_c);
    uint32_t new_hum = clamp_u32(to_u32_positive(hum_value),
                                 th->humidity_min_pct,
                                 th->humidity_max_pct);
    uint32_t new_uv = clamp_u32(to_u32_positive(uv_value), th->uv_index_min,
                                th->uv_index_max);

    if (new_temp != r->temperature) {
      r->temperature = new_temp;
      env_changed = true;
    }
    if (new_hum != r->humidite) {
      r->humidite = new_hum;
      env_changed = true;
    }
    if (new_uv != r->uv_index) {
      r->uv_index = new_uv;
      env_changed = true;
    }
  }

  return env_changed || time_changed;
}

uint32_t reptile_clock_minutes_of_day(const reptile_t *r) {
  if (!r) {
    return 0U;
  }
  return r->clock.minutes_of_day % REPTILE_MINUTES_PER_DAY;
}

uint32_t reptile_clock_days(const reptile_t *r) {
  if (!r) {
    return 0U;
  }
  return r->clock.days;
}

esp_err_t reptile_apply_species_profile(reptile_t *r,
                                        const species_db_entry_t *species) {
  if (!r || !species) {
    return ESP_ERR_INVALID_ARG;
  }
  reptile_environment_thresholds_t thresholds = {
      .temperature_min_c = species->environment.temperature_min_c,
      .temperature_max_c = species->environment.temperature_max_c,
      .humidity_min_pct = species->environment.humidity_min_pct,
      .humidity_max_pct = species->environment.humidity_max_pct,
      .uv_index_min = species->environment.uv_index_min,
      .uv_index_max = species->environment.uv_index_max,
  };
  reptile_apply_thresholds(r, &thresholds);

  if (species->id) {
    strncpy(r->species_id, species->id, sizeof(r->species_id) - 1U);
    r->species_id[sizeof(r->species_id) - 1U] = '\0';
  } else {
    r->species_id[0] = '\0';
  }

  r->temperature =
      clamp_u32(r->temperature, r->thresholds.temperature_min_c,
                r->thresholds.temperature_max_c);
  r->humidite = clamp_u32(r->humidite, r->thresholds.humidity_min_pct,
                          r->thresholds.humidity_max_pct);
  uint32_t uv_mid =
      midpoint_u32(r->thresholds.uv_index_min, r->thresholds.uv_index_max);
  r->uv_index = clamp_u32(uv_mid, r->thresholds.uv_index_min,
                          r->thresholds.uv_index_max);
  (void)reptile_cycle_step(r, 0);
  return ESP_OK;
}

esp_err_t reptile_clear_species_profile(reptile_t *r) {
  if (!r) {
    return ESP_ERR_INVALID_ARG;
  }
  reptile_reset_thresholds(r);
  r->species_id[0] = '\0';
  r->temperature =
      midpoint_u32(r->thresholds.temperature_min_c, r->thresholds.temperature_max_c);
  r->humidite =
      midpoint_u32(r->thresholds.humidity_min_pct, r->thresholds.humidity_max_pct);
  r->uv_index =
      midpoint_u32(r->thresholds.uv_index_min, r->thresholds.uv_index_max);
  (void)reptile_cycle_step(r, 0);
  return ESP_OK;
}

const char *reptile_get_species_id(const reptile_t *r) {
  if (!r) {
    return NULL;
  }
  return r->species_id;
}
