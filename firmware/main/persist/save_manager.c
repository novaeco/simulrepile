#include "save_manager.h"

#include "app_config.h"
#include "compression/compression_if.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "schema_version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SAVE_ROOT "/sdcard/saves"

static const char *TAG = "save_mgr";

static void ensure_directories(void)
{
    mkdir("/sdcard", 0775);
    mkdir(SAVE_ROOT, 0775);
}

static void build_path(size_t slot, char *path, size_t len, bool backup)
{
    snprintf(path, len, "%s/slot%u.%s", SAVE_ROOT, (unsigned)(slot + 1), backup ? "bak" : "sav");
}

static int serialise_state(const sim_terrarium_state_t *state, char **out_buf, size_t *out_len)
{
    if (!state || !out_buf || !out_len) {
        return -1;
    }
    const size_t buf_size = 1024;
    char *buf = malloc(buf_size);
    if (!buf) {
        return -1;
    }
    int written = snprintf(buf, buf_size,
                           "{\n  \"version\": \"%s\",\n  \"nickname\": \"%s\",\n  \"temperature\": %.2f,\n  \"humidity\": %.2f\n}\n",
                           SIMULREPILE_SCHEMA_VERSION_STRING,
                           state->nickname,
                           state->health.temperature_c,
                           state->health.humidity_percent);
    if (written < 0) {
        free(buf);
        return -1;
    }
    *out_buf = buf;
    *out_len = (size_t)written;
    return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s (%d)", path, errno);
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

void save_manager_init(void)
{
    ensure_directories();
}

int save_manager_save_slot(size_t slot, const sim_terrarium_state_t *state)
{
    if (slot >= SAVE_SLOT_COUNT || !state) {
        return -1;
    }
    ensure_directories();
    char *payload = NULL;
    size_t payload_len = 0;
    if (serialise_state(state, &payload, &payload_len) != 0) {
        return -1;
    }

    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    bool use_compression = CONFIG_APP_ENABLE_COMPRESSION && compression_compress((const uint8_t *)payload, payload_len, &compressed, &compressed_len) == 0;

    const uint8_t *data_to_write = use_compression ? compressed : (const uint8_t *)payload;
    size_t len_to_write = use_compression ? compressed_len : payload_len;

    save_header_t header = {
        .compression_enabled = use_compression,
        .crc32 = esp_rom_crc32_le(0, data_to_write, len_to_write),
    };

    char path[128];
    build_path(slot, path, sizeof(path), false);

    char bak_path[128];
    build_path(slot, bak_path, sizeof(bak_path), true);

    // Backup existing save
    FILE *src = fopen(path, "rb");
    if (src) {
        FILE *dst = fopen(bak_path, "wb");
        if (dst) {
            uint8_t buffer[256];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                fwrite(buffer, 1, n, dst);
            }
            fclose(dst);
        }
        fclose(src);
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Unable to open %s", path);
        free(payload);
        free(compressed);
        return -1;
    }
    fwrite(&header, sizeof(header), 1, f);
    fwrite(data_to_write, 1, len_to_write, f);
    fclose(f);

    free(payload);
    free(compressed);

    return 0;
}

static int parse_payload(const uint8_t *data, size_t len, sim_terrarium_state_t *out_state)
{
    if (!data || !out_state) {
        return -1;
    }
    // Minimal parsing: extract nickname
    sscanf((const char *)data, "{\n  \"version\": %*[^,],\n  \"nickname\": \"%31[^\"]", out_state->nickname);
    out_state->health.temperature_c = 30.0f;
    out_state->health.humidity_percent = 50.0f;
    return 0;
}

int save_manager_load_slot(size_t slot, sim_terrarium_state_t *out_state)
{
    if (slot >= SAVE_SLOT_COUNT || !out_state) {
        return -1;
    }
    char path[128];
    build_path(slot, path, sizeof(path), false);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Save %s missing", path);
        return -1;
    }
    save_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long remaining = ftell(f) - (long)sizeof(header);
    fseek(f, sizeof(header), SEEK_SET);
    uint8_t *payload = malloc((size_t)remaining);
    if (!payload) {
        fclose(f);
        return -1;
    }
    fread(payload, 1, (size_t)remaining, f);
    fclose(f);

    uint32_t crc = esp_rom_crc32_le(0, payload, (size_t)remaining);
    if (crc != header.crc32) {
        ESP_LOGE(TAG, "CRC mismatch for slot %u", (unsigned)slot);
        free(payload);
        return -2;
    }

    uint8_t *decompressed = payload;
    size_t decompressed_len = (size_t)remaining;
    if (header.compression_enabled) {
        if (compression_decompress(payload, (size_t)remaining, &decompressed, &decompressed_len) != 0) {
            ESP_LOGE(TAG, "Decompression failed");
            free(payload);
            return -3;
        }
        free(payload);
    }

    int ret = parse_payload(decompressed, decompressed_len, out_state);
    if (decompressed != payload) {
        free(decompressed);
    }
    return ret;
}

int save_manager_rollback(size_t slot)
{
    char path[128];
    char bak_path[128];
    build_path(slot, path, sizeof(path), false);
    build_path(slot, bak_path, sizeof(bak_path), true);

    FILE *bak = fopen(bak_path, "rb");
    if (!bak) {
        return -1;
    }
    uint8_t buffer[256];
    FILE *out = fopen(path, "wb");
    if (!out) {
        fclose(bak);
        return -1;
    }
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), bak)) > 0) {
        fwrite(buffer, 1, n, out);
    }
    fclose(bak);
    fclose(out);
    return 0;
}

int save_manager_internal_crc_validate(const uint8_t *data, size_t len, uint32_t expected)
{
    if (!data) {
        return -1;
    }
    uint32_t crc = esp_rom_crc32_le(0, data, len);
    return crc == expected ? 0 : -1;
}
