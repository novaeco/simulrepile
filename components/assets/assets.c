#include "assets.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "assets";

bool assets_load_sd(const char *path, asset_blob_t *out)
{
    if (!out) {
        errno = EINVAL;
        ESP_LOGE(TAG, "Output blob pointer is NULL for path '%s'", path ? path : "<null>");
        return false;
    }

    if (!path) {
        errno = EINVAL;
        ESP_LOGE(TAG, "Input path pointer is NULL");
        return false;
    }

    out->data = NULL;
    out->size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        int err = errno ? errno : EIO;
        ESP_LOGE(TAG, "Failed to open asset file '%s': %s", path, strerror(err));
        errno = err;
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        int err = errno ? errno : EIO;
        ESP_LOGE(TAG, "fseek(SEEK_END) failed for '%s': %s", path, strerror(err));
        fclose(f);
        errno = err;
        return false;
    }

    long len = ftell(f);
    if (len < 0) {
        int err = errno ? errno : EIO;
        ESP_LOGE(TAG, "ftell failed for '%s': %s", path, strerror(err));
        fclose(f);
        errno = err;
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        int err = errno ? errno : EIO;
        ESP_LOGE(TAG, "fseek(SEEK_SET) failed for '%s': %s", path, strerror(err));
        fclose(f);
        errno = err;
        return false;
    }

    if (len == 0) {
#ifdef ENODATA
        errno = ENODATA;
#else
        errno = EINVAL;
#endif
        ESP_LOGE(TAG, "Asset file '%s' is empty", path);
        fclose(f);
        return false;
    }

    if ((unsigned long long)len > (unsigned long long)SIZE_MAX) {
        errno = EOVERFLOW;
        ESP_LOGE(TAG, "Asset file '%s' size %ld exceeds addressable range", path, len);
        fclose(f);
        return false;
    }

    size_t len_sz = (size_t)len;
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (len_sz > free_bytes) {
        errno = ENOMEM;
        ESP_LOGE(TAG, "Insufficient SPIRAM for '%s': need %zu bytes, free %zu bytes", path, len_sz, free_bytes);
        fclose(f);
        return false;
    }

    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (len_sz > largest_block) {
        errno = ENOMEM;
        ESP_LOGE(TAG, "Insufficient contiguous SPIRAM for '%s': need %zu bytes, largest block %zu bytes", path, len_sz, largest_block);
        fclose(f);
        return false;
    }

    void *buf = heap_caps_malloc(len_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        errno = ENOMEM;
        ESP_LOGE(TAG, "PSRAM allocation failed for '%s' (%zu bytes)", path, len_sz);
        fclose(f);
        return false;
    }

    size_t r = fread(buf, 1, len_sz, f);
    int ferr = ferror(f);
    int saved_errno = errno;
    fclose(f);
    if (r != len_sz) {
        heap_caps_free(buf);
        int err = ferr ? (saved_errno ? saved_errno : EIO) : EIO;
        errno = err;
        ESP_LOGE(TAG, "Failed to read asset file '%s': read %zu/%zu bytes%s", path, r, len_sz, ferr ? "" : " (unexpected EOF)");
        return false;
    }

    out->data = buf;
    out->size = len_sz;
    return true;
}

bool assets_load_embedded(const unsigned char *start, size_t size, asset_blob_t *out)
{
    void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return false;
    memcpy(buf, start, size);
    out->data = buf;
    out->size = size;
    return true;
}

void assets_free(asset_blob_t *asset)
{
    if (asset && asset->data) {
        heap_caps_free(asset->data);
        asset->data = NULL;
        asset->size = 0;
    }
}
