#include "update_manager.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_rom_crc.h"

#include <cJSON.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "update_mgr";
static update_manifest_t s_manifest;

static const char *manifest_path(void)
{
    return APP_SD_MOUNT_POINT "/updates/manifest.json";
}

static int compute_file_crc(const char *path, uint32_t *out_crc)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    uint8_t buffer[256];
    size_t read;
    uint32_t crc = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        crc = esp_rom_crc32_le(crc, buffer, read);
    }
    fclose(f);
    if (out_crc) {
        *out_crc = crc;
    }
    return 0;
}

static int parse_manifest_json(const char *json, update_manifest_t *manifest)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return -1;
    }
    const cJSON *version = cJSON_GetObjectItem(root, "version");
    const cJSON *file = cJSON_GetObjectItem(root, "file");
    const cJSON *signature = cJSON_GetObjectItem(root, "signature");
    const cJSON *crc_item = cJSON_GetObjectItem(root, "crc32");

    if (!cJSON_IsString(version) || !cJSON_IsString(file)) {
        cJSON_Delete(root);
        return -1;
    }

    snprintf(manifest->version, sizeof(manifest->version), "%s", version->valuestring);
    snprintf(manifest->artifact_path, sizeof(manifest->artifact_path), "%s/updates/%s", APP_SD_MOUNT_POINT, file->valuestring);
    snprintf(manifest->signature, sizeof(manifest->signature), "%s",
             cJSON_IsString(signature) ? signature->valuestring : "");

    if (cJSON_IsNumber(crc_item)) {
        manifest->crc32 = (uint32_t)crc_item->valuedouble;
    } else if (cJSON_IsString(crc_item)) {
        manifest->crc32 = (uint32_t)strtoul(crc_item->valuestring, NULL, 16);
    } else {
        manifest->crc32 = 0;
    }

    cJSON_Delete(root);
    return 0;
}

int update_manager_load_manifest(update_manifest_t *manifest)
{
    if (!manifest) {
        return -1;
    }
    const char *path = manifest_path();
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "No update manifest at %s (%d)", path, errno);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        return -1;
    }
    fread(buffer, 1, (size_t)size, f);
    buffer[size] = '\0';
    fclose(f);

    int ret = parse_manifest_json(buffer, manifest);
    free(buffer);
    if (ret != 0) {
        ESP_LOGE(TAG, "Manifest parse failed");
        return -1;
    }

    if (manifest->crc32 == 0) {
        if (compute_file_crc(manifest->artifact_path, &manifest->crc32) != 0) {
            ESP_LOGE(TAG, "Unable to compute CRC for %s", manifest->artifact_path);
            return -1;
        }
    }
    return 0;
}

void update_manager_init(void)
{
    memset(&s_manifest, 0, sizeof(s_manifest));
}

void update_manager_check_sd(void)
{
    if (update_manager_load_manifest(&s_manifest) != 0) {
        ESP_LOGI(TAG, "No SD update manifest detected");
        return;
    }
    uint32_t computed_crc = 0;
    if (compute_file_crc(s_manifest.artifact_path, &computed_crc) != 0) {
        ESP_LOGE(TAG, "Update artifact missing: %s", s_manifest.artifact_path);
        return;
    }
    if (computed_crc != s_manifest.crc32) {
        ESP_LOGE(TAG, "CRC mismatch for update artifact (expected %08X, got %08X)", s_manifest.crc32, computed_crc);
        return;
    }
    ESP_LOGI(TAG, "Update %s ready (%s, CRC=%08X)", s_manifest.version, s_manifest.artifact_path, s_manifest.crc32);
    if (strlen(s_manifest.signature) > 0) {
        ESP_LOGI(TAG, "Signature hint present (%s)", s_manifest.signature);
    }
}
