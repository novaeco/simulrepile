#ifndef REPTILE_LOGIC_H
#define REPTILE_LOGIC_H

#include "esp_err.h"
#include "game_mode.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REPTILE_MAX_TERRARIUMS 25U
#define REPTILE_MAX_CERTIFICATES 6U
#define REPTILE_NAME_MAX_LEN 32U
#define REPTILE_CONFIG_STR_LEN 32U
#define REPTILE_CERT_AUTH_LEN 32U
#define REPTILE_CERT_ID_LEN 24U
#define REPTILE_COMPLIANCE_MSG_LEN 96U

typedef enum {
  REPTILE_GROWTH_HATCHLING = 0,
  REPTILE_GROWTH_JUVENILE,
  REPTILE_GROWTH_ADULT,
  REPTILE_GROWTH_SENIOR,
  REPTILE_GROWTH_STAGE_COUNT
} reptile_growth_stage_t;

typedef enum {
  REPTILE_PATHOLOGY_NONE = 0,
  REPTILE_PATHOLOGY_RESPIRATORY,
  REPTILE_PATHOLOGY_PARASITIC,
  REPTILE_PATHOLOGY_METABOLIC,
} reptile_pathology_t;

typedef enum {
  REPTILE_INCIDENT_NONE = 0,
  REPTILE_INCIDENT_CERTIFICATE_MISSING,
  REPTILE_INCIDENT_CERTIFICATE_EXPIRED,
  REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE,
  REPTILE_INCIDENT_REGISTER_MISSING,
  REPTILE_INCIDENT_DIMENSION_NON_CONFORM,
  REPTILE_INCIDENT_EDUCATION_MISSING,
  REPTILE_INCIDENT_AUDIT_LOCK,
} reptile_incident_t;

typedef enum {
  REPTILE_SPECIES_GECKO = 0,
  REPTILE_SPECIES_PYTHON,
  REPTILE_SPECIES_TORTOISE,
  REPTILE_SPECIES_CHAMELEON,
  REPTILE_SPECIES_CUSTOM,
  REPTILE_SPECIES_COUNT
} reptile_species_id_t;

typedef struct {
  char id[REPTILE_CERT_ID_LEN];
  char authority[REPTILE_CERT_AUTH_LEN];
  time_t issue_date;
  time_t expiry_date;
  bool valid;
} reptile_certificate_t;

typedef struct {
  reptile_species_id_t id;
  char name[REPTILE_NAME_MAX_LEN];
  float day_temp_min;
  float day_temp_max;
  float night_temp_min;
  float night_temp_max;
  float humidity_min;
  float humidity_max;
  float uv_min;
  float uv_max;
  float growth_rate_per_hour;
  float adult_weight_g;
  uint32_t lifespan_days;
  uint32_t food_per_day;
  uint32_t water_ml_per_day;
  int64_t ticket_price_cents;
  int64_t upkeep_cents_per_day;
} species_profile_t;

typedef struct {
  char substrate[REPTILE_CONFIG_STR_LEN];
  char heating[REPTILE_CONFIG_STR_LEN];
  char decor[REPTILE_CONFIG_STR_LEN];
  char uv_setup[REPTILE_CONFIG_STR_LEN];
  float length_cm;
  float width_cm;
  float height_cm;
  bool educational_panel_present;
  bool register_completed;
  char register_reference[REPTILE_CERT_ID_LEN];
} reptile_terrarium_config_t;

typedef struct {
  uint32_t feeders;          /**< Unités d'alimentation (insectes/rongeurs). */
  uint32_t supplement_doses; /**< Doses de compléments. */
  uint32_t substrate_bags;   /**< Sacs de substrat disponibles. */
  uint32_t uv_bulbs;         /**< Tubes UV de remplacement. */
  uint32_t decor_kits;       /**< Kits de décor. */
  uint32_t water_reserve_l;  /**< Réserve d'eau en litres. */
} reptile_inventory_t;

typedef struct {
  int64_t cash_cents;
  int64_t daily_income_cents;
  int64_t daily_expenses_cents;
  int64_t fines_cents;
  uint32_t days_elapsed;
  int64_t weekly_subsidy_cents;
} reptile_economy_t;

typedef struct {
  bool is_daytime;
  uint32_t day_ms;
  uint32_t night_ms;
  uint32_t elapsed_in_phase_ms;
  uint32_t cycle_index;
} reptile_day_cycle_t;

typedef struct {
  bool occupied;
  species_profile_t species;
  char nickname[REPTILE_NAME_MAX_LEN];
  reptile_terrarium_config_t config;
  reptile_certificate_t certificates[REPTILE_MAX_CERTIFICATES];
  uint8_t certificate_count;

  float temperature_c;
  float humidity_pct;
  float uv_index;
  float satiety;
  float hydration;
  float growth;
  reptile_growth_stage_t stage;
  float weight_g;
  uint32_t age_days;
  float age_fraction;
  float feed_debt;
  float water_debt;
  float uv_wear;

  reptile_pathology_t pathology;
  reptile_incident_t incident;
  float pathology_timer_h;
  float compliance_timer_h;
  bool needs_maintenance;
  bool audit_locked;
  uint32_t maintenance_hours;

  int64_t operating_cost_cents_per_day;
  int64_t revenue_cents_per_day;

  time_t last_update;
  char compliance_message[REPTILE_COMPLIANCE_MSG_LEN];
} terrarium_t;

typedef struct {
  terrarium_t terrariums[REPTILE_MAX_TERRARIUMS];
  uint8_t terrarium_count;
  reptile_inventory_t inventory;
  reptile_economy_t economy;
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
} reptile_facility_t;

typedef struct {
  uint32_t occupied;
  uint32_t free_slots;
  uint32_t pathologies;
  uint32_t incidents;
  uint32_t mature;
  float avg_growth;
} reptile_facility_metrics_t;

const species_profile_t *reptile_species_get(reptile_species_id_t id);

esp_err_t reptile_facility_init(reptile_facility_t *facility, bool simulation,
                                const char *slot_name, game_mode_t mode);
esp_err_t reptile_facility_load(reptile_facility_t *facility);
esp_err_t reptile_facility_save(const reptile_facility_t *facility);
esp_err_t reptile_facility_set_slot(reptile_facility_t *facility,
                                    const char *slot_name);
void reptile_facility_tick(reptile_facility_t *facility, uint32_t elapsed_ms);
bool reptile_facility_sensors_available(const reptile_facility_t *facility);
terrarium_t *reptile_facility_get_terrarium(reptile_facility_t *facility,
                                            uint8_t index);
const terrarium_t *reptile_facility_get_terrarium_const(
    const reptile_facility_t *facility, uint8_t index);
void reptile_facility_compute_metrics(const reptile_facility_t *facility,
                                      reptile_facility_metrics_t *out);
void reptile_facility_reset_statistics(reptile_facility_t *facility);
void reptile_facility_reset_state(reptile_facility_t *facility);

esp_err_t reptile_terrarium_set_species(terrarium_t *terrarium,
                                        const species_profile_t *profile,
                                        const char *nickname);
void reptile_terrarium_set_config(terrarium_t *terrarium,
                                  const reptile_terrarium_config_t *config);
esp_err_t reptile_terrarium_set_substrate(terrarium_t *terrarium,
                                          const char *substrate);
esp_err_t reptile_terrarium_set_heating(terrarium_t *terrarium,
                                        const char *heating);
esp_err_t reptile_terrarium_set_decor(terrarium_t *terrarium,
                                      const char *decor);
esp_err_t reptile_terrarium_set_uv(terrarium_t *terrarium, const char *uv);
esp_err_t reptile_terrarium_add_certificate(
    terrarium_t *terrarium, const reptile_certificate_t *certificate);
esp_err_t reptile_terrarium_set_dimensions(terrarium_t *terrarium,
                                           float length_cm, float width_cm,
                                           float height_cm);
void reptile_terrarium_set_education(terrarium_t *terrarium, bool present);
esp_err_t reptile_terrarium_set_register(terrarium_t *terrarium,
                                         bool recorded,
                                         const char *reference);
esp_err_t reptile_facility_export_regulation_report(
    const reptile_facility_t *facility, const char *relative_path);

void reptile_inventory_add_feed(reptile_facility_t *facility,
                                uint32_t quantity);
void reptile_inventory_add_substrate(reptile_facility_t *facility,
                                     uint32_t quantity);
void reptile_inventory_add_uv_bulbs(reptile_facility_t *facility,
                                    uint32_t quantity);
void reptile_inventory_add_decor(reptile_facility_t *facility,
                                 uint32_t quantity);
void reptile_inventory_add_water(reptile_facility_t *facility,
                                 uint32_t liters);

#ifdef __cplusplus
}
#endif

#endif // REPTILE_LOGIC_H
