#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

bool storage_init(void);
bool storage_save(const char *path, const void *data, size_t len);
bool storage_load(const char *path, void *data, size_t len);

#ifdef __cplusplus
}
#endif
