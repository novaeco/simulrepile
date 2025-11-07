#include "updates/updates_manager.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"

#include "cJSON.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UPDATES_ROOT_PATH "/sdcard/updates"
#define UPDATES_MANIFEST_PATH "/sdcard/updates/manifest.json"
#define UPDATES_BIN_PATH "/sdcard/updates/update.bin"
#define UPDATES_BIN_BAK_PATH "/sdcard/updates/update.bin.bak"

static const char *TAG = "updates";

static esp_err_t errno_to_esp_err(int errnum)
{
    switch (errnum) {
        case ENOENT:
            return ESP_ERR_NOT_FOUND;
        case EACCES:
        case EPERM:
            return ESP_ERR_INVALID_STATE;
        case ENOMEM:
            return ESP_ERR_NO_MEM;
        default:
            return ESP_FAIL;
    }
}

static uint32_t parse_crc32(const cJSON *crc_node, bool *ok)
{
    *ok = false;
    if (!crc_node) {
        return 0;
    }

    if (cJSON_IsNumber(crc_node)) {
        double value = crc_node->valuedouble;
        if (value >= 0 && value <= 0xFFFFFFFFu) {
            *ok = true;
            return (uint32_t)value;
        }
        return 0;
    }

    if (cJSON_IsString(crc_node) && crc_node->valuestring) {
        const char *text = crc_node->valuestring;
        char *endptr = NULL;
        uint32_t value = (uint32_t)strtoul(text, &endptr, 0);
        if (endptr != text && *endptr == '\0') {
            *ok = true;
            return value;
        }
    }

    return 0;
}

static size_t parse_size(const cJSON *size_node, bool *ok)
{
    *ok = false;
    if (!size_node) {
        return 0;
    }

    if (cJSON_IsNumber(size_node)) {
        double value = size_node->valuedouble;
        if (value >= 0 && value <= (double)SIZE_MAX) {
            *ok = true;
            return (size_t)value;
        }
        return 0;
    }

    if (cJSON_IsString(size_node) && size_node->valuestring) {
        const char *text = size_node->valuestring;
        char *endptr = NULL;
        unsigned long long value = strtoull(text, &endptr, 0);
        if (endptr != text && *endptr == '\0' && value <= SIZE_MAX) {
            *ok = true;
            return (size_t)value;
        }
    }

    return 0;
}

static esp_err_t read_file_crc_and_size(const char *path, uint32_t *out_crc, size_t *out_size)
{
    if (!path || !out_crc || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        int err = errno;
        ESP_LOGE(TAG, "Failed to open %s: %s", path, strerror(err));
        return errno_to_esp_err(err);
    }

    uint8_t buffer[4096];
    size_t total = 0;
    uint32_t crc = 0;

    while (true) {
        size_t just_read = fread(buffer, 1, sizeof(buffer), file);
        if (just_read > 0) {
            crc = esp_rom_crc32_le(crc, buffer, just_read);
            total += just_read;
        }
        if (just_read < sizeof(buffer)) {
            if (ferror(file)) {
                int err = errno;
                ESP_LOGE(TAG, "Read error on %s: %s", path, strerror(err));
                fclose(file);
                return errno_to_esp_err(err);
            }
            break;
        }
    }

    fclose(file);

    *out_crc = crc;
    *out_size = total;
    return ESP_OK;
}

static void populate_default_manifest(updates_manifest_info_t *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    strncpy(info->file_name, "update.bin", sizeof(info->file_name) - 1);
}

esp_err_t updates_check_available(updates_manifest_info_t *out_info)
{
    updates_manifest_info_t local_info;
    populate_default_manifest(&local_info);

    if (access(UPDATES_MANIFEST_PATH, F_OK) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if ((access(UPDATES_BIN_PATH, F_OK) != 0) && (access(UPDATES_BIN_BAK_PATH, F_OK) == 0)) {
        if (rename(UPDATES_BIN_BAK_PATH, UPDATES_BIN_PATH) == 0) {
            ESP_LOGW(TAG, "Restored %s from leftover .bak", UPDATES_BIN_PATH);
        } else {
            ESP_LOGW(TAG, "Failed to restore leftover .bak update: %s", strerror(errno));
        }
    }

    FILE *manifest_file = fopen(UPDATES_MANIFEST_PATH, "rb");
    if (!manifest_file) {
        int err = errno;
        ESP_LOGE(TAG, "Cannot open manifest: %s", strerror(err));
        return errno_to_esp_err(err);
    }

    if (fseek(manifest_file, 0, SEEK_END) != 0) {
        int err = errno;
        ESP_LOGE(TAG, "Manifest seek failed: %s", strerror(err));
        fclose(manifest_file);
        return errno_to_esp_err(err);
    }

    long manifest_length = ftell(manifest_file);
    if (manifest_length < 0) {
        int err = errno;
        ESP_LOGE(TAG, "Manifest ftell failed: %s", strerror(err));
        fclose(manifest_file);
        return errno_to_esp_err(err);
    }

    rewind(manifest_file);

    char *manifest_json = (char *)malloc((size_t)manifest_length + 1);
    if (!manifest_json) {
        fclose(manifest_file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(manifest_json, 1, (size_t)manifest_length, manifest_file);
    fclose(manifest_file);
    if (read_len != (size_t)manifest_length) {
        free(manifest_json);
        ESP_LOGE(TAG, "Failed to read manifest (expected %ld got %zu)", manifest_length, read_len);
        return ESP_FAIL;
    }
    manifest_json[manifest_length] = '\0';

    cJSON *root = cJSON_Parse(manifest_json);
    free(manifest_json);
    if (!root) {
        ESP_LOGE(TAG, "Manifest JSON parse error");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *channel = cJSON_GetObjectItemCaseSensitive(root, "channel");
    const cJSON *build_id = cJSON_GetObjectItemCaseSensitive(root, "build");
    const cJSON *file_name = cJSON_GetObjectItemCaseSensitive(root, "file");
    const cJSON *crc = cJSON_GetObjectItemCaseSensitive(root, "crc32");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");

    if (cJSON_IsString(version) && version->valuestring) {
        strncpy(local_info.version, version->valuestring, sizeof(local_info.version) - 1);
    }
    if (cJSON_IsString(channel) && channel->valuestring) {
        strncpy(local_info.channel, channel->valuestring, sizeof(local_info.channel) - 1);
    }
    if (cJSON_IsString(build_id) && build_id->valuestring) {
        strncpy(local_info.build_id, build_id->valuestring, sizeof(local_info.build_id) - 1);
    }
    if (cJSON_IsString(file_name) && file_name->valuestring) {
        strncpy(local_info.file_name, file_name->valuestring, sizeof(local_info.file_name) - 1);
    }

    bool crc_ok = false;
    uint32_t manifest_crc = parse_crc32(crc, &crc_ok);
    bool size_ok = false;
    size_t manifest_size = parse_size(size, &size_ok);

    char update_path[128];
    snprintf(update_path, sizeof(update_path), "%s/%s", UPDATES_ROOT_PATH, local_info.file_name[0] ? local_info.file_name : "update.bin");

    if (access(update_path, F_OK) != 0) {
        ESP_LOGW(TAG, "Update binary %s not found", update_path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t actual_crc = 0;
    size_t actual_size = 0;
    esp_err_t err = read_file_crc_and_size(update_path, &actual_crc, &actual_size);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }

    local_info.size_bytes = actual_size;
    local_info.crc32 = actual_crc;

    if (crc_ok && manifest_crc != actual_crc) {
        ESP_LOGE(TAG, "CRC mismatch: manifest %08x actual %08x", manifest_crc, actual_crc);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_CRC;
    }

    if (size_ok && manifest_size != actual_size) {
        ESP_LOGE(TAG, "Size mismatch: manifest %zu actual %zu", manifest_size, actual_size);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Update detected: version=%s size=%zu CRC=%08x", local_info.version[0] ? local_info.version : "?", actual_size, actual_crc);

    if (out_info) {
        *out_info = local_info;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t cleanup_bak_on_failure(void)
{
    if (access(UPDATES_BIN_BAK_PATH, F_OK) != 0) {
        return ESP_OK;
    }

    if (rename(UPDATES_BIN_BAK_PATH, UPDATES_BIN_PATH) != 0) {
        int err = errno;
        ESP_LOGW(TAG, "Failed to restore update.bin from .bak: %s", strerror(err));
        return errno_to_esp_err(err);
    }

    ESP_LOGW(TAG, "Rolled back update.bin from .bak after failure");
    return ESP_OK;
}

esp_err_t updates_apply(const updates_manifest_info_t *expected_info)
{
    updates_manifest_info_t current_info;
    esp_err_t err = updates_check_available(&current_info);
    if (err != ESP_OK) {
        return err;
    }

    if (expected_info) {
        if (expected_info->crc32 && expected_info->crc32 != current_info.crc32) {
            ESP_LOGW(TAG, "Manifest CRC changed (expected %08x got %08x)", expected_info->crc32, current_info.crc32);
            return ESP_ERR_INVALID_STATE;
        }
        if (expected_info->version[0] && strncmp(expected_info->version, current_info.version, sizeof(current_info.version)) != 0) {
            ESP_LOGW(TAG, "Manifest version changed");
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (access(UPDATES_BIN_BAK_PATH, F_OK) == 0) {
        ESP_LOGW(TAG, "Removing stale update backup before applying");
        unlink(UPDATES_BIN_BAK_PATH);
    }

    if (rename(UPDATES_BIN_PATH, UPDATES_BIN_BAK_PATH) != 0) {
        int sys_err = errno;
        ESP_LOGE(TAG, "Failed to create .bak: %s", strerror(sys_err));
        return errno_to_esp_err(sys_err);
    }

    FILE *binary = fopen(UPDATES_BIN_BAK_PATH, "rb");
    if (!binary) {
        int sys_err = errno;
        ESP_LOGE(TAG, "Cannot open %s: %s", UPDATES_BIN_BAK_PATH, strerror(sys_err));
        cleanup_bak_on_failure();
        return errno_to_esp_err(sys_err);
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        ESP_LOGE(TAG, "No OTA partition available");
        fclose(binary);
        cleanup_bak_on_failure();
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(target, current_info.size_bytes, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        fclose(binary);
        cleanup_bak_on_failure();
        return err;
    }

    uint8_t buffer[4096];
    size_t total_written = 0;
    while (true) {
        size_t just_read = fread(buffer, 1, sizeof(buffer), binary);
        if (just_read > 0) {
            err = esp_ota_write(ota_handle, buffer, just_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed at %zu bytes: %s", total_written, esp_err_to_name(err));
                esp_ota_abort(ota_handle);
                fclose(binary);
                cleanup_bak_on_failure();
                return err;
            }
            total_written += just_read;
        }
        if (just_read < sizeof(buffer)) {
            if (ferror(binary)) {
                int sys_err = errno;
                ESP_LOGE(TAG, "Read error while flashing: %s", strerror(sys_err));
                esp_ota_abort(ota_handle);
                fclose(binary);
                cleanup_bak_on_failure();
                return errno_to_esp_err(sys_err);
            }
            break;
        }
    }
    fclose(binary);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        cleanup_bak_on_failure();
        return err;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        cleanup_bak_on_failure();
        return err;
    }

    unlink(UPDATES_BIN_BAK_PATH);
    unlink(UPDATES_MANIFEST_PATH);

    ESP_LOGI(TAG, "Update applied successfully to partition %s (%zu bytes)", target->label, total_written);
    return ESP_OK;
}
