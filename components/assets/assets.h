#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *data;
    size_t size;
} asset_blob_t;

bool assets_load_sd(const char *path, asset_blob_t *out);
bool assets_load_embedded(const unsigned char *start, size_t size, asset_blob_t *out);
void assets_free(asset_blob_t *asset);

#ifdef __cplusplus
}
#endif
