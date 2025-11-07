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
#define UPDATES_DEFAULT_FILE_NAME "update.bin"
#define UPDATES_MANIFEST_PATH UPDATES_ROOT_PATH "/manifest.json"
#define UPDATES_BIN_PATH UPDATES_ROOT_PATH "/" UPDATES_DEFAULT_FILE_NAME
#define UPDATES_BIN_BAK_PATH UPDATES_BIN_PATH ".bak"
#define UPDATES_LAST_FLASH_PATH UPDATES_ROOT_PATH "/last_flash.json"

static const char *TAG = "updates";

static esp_err_t errno_to_esp_err(int errnum);
static void populate_default_manifest(updates_manifest_info_t *info);
static void build_update_paths(const updates_manifest_info_t *info,
                               char *binary_path,
                               size_t binary_path_size,
                               char *backup_path,
                               size_t backup_path_size);

const char *updates_flash_outcome_to_string(updates_flash_outcome_t outcome)
{
    switch (outcome) {
        case UPDATES_FLASH_OUTCOME_SUCCESS:
            return "success";
        case UPDATES_FLASH_OUTCOME_ERROR:
            return "error";
        case UPDATES_FLASH_OUTCOME_ROLLBACK:
            return "rollback";
        case UPDATES_FLASH_OUTCOME_NONE:
        default:
            return "none";
    }
}

static updates_flash_outcome_t flash_outcome_from_string(const char *text, bool *ok)
{
    if (ok) {
        *ok = true;
    }

    if (!text) {
        if (ok) {
            *ok = false;
        }
        return UPDATES_FLASH_OUTCOME_NONE;
    }

    if (strcmp(text, "success") == 0) {
        return UPDATES_FLASH_OUTCOME_SUCCESS;
    }
    if (strcmp(text, "error") == 0) {
        return UPDATES_FLASH_OUTCOME_ERROR;
    }
    if (strcmp(text, "rollback") == 0) {
        return UPDATES_FLASH_OUTCOME_ROLLBACK;
    }
    if (strcmp(text, "none") == 0) {
        return UPDATES_FLASH_OUTCOME_NONE;
    }

    if (ok) {
        *ok = false;
    }
    return UPDATES_FLASH_OUTCOME_NONE;
}

static void populate_default_report(updates_flash_report_t *report)
{
    if (!report) {
        return;
    }

    memset(report, 0, sizeof(*report));
    report->outcome = UPDATES_FLASH_OUTCOME_NONE;
    populate_default_manifest(&report->manifest);
}

static esp_err_t store_last_flash_report(const updates_flash_report_t *report)
{
    if (!report) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "outcome", updates_flash_outcome_to_string(report->outcome));
    cJSON_AddNumberToObject(root, "error", (double)report->error);

    cJSON *manifest = cJSON_CreateObject();
    if (!manifest) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(manifest, "version", report->manifest.version);
    cJSON_AddStringToObject(manifest, "channel", report->manifest.channel);
    cJSON_AddStringToObject(manifest, "build", report->manifest.build_id);
    cJSON_AddStringToObject(manifest, "file", report->manifest.file_name);
    cJSON_AddNumberToObject(manifest, "size", (double)report->manifest.size_bytes);
    cJSON_AddNumberToObject(manifest, "crc32", (double)report->manifest.crc32);
    cJSON_AddItemToObject(root, "manifest", manifest);

    if (report->partition_label[0] != '\0') {
        cJSON_AddStringToObject(root, "partition", report->partition_label);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(UPDATES_LAST_FLASH_PATH, "wb");
    if (!file) {
        int err = errno;
        free(json);
        return errno_to_esp_err(err);
    }

    size_t length = strlen(json);
    size_t written = fwrite(json, 1, length, file);
    free(json);
    if (fclose(file) != 0) {
        int err = errno;
        return errno_to_esp_err(err);
    }

    if (written != length) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t load_last_flash_report(updates_flash_report_t *out_report)
{
    if (!out_report) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(UPDATES_LAST_FLASH_PATH, "rb");
    if (!file) {
        int err = errno;
        return errno_to_esp_err(err);
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        int err = errno;
        fclose(file);
        return errno_to_esp_err(err);
    }

    long length = ftell(file);
    if (length < 0) {
        int err = errno;
        fclose(file);
        return errno_to_esp_err(err);
    }
    rewind(file);

    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read_len != (size_t)length) {
        free(buffer);
        return ESP_FAIL;
    }
    buffer[length] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    populate_default_report(out_report);

    const cJSON *outcome = cJSON_GetObjectItemCaseSensitive(root, "outcome");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *manifest = cJSON_GetObjectItemCaseSensitive(root, "manifest");
    const cJSON *partition = cJSON_GetObjectItemCaseSensitive(root, "partition");

    bool outcome_ok = false;
    out_report->outcome = flash_outcome_from_string(cJSON_IsString(outcome) ? outcome->valuestring : NULL, &outcome_ok);

    if (cJSON_IsNumber(error)) {
        out_report->error = (esp_err_t)error->valueint;
    }

    if (manifest && cJSON_IsObject(manifest)) {
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(manifest, "version");
        const cJSON *channel = cJSON_GetObjectItemCaseSensitive(manifest, "channel");
        const cJSON *build = cJSON_GetObjectItemCaseSensitive(manifest, "build");
        const cJSON *file_name = cJSON_GetObjectItemCaseSensitive(manifest, "file");
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(manifest, "size");
        const cJSON *crc32 = cJSON_GetObjectItemCaseSensitive(manifest, "crc32");

        if (cJSON_IsString(version) && version->valuestring) {
            strncpy(out_report->manifest.version, version->valuestring, sizeof(out_report->manifest.version) - 1);
        }
        if (cJSON_IsString(channel) && channel->valuestring) {
            strncpy(out_report->manifest.channel, channel->valuestring, sizeof(out_report->manifest.channel) - 1);
        }
        if (cJSON_IsString(build) && build->valuestring) {
            strncpy(out_report->manifest.build_id, build->valuestring, sizeof(out_report->manifest.build_id) - 1);
        }
        if (cJSON_IsString(file_name) && file_name->valuestring) {
            strncpy(out_report->manifest.file_name, file_name->valuestring, sizeof(out_report->manifest.file_name) - 1);
        }
        if (cJSON_IsNumber(size)) {
            out_report->manifest.size_bytes = (size_t)size->valuedouble;
        }
        if (cJSON_IsNumber(crc32)) {
            out_report->manifest.crc32 = (uint32_t)crc32->valuedouble;
        }
    }

    if (cJSON_IsString(partition) && partition->valuestring) {
        strncpy(out_report->partition_label, partition->valuestring, sizeof(out_report->partition_label) - 1);
    }

    cJSON_Delete(root);

    if (!outcome_ok) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static void record_flash_outcome(updates_flash_outcome_t outcome,
                                 const updates_manifest_info_t *manifest,
                                 esp_err_t error,
                                 const char *partition_label)
{
    updates_flash_report_t report;
    populate_default_report(&report);

    report.outcome = outcome;
    report.error = error;
    if (manifest) {
        report.manifest = *manifest;
    }
    if (partition_label && partition_label[0] != '\0') {
        strncpy(report.partition_label, partition_label, sizeof(report.partition_label) - 1);
    }

    esp_err_t err = store_last_flash_report(&report);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store flash report: %s", esp_err_to_name(err));
    }
}

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
    strncpy(info->file_name, UPDATES_DEFAULT_FILE_NAME, sizeof(info->file_name) - 1);
}

static void build_update_paths(const updates_manifest_info_t *info,
                               char *binary_path,
                               size_t binary_path_size,
                               char *backup_path,
                               size_t backup_path_size)
{
    const char *file_name = UPDATES_DEFAULT_FILE_NAME;
    if (info && info->file_name[0] != '\0') {
        file_name = info->file_name;
    }

    if (binary_path && binary_path_size > 0) {
        snprintf(binary_path, binary_path_size, "%s/%s", UPDATES_ROOT_PATH, file_name);
    }

    if (backup_path && backup_path_size > 0) {
        snprintf(backup_path, backup_path_size, "%s/%s.bak", UPDATES_ROOT_PATH, file_name);
    }
}

esp_err_t updates_check_available(updates_manifest_info_t *out_info)
{
    updates_manifest_info_t local_info;
    populate_default_manifest(&local_info);

    if (access(UPDATES_MANIFEST_PATH, F_OK) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    char default_binary_path[128];
    char default_backup_path[128];
    build_update_paths(NULL, default_binary_path, sizeof(default_binary_path), default_backup_path, sizeof(default_backup_path));

    if ((access(default_binary_path, F_OK) != 0) && (access(default_backup_path, F_OK) == 0)) {
        if (rename(default_backup_path, default_binary_path) == 0) {
            ESP_LOGW(TAG, "Restored %s from leftover .bak", default_binary_path);
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
    char update_bak_path[128];
    build_update_paths(&local_info, update_path, sizeof(update_path), update_bak_path, sizeof(update_bak_path));

    if ((access(update_path, F_OK) != 0) && (access(update_bak_path, F_OK) == 0)) {
        if (rename(update_bak_path, update_path) == 0) {
            ESP_LOGW(TAG, "Restored %s from leftover .bak", update_path);
        } else {
            ESP_LOGW(TAG, "Failed to restore leftover .bak update: %s", strerror(errno));
        }
    }

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

static esp_err_t cleanup_bak_on_failure(const char *binary_path, const char *backup_path)
{
    if (!backup_path || backup_path[0] == '\0') {
        return ESP_OK;
    }

    if (access(backup_path, F_OK) != 0) {
        return ESP_OK;
    }

    if (!binary_path || binary_path[0] == '\0') {
        ESP_LOGW(TAG, "Backup %s present but no target path available", backup_path);
        if (unlink(backup_path) != 0) {
            int err = errno;
            return errno_to_esp_err(err);
        }
        return ESP_OK;
    }

    if (rename(backup_path, binary_path) != 0) {
        int err = errno;
        ESP_LOGW(TAG, "Failed to restore %s from .bak: %s", binary_path, strerror(err));
        return errno_to_esp_err(err);
    }

    ESP_LOGW(TAG, "Rolled back %s from .bak after failure", binary_path);
    return ESP_OK;
}

esp_err_t updates_apply(const updates_manifest_info_t *expected_info)
{
    updates_manifest_info_t current_info;
    populate_default_manifest(&current_info);

    esp_err_t err = updates_check_available(&current_info);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NOT_FOUND) {
            record_flash_outcome(UPDATES_FLASH_OUTCOME_ERROR, &current_info, err, NULL);
        }
        return err;
    }

    if (expected_info) {
        if (expected_info->crc32 && expected_info->crc32 != current_info.crc32) {
            ESP_LOGW(TAG, "Manifest CRC changed (expected %08x got %08x)", expected_info->crc32, current_info.crc32);
            record_flash_outcome(UPDATES_FLASH_OUTCOME_ERROR, &current_info, ESP_ERR_INVALID_STATE, NULL);
            return ESP_ERR_INVALID_STATE;
        }
        if (expected_info->version[0] && strncmp(expected_info->version, current_info.version, sizeof(current_info.version)) != 0) {
            ESP_LOGW(TAG, "Manifest version changed");
            record_flash_outcome(UPDATES_FLASH_OUTCOME_ERROR, &current_info, ESP_ERR_INVALID_STATE, NULL);
            return ESP_ERR_INVALID_STATE;
        }
        if (expected_info->file_name[0]
            && strncmp(expected_info->file_name, current_info.file_name, sizeof(current_info.file_name)) != 0) {
            ESP_LOGW(TAG, "Manifest file name changed");
            record_flash_outcome(UPDATES_FLASH_OUTCOME_ERROR, &current_info, ESP_ERR_INVALID_STATE, NULL);
            return ESP_ERR_INVALID_STATE;
        }
    }

    char binary_path[128];
    char backup_path[128];
    build_update_paths(&current_info, binary_path, sizeof(binary_path), backup_path, sizeof(backup_path));

    if (access(backup_path, F_OK) == 0) {
        ESP_LOGW(TAG, "Removing stale update backup before applying");
        unlink(backup_path);
    }

    if (rename(binary_path, backup_path) != 0) {
        int sys_err = errno;
        ESP_LOGE(TAG, "Failed to create .bak: %s", strerror(sys_err));
        esp_err_t mapped = errno_to_esp_err(sys_err);
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ERROR, &current_info, mapped, NULL);
        return mapped;
    }

    FILE *binary = fopen(backup_path, "rb");
    if (!binary) {
        int sys_err = errno;
        ESP_LOGE(TAG, "Cannot open %s: %s", backup_path, strerror(sys_err));
        cleanup_bak_on_failure(binary_path, backup_path);
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, errno_to_esp_err(sys_err), NULL);
        return errno_to_esp_err(sys_err);
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        ESP_LOGE(TAG, "No OTA partition available");
        fclose(binary);
        cleanup_bak_on_failure(binary_path, backup_path);
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, ESP_FAIL, NULL);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(target, current_info.size_bytes, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        fclose(binary);
        cleanup_bak_on_failure(binary_path, backup_path);
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, err, target->label);
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
                cleanup_bak_on_failure(binary_path, backup_path);
                record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, err, target->label);
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
                cleanup_bak_on_failure(binary_path, backup_path);
                esp_err_t mapped = errno_to_esp_err(sys_err);
                record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, mapped, target->label);
                return mapped;
            }
            break;
        }
    }
    fclose(binary);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        cleanup_bak_on_failure(binary_path, backup_path);
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, err, target->label);
        return err;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        cleanup_bak_on_failure(binary_path, backup_path);
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &current_info, err, target->label);
        return err;
    }

    unlink(backup_path);
    unlink(UPDATES_MANIFEST_PATH);

    record_flash_outcome(UPDATES_FLASH_OUTCOME_SUCCESS, &current_info, ESP_OK, target->label);
    ESP_LOGI(TAG, "Update applied successfully to partition %s (%zu bytes)", target->label, total_written);
    return ESP_OK;
}

esp_err_t updates_get_last_flash_report(updates_flash_report_t *out_report)
{
    if (!out_report) {
        return ESP_ERR_INVALID_ARG;
    }

    populate_default_report(out_report);
    esp_err_t err = load_last_flash_report(out_report);
    return err;
}

esp_err_t updates_finalize_boot_state(void)
{
    esp_err_t result = ESP_OK;

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
        esp_err_t state_err = esp_ota_get_state_partition(running, &state);
        if (state_err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
            if (mark_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to mark OTA image valid: %s", esp_err_to_name(mark_err));
                if (result == ESP_OK) {
                    result = mark_err;
                }
            } else {
                ESP_LOGI(TAG, "Marked partition %s as valid", running->label);
            }
        } else if (state_err != ESP_OK && result == ESP_OK) {
            result = state_err;
        }
    }

    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    if (invalid) {
        updates_flash_report_t report;
        esp_err_t load_err = updates_get_last_flash_report(&report);
        if (load_err != ESP_OK && load_err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to load flash history: %s", esp_err_to_name(load_err));
        }
        if (load_err != ESP_OK) {
            populate_default_report(&report);
        }

        esp_err_t error_code = report.error != ESP_OK ? report.error : ESP_FAIL;
        record_flash_outcome(UPDATES_FLASH_OUTCOME_ROLLBACK, &report.manifest, error_code, invalid->label);
        ESP_LOGW(TAG, "Rollback detected, active partition reverted to %s", invalid->label);
    }

    return result;
}
