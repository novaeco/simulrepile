#include "logs.h"
#include "storage.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef CONFIG_STORAGE_LOG_GZIP
#include <zlib.h>
#endif

#define TAG "storage_log"
#define LOG_BASE_PATH "/sdcard/logs"
#define LOG_MIN_FREE_BYTES (1024 * 1024) /* 1 MB */

static bool ensure_path(void)
{
    struct stat st;
    if (stat(LOG_BASE_PATH, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(LOG_BASE_PATH, 0775) == 0;
}

static void rotate_log(const char *path)
{
    char rotated[128];
    snprintf(rotated, sizeof(rotated), "%s.1", path);
    rename(path, rotated);
#ifdef CONFIG_STORAGE_LOG_GZIP
    char gz[128];
    snprintf(gz, sizeof(gz), "%s.gz", rotated);
    FILE *in = fopen(rotated, "rb");
    if (in) {
        gzFile out = gzopen(gz, "wb");
        if (out) {
            char buf[256];
            int n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                gzwrite(out, buf, n);
            }
            gzclose(out);
        }
        fclose(in);
        unlink(rotated);
    }
#endif
}

static bool check_space(const char *path)
{
    struct statvfs vfs;
    if (statvfs("/sdcard", &vfs) != 0) {
        return true; /* Assume enough space */
    }
    unsigned long free_bytes = vfs.f_bavail * vfs.f_frsize;
    if (free_bytes < LOG_MIN_FREE_BYTES) {
        rotate_log(path);
    }
    return true;
}

bool storage_append_log(const char *terrarium,
                        const storage_log_entry_t *entry,
                        storage_log_format_t format)
{
    if (!terrarium || !entry) {
        return false;
    }
    if (!ensure_path()) {
        ESP_LOGE(TAG, "Cannot create log path");
        return false;
    }
    char path[128];
    const char *ext = (format == STORAGE_LOG_CSV) ? "csv" : "json";
    snprintf(path, sizeof(path), LOG_BASE_PATH"/%s.%s", terrarium, ext);
    check_space(path);
    FILE *f = fopen(path, "a");
    if (!f) {
        return false;
    }
    if (format == STORAGE_LOG_CSV) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size == 0) {
            fprintf(f, "time,temperature,humidity,uv,co2,actuators,power\n");
        }
        fprintf(f, "%ld,%.2f,%.2f,%.2f,%.2f,0x%08"PRIx32",%.2f\n",
                (long)entry->timestamp,
                entry->temperature,
                entry->humidity,
                entry->uv_index,
                entry->co2,
                entry->actuator_mask,
                entry->power);
    } else {
        fprintf(f,
                "{\"time\":%ld,\"temperature\":%.2f,\"humidity\":%.2f,\"uv\":%.2f,\"co2\":%.2f,\"actuators\":\"0x%08"PRIx32"\",\"power\":%.2f}\n",
                (long)entry->timestamp,
                entry->temperature,
                entry->humidity,
                entry->uv_index,
                entry->co2,
                entry->actuator_mask,
                entry->power);
    }
    fclose(f);
    return true;
}
