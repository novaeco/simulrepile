#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char path[128];
    uint32_t size;
    uint8_t *data;
} asset_item_t;

void asset_manager_init(void);
const asset_item_t *asset_manager_get(const char *path);
void asset_manager_release(const char *path);

#ifdef __cplusplus
}
#endif
