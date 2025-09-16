#include "logging.h"
#include "sd.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <stdbool.h>

#define LOG_TAG "logging"

static const reptile_t *(*state_cb)(void);
static lv_timer_t *log_timer;

static const char *LOG_FILE = "/sdcard/reptile_log.csv";

static void logging_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!state_cb) {
        return;
    }
    const reptile_t *r = state_cb();
    if (!r) {
        return;
    }
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        ESP_LOGE(LOG_TAG, "Failed to open log file");
        return;
    }
    fprintf(f,
            "%ld,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n",
            (long)r->last_update, r->faim, r->eau,
            r->temperature, r->humeur, (uint32_t)r->event);
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

void logging_init(const reptile_t *(*cb)(void))
{
    state_cb = cb;
    struct stat st;
    bool need_header = stat(LOG_FILE, &st) != 0;
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        ESP_LOGE(LOG_TAG, "Failed to create log file");
        return;
    }
    if (need_header) {
        fputs("timestamp,faim,eau,temperature,humeur,event\n", f);
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

