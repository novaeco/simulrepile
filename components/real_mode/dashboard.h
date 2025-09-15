#ifndef REAL_MODE_DASHBOARD_H
#define REAL_MODE_DASHBOARD_H

#include "real_mode.h"

void dashboard_init(void);
void dashboard_update(const sensor_data_t *data);
void dashboard_show(void);
void dashboard_set_device_status(size_t terrarium_idx, const terrarium_device_status_t *status);

#endif /* REAL_MODE_DASHBOARD_H */
