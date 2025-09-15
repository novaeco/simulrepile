#include "assets.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

bool assets_load_sd(const char *path, asset_blob_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t r = fread(buf, 1, len, f);
    fclose(f);
    if (r != len) {
        heap_caps_free(buf);
        return false;
    }
    out->data = buf;
    out->size = (size_t)len;
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
