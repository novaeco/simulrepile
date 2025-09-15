#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *species;
    float temperature;
    float humidity;
    float uv_index;
    float terrarium_min_size;
    bool requires_authorisation;
    bool requires_certificat;
} reptile_info_t;

bool reptiles_load(void);
const reptile_info_t *reptiles_get(size_t *count);
const reptile_info_t *reptiles_find(const char *species);
/* Validate biological needs and legal compliance */
bool reptiles_validate(const reptile_info_t *info);
