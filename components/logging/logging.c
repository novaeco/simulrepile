#include "logging.h"
#include "sd.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "logging"

#define LOG_DEFAULT_PERIOD_MS 60000U
#define LOG_REAL_DIR "/sdcard/real"
#define LOG_REAL_FILE LOG_REAL_DIR "/logs.csv"
#define LOG_SIM_FILE  "/sdcard/reptile_log.csv"

#define POWER_HEATING_W 75.0f
#define POWER_PUMP_W    15.0f
#define POWER_FAN_W     8.0f
#define POWER_UV_W      25.0f
#define POWER_LIGHT_W   18.0f
#define POWER_FEED_W    3.0f

static logging_provider_t s_provider;
static bool s_provider_valid;
static bool s_real_mode;
static lv_timer_t *log_timer;
static uint32_t s_period_ms = LOG_DEFAULT_PERIOD_MS;
static const char *s_log_file = LOG_SIM_FILE;

static float compute_power(const logging_real_sample_t *sample)
{
    float power = 0.0f;
    if (sample->heating)
        power += POWER_HEATING_W;
    if (sample->pumping)
        power += POWER_PUMP_W;
    if (sample->fan)
        power += POWER_FAN_W;
    if (sample->uv_lamp)
        power += POWER_UV_W;
    if (sample->light)
        power += POWER_LIGHT_W;
    if (sample->feeding)
        power += POWER_FEED_W;
    return power;
}

static bool prepare_log_file(void)
{
    if (s_real_mode) {
        mkdir(LOG_REAL_DIR, 0775);
    }
    struct stat st;
    bool need_header = stat(s_log_file, &st) != 0;
    FILE *f = fopen(s_log_file, "a");
    if (!f) {
        ESP_LOGE(LOG_TAG, "Failed to open log file %s", s_log_file);
        return false;
    }
    if (need_header) {
        if (s_real_mode) {
            fputs("timestamp,temp_c,humidity_pct,lux,uva_mWcm2,uvb_mWcm2,uv_index,heating,pumping,fan,uv_lamp,light,feeding,power_w,energy_Wh\n",
                  f);
        } else {
            fputs("timestamp,faim,eau,temperature,humeur,event\n", f);
        }
        fflush(f);
        if (ferror(f)) {
            ESP_LOGE(LOG_TAG, "Failed to write header to log file");
            fclose(f);
            sd_mmc_unmount();
            sd_mmc_init();
            return false;
        }
    }
    fclose(f);
    return true;
}

static void handle_write_error(FILE *f)
{
    if (f) {
        fclose(f);
    }
    sd_mmc_unmount();
    sd_mmc_init();
}

static void logging_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_provider_valid) {
        return;
    }

    if (s_real_mode) {
        if (!s_provider.get_real_sample) {
            return;
        }
        logging_real_sample_t sample;
        if (!s_provider.get_real_sample(&sample)) {
            return;
        }
        FILE *f = fopen(s_log_file, "a");
        if (!f) {
            ESP_LOGE(LOG_TAG, "Failed to open log file %s", s_log_file);
            return;
        }
        float power = compute_power(&sample);
        float energy = power * ((float)s_period_ms / 3600000.0f);
        time_t now = time(NULL);
        long ts = (now == (time_t)-1) ? 0 : (long)now;
        fprintf(f,
                "%ld,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%.3f,%.3f\n",
                ts,
                sample.temperature_c,
                sample.humidity_pct,
                sample.lux,
                sample.uva_mw_cm2,
                sample.uvb_mw_cm2,
                sample.uv_index,
                sample.heating ? 1 : 0,
                sample.pumping ? 1 : 0,
                sample.fan ? 1 : 0,
                sample.uv_lamp ? 1 : 0,
                sample.light ? 1 : 0,
                sample.feeding ? 1 : 0,
                power,
                energy);
        fflush(f);
        if (ferror(f)) {
            ESP_LOGE(LOG_TAG, "Failed to write log file");
            handle_write_error(f);
            return;
        }
        fclose(f);
    } else {
        if (!s_provider.get_reptile_state) {
            return;
        }
        const reptile_t *r = s_provider.get_reptile_state();
        if (!r) {
            return;
        }
        FILE *f = fopen(s_log_file, "a");
        if (!f) {
            ESP_LOGE(LOG_TAG, "Failed to open log file %s", s_log_file);
            return;
        }
        fprintf(f,
                "%ld,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n",
                (long)r->last_update,
                r->faim,
                r->eau,
                r->temperature,
                r->humeur,
                (uint32_t)r->event);
        fflush(f);
        if (ferror(f)) {
            ESP_LOGE(LOG_TAG, "Failed to write log file");
            handle_write_error(f);
            return;
        }
        fclose(f);
    }
}

void logging_init(const logging_provider_t *provider)
{
    if (log_timer) {
        lv_timer_del(log_timer);
        log_timer = NULL;
    }
    memset(&s_provider, 0, sizeof(s_provider));
    s_provider_valid = false;
    s_real_mode = false;
    s_log_file = LOG_SIM_FILE;
    s_period_ms = LOG_DEFAULT_PERIOD_MS;

    if (!provider) {
        return;
    }

    s_provider = *provider;
    s_provider_valid = true;
    s_real_mode = (provider->get_real_sample != NULL);
    s_period_ms = provider->period_ms ? provider->period_ms : LOG_DEFAULT_PERIOD_MS;
    s_log_file = s_real_mode ? LOG_REAL_FILE : LOG_SIM_FILE;

    if (!prepare_log_file()) {
        s_provider_valid = false;
        return;
    }

    log_timer = lv_timer_create(logging_timer_cb, s_period_ms, NULL);
    if (!log_timer) {
        ESP_LOGE(LOG_TAG, "Failed to create logging timer");
        s_provider_valid = false;
    }
}

void logging_pause(void)
{
    if (log_timer) {
        lv_timer_pause(log_timer);
    }
}

void logging_resume(void)
{
    if (log_timer) {
        lv_timer_resume(log_timer);
    }
}

