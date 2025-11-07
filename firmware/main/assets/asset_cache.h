#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASSET_TYPE_IMAGE_PNG = 0,
    ASSET_TYPE_JSON,
    ASSET_TYPE_TEXT,
    ASSET_TYPE_BINARY,
} asset_type_t;

typedef struct {
    const char *path;
    asset_type_t type;
    void *data;
    size_t size;
    uint32_t ref_count;
} asset_handle_t;

esp_err_t asset_cache_init(void);
void asset_cache_deinit(void);
void asset_cache_tick(void);
esp_err_t asset_cache_get(const char *path, asset_handle_t *handle);
void asset_cache_release(asset_handle_t *handle);

#ifdef __cplusplus
}
#endif
