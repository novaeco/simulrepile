#pragma once

#include "app_config.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char version[16];
    char artifact_path[APP_SD_PATH_MAX_LEN];
    char signature[128];
    uint32_t crc32;
} update_manifest_t;

void update_manager_init(void);
void update_manager_check_sd(void);
int update_manager_load_manifest(update_manifest_t *manifest);

#ifdef __cplusplus
}
#endif
