#include "assets/asset_cache.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define ASSET_CACHE_HASH_BUCKETS ((size_t)CONFIG_APP_ASSET_CACHE_HASH_BUCKETS)
#define ASSET_CACHE_MAX_PATH_LEN ((size_t)CONFIG_APP_ASSET_CACHE_MAX_PATH)
#define ASSET_CACHE_IDLE_GRACE_TICKS ((uint32_t)CONFIG_APP_ASSET_CACHE_IDLE_GRACE_TICKS)

_Static_assert(CONFIG_APP_ASSET_CACHE_HASH_BUCKETS > 0, "hash bucket count must be > 0");
_Static_assert(CONFIG_APP_ASSET_CACHE_MAX_PATH > 0, "max path length must be > 0");

typedef struct asset_cache_entry {
    struct asset_cache_entry *prev;
    struct asset_cache_entry *next;
    struct asset_cache_entry *hash_next;
    char *path;
    asset_type_t type;
    void *data;
    size_t size;
    uint32_t ref_count;
    uint32_t idle_ticks;
} asset_cache_entry_t;

typedef struct {
    asset_cache_entry_t *head;
    asset_cache_entry_t *tail;
    asset_cache_entry_t *hash_table[ASSET_CACHE_HASH_BUCKETS];
    size_t capacity;
    size_t count;
    bool initialized;
} asset_cache_context_t;

static const char *TAG = "asset_cache";
static asset_cache_context_t s_cache;

static void asset_cache_reset_context(void)
{
    memset(&s_cache, 0, sizeof(s_cache));
}

static uint32_t asset_cache_hash(const char *path)
{
    const uint8_t *bytes = (const uint8_t *)path;
    uint32_t hash = 2166136261u;
    while (*bytes) {
        hash ^= *bytes++;
        hash *= 16777619u;
    }
    return hash;
}

static asset_cache_entry_t *asset_cache_hash_find(const char *path)
{
    if (!path) {
        return NULL;
    }
    uint32_t idx = asset_cache_hash(path) % ASSET_CACHE_HASH_BUCKETS;
    asset_cache_entry_t *entry = s_cache.hash_table[idx];
    while (entry) {
        if (strcmp(entry->path, path) == 0) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return NULL;
}

static void asset_cache_hash_insert(asset_cache_entry_t *entry)
{
    uint32_t idx = asset_cache_hash(entry->path) % ASSET_CACHE_HASH_BUCKETS;
    entry->hash_next = s_cache.hash_table[idx];
    s_cache.hash_table[idx] = entry;
}

static void asset_cache_hash_remove(asset_cache_entry_t *entry)
{
    uint32_t idx = asset_cache_hash(entry->path) % ASSET_CACHE_HASH_BUCKETS;
    asset_cache_entry_t **cursor = &s_cache.hash_table[idx];
    while (*cursor) {
        if (*cursor == entry) {
            *cursor = entry->hash_next;
            entry->hash_next = NULL;
            return;
        }
        cursor = &(*cursor)->hash_next;
    }
}

static void asset_cache_list_detach(asset_cache_entry_t *entry)
{
    if (!entry) {
        return;
    }
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    if (s_cache.head == entry) {
        s_cache.head = entry->next;
    }
    if (s_cache.tail == entry) {
        s_cache.tail = entry->prev;
    }
    entry->prev = NULL;
    entry->next = NULL;
}

static void asset_cache_list_insert_head(asset_cache_entry_t *entry)
{
    entry->prev = NULL;
    entry->next = s_cache.head;
    if (s_cache.head) {
        s_cache.head->prev = entry;
    }
    s_cache.head = entry;
    if (!s_cache.tail) {
        s_cache.tail = entry;
    }
}

static void asset_cache_list_move_to_head(asset_cache_entry_t *entry)
{
    if (!entry || s_cache.head == entry) {
        return;
    }
    asset_cache_list_detach(entry);
    asset_cache_list_insert_head(entry);
}

static void asset_cache_free_entry(asset_cache_entry_t *entry)
{
    if (!entry) {
        return;
    }
    if (entry->data) {
        heap_caps_free(entry->data);
        entry->data = NULL;
    }
    free(entry->path);
    entry->path = NULL;
    free(entry);
}

void asset_cache_deinit(void)
{
    if (!s_cache.initialized) {
        return;
    }

    size_t released = s_cache.count;
    asset_cache_entry_t *cursor = s_cache.head;
    while (cursor) {
        asset_cache_entry_t *next = cursor->next;
        asset_cache_free_entry(cursor);
        cursor = next;
    }

    asset_cache_reset_context();
    if (released > 0U) {
        ESP_LOGI(TAG, "Asset cache flushed (%zu entries)", released);
    } else {
        ESP_LOGD(TAG, "Asset cache flushed (empty)");
    }
}

static bool asset_cache_evict_entry(asset_cache_entry_t *entry)
{
    if (!entry) {
        return false;
    }
    ESP_LOGD(TAG, "Evicting asset: %s", entry->path);
    asset_cache_hash_remove(entry);
    asset_cache_list_detach(entry);
    s_cache.count--;
    asset_cache_free_entry(entry);
    return true;
}

static bool asset_cache_evict_oldest(void)
{
    asset_cache_entry_t *cursor = s_cache.tail;
    while (cursor) {
        if (cursor->ref_count == 0U) {
            return asset_cache_evict_entry(cursor);
        }
        cursor = cursor->prev;
    }
    return false;
}

static char *asset_cache_strdup(const char *source)
{
    size_t len = strlen(source) + 1U;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, source, len);
    }
    return copy;
}

static asset_type_t asset_cache_detect_type(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (!extension) {
        return ASSET_TYPE_TEXT;
    }
    extension++;
    if (strcasecmp(extension, "png") == 0) {
        return ASSET_TYPE_IMAGE_PNG;
    }
    if (strcasecmp(extension, "json") == 0) {
        return ASSET_TYPE_JSON;
    }
    if (strcasecmp(extension, "txt") == 0 || strcasecmp(extension, "md") == 0 ||
        strcasecmp(extension, "html") == 0) {
        return ASSET_TYPE_TEXT;
    }
    return ASSET_TYPE_BINARY;
}

static esp_err_t asset_cache_load_file(const char *path, asset_cache_entry_t *entry)
{
    asset_type_t type = asset_cache_detect_type(path);

    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) {
            ESP_LOGW(TAG, "Asset not found: %s", path);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to open asset %s: errno=%d", path, errno);
        return ESP_FAIL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        ESP_LOGE(TAG, "Failed to seek asset %s", path);
        fclose(file);
        return ESP_FAIL;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to determine size for %s", path);
        fclose(file);
        return ESP_FAIL;
    }
    rewind(file);

    size_t size = (size_t)file_size;
    bool null_terminated = (type == ASSET_TYPE_JSON) || (type == ASSET_TYPE_TEXT);
    size_t alloc_size = size + (null_terminated ? 1U : 0U);

    void *buffer = heap_caps_malloc(alloc_size > 0 ? alloc_size : 1U,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes in PSRAM for %s", alloc_size, path);
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1U, size, file);
    fclose(file);

    if (read != size) {
        ESP_LOGE(TAG, "Short read for asset %s (%zu/%zu)", path, read, size);
        heap_caps_free(buffer);
        return ESP_FAIL;
    }

    if (null_terminated) {
        ((uint8_t *)buffer)[size] = '\0';
    }

    entry->data = buffer;
    entry->size = size;
    entry->type = type;
    entry->idle_ticks = 0;
    return ESP_OK;
}

static esp_err_t asset_cache_normalize_path(const char *input, char *output, size_t output_len)
{
    if (!input || !output || output_len < sizeof("/sdcard/")) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(input, "/sdcard/", 8) == 0) {
        size_t length = strlen(input);
        if (length + 1U > output_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(output, input, length + 1U);
        return ESP_OK;
    }

    if (strcmp(input, "/sdcard") == 0) {
        static const char canonical[] = "/sdcard/";
        if (sizeof(canonical) > output_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(output, canonical, sizeof(canonical));
        return ESP_OK;
    }

    const char *relative = input;
    if (relative[0] == '/') {
        relative++;
    }

    size_t relative_len = strlen(relative);
    size_t required = 8U + relative_len;
    if (required + 1U > output_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(output, "/sdcard/", 8);
    memcpy(output + 8, relative, relative_len + 1U);
    return ESP_OK;
}

static esp_err_t asset_cache_create_entry(const char *path, asset_cache_entry_t **out_entry)
{
    asset_cache_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }

    entry->path = asset_cache_strdup(path);
    if (!entry->path) {
        free(entry);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = asset_cache_load_file(path, entry);
    if (err != ESP_OK) {
        asset_cache_free_entry(entry);
        return err;
    }

    entry->ref_count = 1U;
    *out_entry = entry;
    return ESP_OK;
}

esp_err_t asset_cache_init(void)
{
    if (s_cache.initialized) {
        asset_cache_deinit();
    }

    asset_cache_reset_context();
    s_cache.capacity = CONFIG_APP_ASSET_CACHE_CAPACITY;
    if (s_cache.capacity == 0U) {
        s_cache.capacity = 1U;
    }
    s_cache.initialized = true;
    ESP_LOGI(TAG, "Asset cache ready (capacity=%zu)", s_cache.capacity);
    return ESP_OK;
}

void asset_cache_tick(void)
{
    if (!s_cache.initialized) {
        return;
    }

    asset_cache_entry_t *cursor = s_cache.tail;
    while (cursor) {
        asset_cache_entry_t *previous = cursor->prev;
        if (cursor->ref_count == 0U) {
            if (cursor->idle_ticks >= ASSET_CACHE_IDLE_GRACE_TICKS || s_cache.count > s_cache.capacity) {
                asset_cache_evict_entry(cursor);
            } else {
                cursor->idle_ticks++;
            }
        } else {
            cursor->idle_ticks = 0U;
        }
        cursor = previous;
    }
}

esp_err_t asset_cache_get(const char *path, asset_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(s_cache.initialized, ESP_ERR_INVALID_STATE, TAG, "Cache not initialized");
    ESP_RETURN_ON_FALSE(path && handle, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    char normalized[ASSET_CACHE_MAX_PATH_LEN];
    esp_err_t err = asset_cache_normalize_path(path, normalized, sizeof(normalized));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid asset path: %s", path);
        return err;
    }

    asset_cache_entry_t *entry = asset_cache_hash_find(normalized);
    if (entry) {
        entry->ref_count++;
        entry->idle_ticks = 0U;
        asset_cache_list_move_to_head(entry);
    } else {
        while (s_cache.count >= s_cache.capacity) {
            if (!asset_cache_evict_oldest()) {
                ESP_LOGW(TAG, "Cache capacity reached (%zu) and no evictable asset", s_cache.capacity);
                return ESP_ERR_NO_MEM;
            }
        }

        err = asset_cache_create_entry(normalized, &entry);
        if (err != ESP_OK) {
            return err;
        }

        asset_cache_list_insert_head(entry);
        asset_cache_hash_insert(entry);
        s_cache.count++;
        ESP_LOGD(TAG, "Cached asset: %s (size=%zu)", entry->path, entry->size);
    }

    memset(handle, 0, sizeof(*handle));
    handle->path = entry->path;
    handle->type = entry->type;
    handle->data = entry->data;
    handle->size = entry->size;
    handle->ref_count = entry->ref_count;
    return ESP_OK;
}

void asset_cache_release(asset_handle_t *handle)
{
    if (!handle || !handle->path) {
        return;
    }

    if (!s_cache.initialized) {
        memset(handle, 0, sizeof(*handle));
        return;
    }

    asset_cache_entry_t *entry = asset_cache_hash_find(handle->path);
    if (!entry) {
        ESP_LOGW(TAG, "Attempted to release unmanaged asset: %s", handle->path);
        memset(handle, 0, sizeof(*handle));
        return;
    }

    if (entry->ref_count > 0U) {
        entry->ref_count--;
    }
    if (entry->ref_count == 0U) {
        entry->idle_ticks = 0U;
    }

    ESP_LOGD(TAG, "Released asset: %s (ref=%u)", entry->path, (unsigned)entry->ref_count);
    memset(handle, 0, sizeof(*handle));
}
