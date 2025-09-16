#ifndef REPTILE_LOGIC_H
#define REPTILE_LOGIC_H

#include "esp_err.h"
#include "species_db.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  REPTILE_EVENT_NONE = 0,
  REPTILE_EVENT_MALADIE,
  REPTILE_EVENT_CROISSANCE,
} reptile_event_t;

typedef enum {
  REPTILE_FAMINE_THRESHOLD = 30,
  REPTILE_EAU_THRESHOLD = 30,
  REPTILE_HUMEUR_THRESHOLD = 40,
} reptile_threshold_t;

typedef struct {
  uint16_t temperature_min_c;
  uint16_t temperature_max_c;
  uint8_t humidity_min_pct;
  uint8_t humidity_max_pct;
  uint8_t uv_index_min;
  uint8_t uv_index_max;
} reptile_environment_thresholds_t;

#define REPTILE_MINUTES_PER_DAY (24U * 60U)
#define REPTILE_MS_PER_MINUTE 60000U

typedef struct {
  int32_t solde;
  int32_t revenus_hebdomadaires;
  int32_t depenses;
} reptile_economy_t;

typedef struct {
  uint32_t minutes_of_day;
  uint32_t minute_ms;
  uint32_t days;
} reptile_clock_t;

#define REPTILE_DEFAULT_TEMP_MIN_C 26U
#define REPTILE_DEFAULT_TEMP_MAX_C 34U
#define REPTILE_DEFAULT_HUM_MIN 40U
#define REPTILE_DEFAULT_HUM_MAX 60U
#define REPTILE_DEFAULT_UV_MIN 2U
#define REPTILE_DEFAULT_UV_MAX 5U

#define REPTILE_SPECIES_ID_MAX_LEN SPECIES_DB_ID_MAX_LEN

typedef struct {
  uint32_t faim;
  uint32_t eau;
  uint32_t temperature;
  uint32_t humidite; /* pourcentage d'humidite */
  uint32_t uv_index;
  uint32_t humeur;
  reptile_event_t event;
  time_t last_update;
  reptile_environment_thresholds_t thresholds;
  char species_id[REPTILE_SPECIES_ID_MAX_LEN];
  reptile_economy_t economy;
  reptile_clock_t clock;
} reptile_t;

enum { REPTILE_SLOT_NAME_MAX = 64 };

esp_err_t reptile_init(reptile_t *r, bool simulation);
void reptile_update(reptile_t *r, uint32_t elapsed_ms);
esp_err_t reptile_load(reptile_t *r);
esp_err_t reptile_save(reptile_t *r);
esp_err_t reptile_select_save(const char *slot_name, bool simulation);
void reptile_feed(reptile_t *r);
void reptile_give_water(reptile_t *r);
void reptile_heat(reptile_t *r);
void reptile_soothe(reptile_t *r);
reptile_event_t reptile_check_events(reptile_t *r);
bool reptile_sensors_available(void);
void reptile_get_thresholds(const reptile_t *r,
                           reptile_environment_thresholds_t *out);
bool reptile_cycle_step(reptile_t *r, uint32_t elapsed_ms);
uint32_t reptile_clock_minutes_of_day(const reptile_t *r);
uint32_t reptile_clock_days(const reptile_t *r);
esp_err_t reptile_apply_species_profile(reptile_t *r,
                                        const species_db_entry_t *species);
esp_err_t reptile_clear_species_profile(reptile_t *r);
const char *reptile_get_species_id(const reptile_t *r);

#ifdef __cplusplus
}
#endif

#endif // REPTILE_LOGIC_H
