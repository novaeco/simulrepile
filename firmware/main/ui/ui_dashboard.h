#pragma once

#include <stddef.h>

#include "sim/models.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_dashboard_create(void);
void ui_dashboard_refresh(size_t terrarium_count, const terrarium_state_t *first_state);

#ifdef __cplusplus
}
#endif
