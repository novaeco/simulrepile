#include "logging.h"
#include "sd.h"
#include "esp_log.h"
#include "lvgl.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#define LOG_TAG "logging"

static const reptile_facility_t *(*facility_cb)(void);
static lv_timer_t *log_timer;

static const char *LOG_FILE = "/sdcard/reptile_log.csv";

static void logging_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!facility_cb) {
        return;
    }
    const reptile_facility_t *facility = facility_cb();
    if (!facility) {
        return;
    }
    reptile_facility_metrics_t metrics;
    reptile_facility_compute_metrics(facility, &metrics);

    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        ESP_LOGE(LOG_TAG, "Failed to open log file");
        return;
    }
    time_t now = time(NULL);
    fprintf(f,
            "%ld,%s,%lld,%u,%u,%u,%u,%lld,%lld,%lld\n",
            (long)now,
            facility->slot,
            (long long)facility->economy.cash_cents,
            metrics.occupied,
            facility->alerts_active,
            facility->pathology_active,
            facility->compliance_alerts,
            (long long)facility->economy.daily_income_cents,
            (long long)facility->economy.daily_expenses_cents,
            (long long)facility->economy.fines_cents);
    fflush(f);
    if (ferror(f)) {
        ESP_LOGE(LOG_TAG, "Failed to write log file");
        fclose(f);
        sd_mmc_unmount();
        sd_mmc_init();
        return;
    }
    fclose(f);
}

void logging_init(const reptile_facility_t *(*cb)(void))
{
    facility_cb = cb;
    struct stat st;
    bool need_header = stat(LOG_FILE, &st) != 0;
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        ESP_LOGE(LOG_TAG, "Failed to create log file");
        return;
    }
    if (need_header) {
        fputs("timestamp,slot,cash_cents,occupied,alerts,pathologies,compliance,"
              "daily_income_cents,daily_expenses_cents,fines_cents\n",
              f);
        fflush(f);
        if (ferror(f)) {
            ESP_LOGE(LOG_TAG, "Failed to write header to log file");
            fclose(f);
            sd_mmc_unmount();
            sd_mmc_init();
            return;
        }
    }
    fclose(f);
    log_timer = lv_timer_create(logging_timer_cb, 60000, NULL);
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

