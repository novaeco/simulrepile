#include "logging.h"
#include "sd.h"
#include "esp_log.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define REAL_LOG_TAG "logging_real"
#define REAL_LOG_DIR "/sdcard/real"

static FILE *s_real_files[REPTILE_ENV_MAX_TERRARIUMS];
static size_t s_real_count;
static bool s_real_active;

static void logging_real_cleanup(void)
{
    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        if (s_real_files[i]) {
            fclose(s_real_files[i]);
        }
    }
    memset(s_real_files, 0, sizeof(s_real_files));
    s_real_count = 0;
    s_real_active = false;
}

static void sanitize_name(const char *name, char *out, size_t len)
{
    size_t j = 0;
    for (size_t i = 0; name && name[i] != '\0' && j + 1 < len; ++i) {
        char c = name[i];
        if (isalnum((unsigned char)c)) {
            out[j++] = c;
        } else if (c == '_' || c == '-') {
            out[j++] = c;
        } else if (isspace((unsigned char)c)) {
            out[j++] = '_';
        }
    }
    if (j == 0) {
        strncpy(out, "terrarium", len);
        out[len - 1] = '\0';
        return;
    }
    out[j] = '\0';
}

static esp_err_t ensure_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return ESP_OK;
    }
    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }
    if (errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(REAL_LOG_TAG, "Failed to create directory %s", path);
    return ESP_FAIL;
}

static void write_header(FILE *f)
{
    fputs("timestamp,temp_c,humidity_pct,light_lux,target_temp_c,target_humidity_pct,target_light_lux,heating,pumping,uv,manual_heat,manual_pump,manual_uv,energy_heat_wh,energy_pump_wh,energy_uv_wh,total_energy_wh,alarm_flags\n",
          f);
}

esp_err_t logging_real_start(size_t terrarium_count, const reptile_env_config_t *cfg)
{
    logging_real_stop();
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (terrarium_count > REPTILE_ENV_MAX_TERRARIUMS) {
        terrarium_count = REPTILE_ENV_MAX_TERRARIUMS;
    }
    esp_err_t err = ensure_directory(REAL_LOG_DIR);
    if (err != ESP_OK) {
        return err;
    }
    memset(s_real_files, 0, sizeof(s_real_files));
    s_real_count = 0;
    s_real_active = false;

    for (size_t i = 0; i < terrarium_count; ++i) {
        char safe_name[32];
        sanitize_name(cfg->terrarium[i].name, safe_name, sizeof(safe_name));
        char path[128];
        snprintf(path, sizeof(path), REAL_LOG_DIR "/%02u_%s.csv", (unsigned)(i + 1), safe_name);
        struct stat st = {0};
        bool need_header = (stat(path, &st) != 0);
        FILE *f = fopen(path, "a");
        if (!f) {
            ESP_LOGE(REAL_LOG_TAG, "Failed to open %s", path);
            logging_real_cleanup();
            return ESP_FAIL;
        }
        if (need_header) {
            write_header(f);
            fflush(f);
        }
        s_real_files[i] = f;
    }
    s_real_count = terrarium_count;
    s_real_active = true;
    return ESP_OK;
}

void logging_real_append(size_t terrarium_index, const reptile_env_terrarium_state_t *state)
{
    if (!s_real_active || !state || terrarium_index >= s_real_count) {
        return;
    }
    FILE *f = s_real_files[terrarium_index];
    if (!f) {
        return;
    }
    time_t now = time(NULL);
    float total = state->energy_heat_Wh + state->energy_pump_Wh + state->energy_uv_Wh;
    fprintf(f,
            "%ld,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%" PRIu8 ",%" PRIu8 ",%" PRIu8 ",%" PRIu8 ",%" PRIu8 ",%" PRIu8
            ",%.3f,%.3f,%.3f,%.3f,%" PRIu32 "\n",
            (long)now,
            state->temperature_c,
            state->humidity_pct,
            state->light_lux,
            state->target_temperature_c,
            state->target_humidity_pct,
            state->target_light_lux,
            (uint8_t)state->heating,
            (uint8_t)state->pumping,
            (uint8_t)state->uv_light,
            (uint8_t)state->manual_heat,
            (uint8_t)state->manual_pump,
            (uint8_t)state->manual_uv_override,
            state->energy_heat_Wh,
            state->energy_pump_Wh,
            state->energy_uv_Wh,
            total,
            (uint32_t)state->alarm_flags);
    fflush(f);
    if (ferror(f)) {
        ESP_LOGE(REAL_LOG_TAG, "Write failure on terrarium %u log", (unsigned)(terrarium_index + 1));
        clearerr(f);
    }
}

void logging_real_stop(void)
{
    if (!s_real_active) {
        bool has_open_handles = false;
        for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
            if (s_real_files[i]) {
                has_open_handles = true;
                break;
            }
        }
        if (!has_open_handles) {
            return;
        }
    }
    logging_real_cleanup();
}

