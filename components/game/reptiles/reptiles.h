#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float temperature;
    float humidity;
    float uv_index;
    float terrarium_min_size;
} reptile_needs_t;

typedef struct {
    bool requires_authorisation;
    bool requires_certificat;
} reptile_legal_t;

typedef struct {
    const char *species;
    reptile_needs_t needs;
    reptile_legal_t legal;
} reptile_info_t;

bool reptiles_load(void);
const reptile_info_t *reptiles_get(size_t *count);
const reptile_info_t *reptiles_find(const char *species);
/* Validate biological needs and legal compliance */
bool reptiles_validate(const reptile_info_t *info);
/* Add a reptile after validation */
bool reptiles_add(const reptile_info_t *info);
