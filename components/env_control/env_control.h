#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REPTILE_ENV_MAX_TERRARIUMS 4
#define REPTILE_ENV_HISTORY_LENGTH 720U

/** Control loop default period in milliseconds. */
#define REPTILE_ENV_DEFAULT_PERIOD_MS 1000U

/** Alarm flags exposed in reptile_env_terrarium_state_t::alarm_flags. */
typedef enum {
    REPTILE_ENV_ALARM_NONE           = 0,
    REPTILE_ENV_ALARM_SENSOR_FAILURE = (1u << 0),
    REPTILE_ENV_ALARM_TEMP_LOW       = (1u << 1),
    REPTILE_ENV_ALARM_TEMP_HIGH      = (1u << 2),
    REPTILE_ENV_ALARM_HUM_LOW        = (1u << 3),
    REPTILE_ENV_ALARM_HUM_HIGH       = (1u << 4),
} reptile_env_alarm_flags_t;

/** Simple representation of a wall-clock time of day. */
typedef struct {
    uint8_t hour;   /*!< Hour [0, 23] */
    uint8_t minute; /*!< Minute [0, 59] */
} reptile_env_time_point_t;

/** Temperature/humidity setpoints for a profile (day or night). */
typedef struct {
    float temperature_c; /*!< Target temperature in degree Celsius */
    float humidity_pct;  /*!< Target humidity in percent */
} reptile_env_profile_thresholds_t;

/** Independent hysteresis parameters for actuators. */
typedef struct {
    float heat_on_delta;     /*!< Degrees below setpoint before triggering heater */
    float heat_off_delta;    /*!< Degrees above setpoint before authorising next cycle */
    float humidity_on_delta; /*!< Percent below setpoint before triggering pump */
    float humidity_off_delta;/*!< Percent above setpoint before authorising next cycle */
} reptile_env_hysteresis_t;

/** Daily UV lighting schedule. */
typedef struct {
    bool enabled;               /*!< Enable automatic UV cycle */
    reptile_env_time_point_t on;  /*!< Daily switch-on time */
    reptile_env_time_point_t off; /*!< Daily switch-off time */
} reptile_env_uv_schedule_t;

/** Electrical characteristics of each actuator for energy monitoring. */
typedef struct {
    float heater_power_w; /*!< Rated heater power in watts */
    float pump_power_w;   /*!< Rated humidification pump power in watts */
    float uv_power_w;     /*!< Rated UV lighting power in watts */
} reptile_env_power_profile_t;

/** Configuration for a single terrarium. */
typedef struct {
    char name[32];                    /*!< Friendly name used in UI/logs */
    bool enabled;                     /*!< Skip control if disabled */
    uint8_t sensor_channel;           /*!< Index of the sensor input */
    reptile_env_time_point_t day_start;  /*!< Time of day when DAY profile starts */
    reptile_env_time_point_t night_start;/*!< Time of day when NIGHT profile starts */
    reptile_env_profile_thresholds_t day;   /*!< Thresholds applied during the day */
    reptile_env_profile_thresholds_t night; /*!< Thresholds applied during the night */
    reptile_env_hysteresis_t hysteresis;    /*!< Independent hysteresis per actuator */
    reptile_env_uv_schedule_t uv;           /*!< UV lighting schedule */
    reptile_env_power_profile_t power;      /*!< Energy accounting */
    uint32_t min_minutes_between_heat;      /*!< Minimum time between two heat cycles */
    uint32_t min_minutes_between_pump;      /*!< Minimum time between two humidification cycles */
} reptile_env_terrarium_config_t;

/** Global environment controller configuration. */
typedef struct {
    size_t terrarium_count;                                        /*!< Number of managed terrariums */
    reptile_env_terrarium_config_t terrarium[REPTILE_ENV_MAX_TERRARIUMS]; /*!< Array of per-terrarium configurations */
    uint32_t period_ms;                                            /*!< Control loop period in milliseconds */
} reptile_env_config_t;

/** Historical sample used for plotting. */
typedef struct {
    time_t timestamp;           /*!< Wall-clock timestamp */
    float temperature_c;        /*!< Measured temperature */
    float humidity_pct;         /*!< Measured humidity */
    float target_temperature_c; /*!< Active target temperature */
    float target_humidity_pct;  /*!< Active target humidity */
} reptile_env_history_entry_t;

/** Runtime state of a single terrarium. */
typedef struct {
    float temperature_c;        /*!< Last measured temperature */
    float humidity_pct;         /*!< Last measured humidity */
    float target_temperature_c; /*!< Target temperature according to schedule */
    float target_humidity_pct;  /*!< Target humidity according to schedule */
    bool  temperature_valid;    /*!< Measurement validity */
    bool  humidity_valid;       /*!< Measurement validity */
    bool  heating;              /*!< Heating actuator active */
    bool  pumping;              /*!< Humidification actuator active */
    bool  uv_light;             /*!< UV lighting active */
    bool  day_profile_active;   /*!< true if DAY profile currently applied */
    bool  manual_heat;          /*!< Last command manually triggered heat */
    bool  manual_pump;          /*!< Last command manually triggered pump */
    bool  manual_uv_override;   /*!< Manual override applied to UV lighting */
    uint32_t alarm_flags;       /*!< Combination of reptile_env_alarm_flags_t */
    float energy_heat_Wh;       /*!< Cumulated heater energy in Wh */
    float energy_pump_Wh;       /*!< Cumulated pump energy in Wh */
    float energy_uv_Wh;         /*!< Cumulated UV energy in Wh */
    time_t last_update;         /*!< Timestamp of the last control iteration */
} reptile_env_terrarium_state_t;

/** Callback invoked whenever a terrarium state is updated. */
typedef void (*reptile_env_update_cb_t)(size_t terrarium_index,
                                        const reptile_env_terrarium_state_t *state,
                                        void *user_ctx);

/**
 * @brief Fill a configuration structure with sane defaults for two terrariums.
 */
void reptile_env_get_default_config(reptile_env_config_t *cfg);

/**
 * @brief Start the environment controller.
 *
 * @param cfg      Configuration to apply (copied internally).
 * @param cb       Optional callback invoked after each terrarium update.
 * @param user_ctx User context passed back to the callback.
 */
esp_err_t reptile_env_start(const reptile_env_config_t *cfg,
                            reptile_env_update_cb_t cb,
                            void *user_ctx);

/** Stop the environment controller and release all resources. */
void reptile_env_stop(void);

/**
 * @brief Update the controller configuration at runtime.
 *
 * The new configuration is applied atomically at the next control iteration.
 */
esp_err_t reptile_env_update_config(const reptile_env_config_t *cfg);

/** @return pointer to the immutable configuration currently applied. */
const reptile_env_config_t *reptile_env_get_config(void);

/** @return number of terrariums actively managed. */
size_t reptile_env_get_terrarium_count(void);

/** Retrieve latest state snapshot for a terrarium. */
esp_err_t reptile_env_get_state(size_t terrarium_index,
                                reptile_env_terrarium_state_t *out_state);

/** Copy historical samples into the provided buffer (most recent last). */
size_t reptile_env_get_history(size_t terrarium_index,
                               reptile_env_history_entry_t *out,
                               size_t max_entries);

/** Manually trigger a humidification cycle regardless of hysteresis. */
esp_err_t reptile_env_manual_pump(size_t terrarium_index);

/** Manually trigger a heating cycle regardless of hysteresis. */
esp_err_t reptile_env_manual_heat(size_t terrarium_index);

/** Toggle UV lighting manual override for a terrarium. */
esp_err_t reptile_env_manual_uv_toggle(size_t terrarium_index);

/** Explicitly set UV lighting manual override state. */
esp_err_t reptile_env_manual_uv_set(size_t terrarium_index, bool force_on);

#ifdef __cplusplus
}
#endif

