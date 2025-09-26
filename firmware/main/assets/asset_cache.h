#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASSET_TYPE_IMAGE,
    ASSET_TYPE_TEXT,
} asset_type_t;

typedef struct {
    const char *path;
    asset_type_t type;
    void *data;
    size_t size;
} asset_handle_t;

esp_err_t asset_cache_init(void);
void asset_cache_tick(void);
esp_err_t asset_cache_get(const char *path, asset_handle_t *handle);
void asset_cache_release(asset_handle_t *handle);

#ifdef __cplusplus
}
#endif
