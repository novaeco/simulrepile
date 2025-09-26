#include "log_manager.h"

#include "app_config.h"
#include "esp_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define LOG_RING_CAPACITY 64
#define LOG_MESSAGE_MAX_LEN 160

static const char *TAG = "log_mgr";

typedef struct {
    char line[LOG_MESSAGE_MAX_LEN];
} log_entry_t;

static log_entry_t s_ring[LOG_RING_CAPACITY];
static size_t s_ring_head = 0;
static size_t s_ring_count = 0;
static bool s_log_initialised = false;

static void append_entry(esp_log_level_t level, const char *prefix, const char *fmt, va_list args)
{
    char formatted[LOG_MESSAGE_MAX_LEN];
    int written = vsnprintf(formatted, sizeof(formatted), fmt, args);
    if (written < 0) {
        return;
    }
    esp_log_write(level, TAG, "%s%s", prefix, formatted);

    snprintf(s_ring[s_ring_head].line, sizeof(s_ring[s_ring_head].line), "%s%s", prefix, formatted);
    s_ring_head = (s_ring_head + 1) % LOG_RING_CAPACITY;
    if (s_ring_count < LOG_RING_CAPACITY) {
        ++s_ring_count;
    }
}

void log_manager_init(void)
{
    if (s_log_initialised) {
        return;
    }
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_head = 0;
    s_ring_count = 0;
    s_log_initialised = true;
    ESP_LOGI(TAG, "Log manager ready (ring=%u)", LOG_RING_CAPACITY);
}

void log_manager_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    append_entry(ESP_LOG_INFO, "INFO: ", fmt, args);
    va_end(args);
}

void log_manager_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    append_entry(ESP_LOG_ERROR, "ERROR: ", fmt, args);
    va_end(args);
}

size_t log_manager_copy_recent(char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return 0;
    }
    size_t total_written = 0;
    for (size_t i = 0; i < s_ring_count; ++i) {
        size_t idx = (s_ring_head + LOG_RING_CAPACITY - s_ring_count + i) % LOG_RING_CAPACITY;
        int n = snprintf(buffer + total_written, buffer_len - total_written, "%s\n", s_ring[idx].line);
        if (n < 0 || (size_t)n >= buffer_len - total_written) {
            break;
        }
        total_written += (size_t)n;
    }
    return total_written;
}

void log_manager_flush_to_sd(void)
{
    char path[APP_SD_PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/journal.log", APP_SD_SAVES_DIR);
    mkdir(APP_SD_MOUNT_POINT, 0775);
    mkdir(APP_SD_SAVES_DIR, 0775);
    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to flush logs to %s", path);
        return;
    }
    for (size_t i = 0; i < s_ring_count; ++i) {
        size_t idx = (s_ring_head + LOG_RING_CAPACITY - s_ring_count + i) % LOG_RING_CAPACITY;
        fprintf(f, "%s\n", s_ring[idx].line);
    }
    fclose(f);
}
