#include "logging.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#define LOG_DIR "/sdcard/logs"

static const char *TAG = "logging";

esp_err_t logging_init(void)
{
    struct stat st = {0};
    if (stat(LOG_DIR, &st) == -1) {
        int res = mkdir(LOG_DIR, 0775);
        if (res != 0) {
            ESP_LOGE(TAG, "mkdir %s failed", LOG_DIR);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t logging_write(const sensor_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filepath[64];
    strftime(filepath, sizeof(filepath), LOG_DIR "/%Y%m%d.log", tm_info);
    FILE *f = fopen(filepath, "a");
    if (!f) {
        ESP_LOGE(TAG, "fopen failed");
        return ESP_FAIL;
    }
    fprintf(f, "%ld,%.2f,%.2f,%.2f,%.2f\n", now, data->temperature_c,
            data->humidity_pct, data->luminosity_lux, data->co2_ppm);
    fclose(f);
    return ESP_OK;
}
