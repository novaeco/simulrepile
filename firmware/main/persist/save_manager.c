#include "persist/save_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_rom_crc.h"
#include "persist/schema_version.h"

static const char *TAG = "save_manager";
static char s_root[128];

#define SAVE_MANAGER_MAX_SLOTS 4

typedef struct __attribute__((packed)) {
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t payload_crc32;
    uint32_t payload_length;
    uint64_t saved_at_unix;
} save_file_header_t;

static esp_err_t ensure_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "%s exists but is not a directory", path);
        return ESP_FAIL;
    }
    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }
    if (errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to create %s: errno=%d", path, errno);
    return ESP_FAIL;
}

static void build_path(int slot_index, bool backup, char *buffer, size_t len)
{
    snprintf(buffer, len, "%s/slot%d%s.json", s_root, slot_index, backup ? ".bak" : "");
}

static esp_err_t copy_file(const char *src_path, const char *dst_path)
{
    FILE *src = fopen(src_path, "rb");
    if (!src) {
        return ESP_ERR_NOT_FOUND;
    }
    FILE *dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        return ESP_FAIL;
    }

    uint8_t buffer[512];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, read_bytes, dst) != read_bytes) {
            fclose(src);
            fclose(dst);
            unlink(dst_path);
            return ESP_FAIL;
        }
    }
    fflush(dst);
    fsync(fileno(dst));
    fclose(src);
    fclose(dst);
    return ESP_OK;
}

static esp_err_t delete_file(const char *path)
{
    if (unlink(path) == 0) {
        return ESP_OK;
    }
    if (errno == ENOENT) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "Failed to delete %s: errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t write_atomic(const char *path, const save_file_header_t *header, const uint8_t *payload)
{
    char tmp_path[200];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", tmp_path);
        return ESP_FAIL;
    }
    if (fwrite(header, sizeof(*header), 1, f) != 1) {
        ESP_LOGE(TAG, "Header write failed for %s", tmp_path);
        fclose(f);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    if (header->payload_length > 0 && payload) {
        if (fwrite(payload, 1, header->payload_length, f) != header->payload_length) {
            ESP_LOGE(TAG, "Payload write failed for %s", tmp_path);
            fclose(f);
            unlink(tmp_path);
            return ESP_FAIL;
        }
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to move %s -> %s (errno=%d)", tmp_path, path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t load_from_path(const char *path, save_slot_t *out_slot)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    save_file_header_t header;
    size_t read = fread(&header, 1, sizeof(header), f);
    if (read != sizeof(header)) {
        ESP_LOGE(TAG, "Header read failed for %s", path);
        fclose(f);
        return ESP_FAIL;
    }

    if (memcmp(header.magic, SIMULREPILE_SAVE_MAGIC, sizeof(header.magic)) != 0) {
        ESP_LOGE(TAG, "Invalid magic in %s", path);
        fclose(f);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header.version > SIMULREPILE_SAVE_VERSION) {
        ESP_LOGE(TAG, "Unsupported version %u in %s", header.version, path);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (header.flags & SAVE_MANAGER_FLAG_COMPRESSED) {
        ESP_LOGW(TAG, "Compressed saves not yet supported (flags=0x%08x)", header.flags);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (header.flags & ~SAVE_MANAGER_FLAG_COMPRESSED) {
        ESP_LOGE(TAG, "Unknown flag bits set (0x%08x) in %s", header.flags, path);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t *payload = NULL;
    if (header.payload_length > 0) {
        payload = calloc(1, header.payload_length + 1);
        if (!payload) {
            fclose(f);
            return ESP_ERR_NO_MEM;
        }
        if (fread(payload, 1, header.payload_length, f) != header.payload_length) {
            ESP_LOGE(TAG, "Failed to read payload for %s", path);
            free(payload);
            fclose(f);
            return ESP_FAIL;
        }
        uint32_t crc = esp_rom_crc32_le(0, payload, header.payload_length);
        if (crc != header.payload_crc32) {
            ESP_LOGE(TAG, "CRC mismatch for %s (expected %08x got %08x)", path, header.payload_crc32, crc);
            free(payload);
            fclose(f);
            return ESP_ERR_INVALID_CRC;
        }
        payload[header.payload_length] = '\0';
    }
    fclose(f);

    out_slot->payload = payload;
    out_slot->meta.schema_version = header.version;
    out_slot->meta.flags = header.flags;
    out_slot->meta.crc32 = header.payload_crc32;
    out_slot->meta.payload_length = header.payload_length;
    out_slot->meta.saved_at_unix = header.saved_at_unix;
    memset(out_slot->meta.reserved, 0, sizeof(out_slot->meta.reserved));
    return ESP_OK;
}

esp_err_t save_manager_init(const char *root_path)
{
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_root, root_path, sizeof(s_root));
    esp_err_t err = ensure_directory(s_root);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "Save root set to %s", s_root);
    return ESP_OK;
}

esp_err_t save_manager_load_slot(int slot_index, save_slot_t *out_slot)
{
    if (!out_slot) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot_index < 0 || slot_index >= SAVE_MANAGER_MAX_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_slot, 0, sizeof(*out_slot));

    char path[160];
    build_path(slot_index, false, path, sizeof(path));
    ESP_LOGI(TAG, "Loading slot %d (%s)", slot_index, path);

    esp_err_t err = load_from_path(path, out_slot);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Primary slot %d invalid (err=0x%x), trying backup", slot_index, err);
    }

    char bak_path[160];
    build_path(slot_index, true, bak_path, sizeof(bak_path));
    ESP_LOGI(TAG, "Loading backup slot %d (%s)", slot_index, bak_path);
    err = load_from_path(bak_path, out_slot);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Backup for slot %d unavailable (err=0x%x)", slot_index, err);
    return err;
}

esp_err_t save_manager_save_slot(int slot_index, const save_slot_t *slot_data, bool make_backup)
{
    if (!slot_data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot_index < 0 || slot_index >= SAVE_MANAGER_MAX_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!slot_data->payload && slot_data->meta.payload_length > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot_data->meta.flags & SAVE_MANAGER_FLAG_COMPRESSED) {
        ESP_LOGW(TAG, "Compression flag set but codec not available");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (slot_data->meta.flags & ~SAVE_MANAGER_FLAG_COMPRESSED) {
        ESP_LOGE(TAG, "Unsupported flag bits 0x%08x", slot_data->meta.flags);
        return ESP_ERR_INVALID_ARG;
    }
    char path[160];
    build_path(slot_index, false, path, sizeof(path));
    ESP_LOGI(TAG, "Saving slot %d -> %s (backup=%d)", slot_index, path, make_backup);
    if (make_backup) {
        char bak_path[160];
        build_path(slot_index, true, bak_path, sizeof(bak_path));
        esp_err_t copy_err = copy_file(path, bak_path);
        if (copy_err != ESP_OK && copy_err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Backup copy failed for slot %d (err=0x%x)", slot_index, copy_err);
        }
    }

    size_t payload_length = slot_data->meta.payload_length;
    if (payload_length == 0 && slot_data->payload) {
        payload_length = strlen((const char *)slot_data->payload);
    }

    save_file_header_t disk_header = {0};
    memcpy(disk_header.magic, SIMULREPILE_SAVE_MAGIC, sizeof(disk_header.magic));
    disk_header.version = slot_data->meta.schema_version ? slot_data->meta.schema_version : SIMULREPILE_SAVE_VERSION;
    disk_header.flags = slot_data->meta.flags;
    disk_header.payload_length = payload_length;
    disk_header.saved_at_unix = (uint64_t)time(NULL);
    if (payload_length > 0 && slot_data->payload) {
        disk_header.payload_crc32 = esp_rom_crc32_le(0, slot_data->payload, payload_length);
    }

    esp_err_t err = write_atomic(path, &disk_header, slot_data->payload);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "Slot %d saved (len=%u crc=%08x)", slot_index, (unsigned)disk_header.payload_length, disk_header.payload_crc32);
    return ESP_OK;
}

esp_err_t save_manager_delete_slot(int slot_index)
{
    if (slot_index < 0 || slot_index >= SAVE_MANAGER_MAX_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[160];
    build_path(slot_index, false, path, sizeof(path));
    char bak_path[160];
    build_path(slot_index, true, bak_path, sizeof(bak_path));

    esp_err_t err = delete_file(path);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }
    err = delete_file(bak_path);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
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
