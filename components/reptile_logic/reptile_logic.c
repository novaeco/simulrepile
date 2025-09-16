#include "reptile_logic.h"
#include "esp_log.h"
#include "esp_random.h"
#include "gpio.h"
#include "sensors.h"
#include "sd.h"
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "reptile_logic";
static bool s_sensors_ready = false;
static bool s_simulation_mode = false;
static bool log_once = false;

static const char *get_save_path(void) {
  static char path[64];
  const char *base = MOUNT_POINT;
  if (s_simulation_mode) {
    snprintf(path, sizeof(path), "%s/sim/reptile_state.bin", base);
  } else {
    snprintf(path, sizeof(path), "%s/real/reptile_state.bin", base);
  }
  return path;
}

static void reptile_set_defaults(reptile_t *r);

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

static void reptile_set_defaults(reptile_t *r) {
  r->faim = 100;
  r->eau = 100;
  r->temperature = 30;
  r->humidite = 50;
  r->humeur = 100;
  r->event = REPTILE_EVENT_NONE;
  r->last_update = time(NULL);
}

void reptile_update(reptile_t *r, uint32_t elapsed_ms) {
  if (!r) {
    return;
  }

  uint32_t decay = elapsed_ms / 1000U; /* 1 point per second */

  r->faim = (r->faim > decay) ? (r->faim - decay) : 0;
  r->eau = (r->eau > decay) ? (r->eau - decay) : 0;
  r->humeur = (r->humeur > decay) ? (r->humeur - decay) : 0;

  if (s_simulation_mode) {
    uint32_t randv = esp_random();
    float temp = 26.0f + (float)(randv % 80) / 10.0f; /* 26.0 - 33.9 */
    randv = esp_random();
    float hum = 40.0f + (float)(randv % 200) / 10.0f; /* 40.0 - 59.9 */
    r->temperature = (uint32_t)temp;
    r->humidite = (uint32_t)hum;
  } else if (s_sensors_ready) {

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

  r->last_update += (time_t)decay;
}

esp_err_t reptile_save(reptile_t *r) {
  if (!r) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *path = get_save_path();
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Impossible d'ouvrir le fichier de sauvegarde SD");
    return ESP_FAIL;
  }
  size_t written = fwrite(r, sizeof(reptile_t), 1, f);
  fclose(f);
  if (written != 1) {
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
  size_t read = fread(r, 1, sizeof(reptile_t), f);
  fclose(f);
  return (read == sizeof(reptile_t)) ? ESP_OK : ESP_FAIL;

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
  r->temperature = (r->temperature + 5 > 50) ? 50 : r->temperature + 5;
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

  if (r->faim <= REPTILE_FAMINE_THRESHOLD || r->eau <= REPTILE_EAU_THRESHOLD ||
      r->temperature <= REPTILE_TEMP_THRESHOLD_LOW ||
      r->temperature >= REPTILE_TEMP_THRESHOLD_HIGH ||
      r->humeur <= REPTILE_HUMEUR_THRESHOLD) {
    evt = REPTILE_EVENT_MALADIE;
  } else if (r->faim >= 90 && r->eau >= 90 && r->humeur >= 90 &&
             r->temperature > REPTILE_TEMP_THRESHOLD_LOW &&
             r->temperature < REPTILE_TEMP_THRESHOLD_HIGH) {
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
