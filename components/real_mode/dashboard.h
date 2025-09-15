#ifndef REAL_MODE_DASHBOARD_H
#define REAL_MODE_DASHBOARD_H

#include "real_mode.h"

void dashboard_init(void);
void dashboard_update(const sensor_data_t *data);
void dashboard_show(void);

#endif /* REAL_MODE_DASHBOARD_H */
