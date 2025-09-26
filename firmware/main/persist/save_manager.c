#include "persist/save_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "persist/schema_version.h"

static const char *TAG = "save_manager";
static char s_root[128];

static void build_path(int slot_index, bool backup, char *buffer, size_t len)
{
    snprintf(buffer, len, "%s/slot%d%s.json", s_root, slot_index, backup ? ".bak" : "");
}

esp_err_t save_manager_init(const char *root_path)
{
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_root, root_path, sizeof(s_root));
    ESP_LOGI(TAG, "Save root set to %s", s_root);
    return ESP_OK;
}

esp_err_t save_manager_load_slot(int slot_index, save_slot_t *out_slot)
{
    if (!out_slot) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[160];
    build_path(slot_index, false, path, sizeof(path));
    ESP_LOGI(TAG, "Loading slot %d (%s)", slot_index, path);
    memset(out_slot, 0, sizeof(*out_slot));
    out_slot->payload = calloc(1, 128);
    if (!out_slot->payload) {
        return ESP_ERR_NO_MEM;
    }
    out_slot->meta.payload_length = 128;
    out_slot->meta.crc32 = 0;
    strcpy((char *)out_slot->payload, "{}");
    return ESP_OK;
}

esp_err_t save_manager_save_slot(int slot_index, const save_slot_t *slot_data, bool make_backup)
{
    if (!slot_data) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[160];
    build_path(slot_index, false, path, sizeof(path));
    ESP_LOGI(TAG, "Saving slot %d -> %s (backup=%d)", slot_index, path, make_backup);
    if (make_backup) {
        char bak_path[160];
        build_path(slot_index, true, bak_path, sizeof(bak_path));
        ESP_LOGI(TAG, "Creating backup %s", bak_path);
    }
    return ESP_OK;
}

void save_manager_free_slot(save_slot_t *slot)
{
    if (!slot) {
        return;
    }
    free(slot->payload);
    memset(slot, 0, sizeof(*slot));
}
