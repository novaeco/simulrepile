#ifndef REAL_MODE_DASHBOARD_H
#define REAL_MODE_DASHBOARD_H

#include "real_mode.h"

void dashboard_init(void);
/**
 * @brief Met à jour les widgets affichant les mesures.
 *
 * Les tâches qui ne sont pas le thread GUI doivent au préalable obtenir le
 * verrou exposé par lvgl_port_lock()/lvgl_port_unlock().
 */
void dashboard_update(const sensor_data_t *data);
void dashboard_show(void);
/**
 * @brief Rafraîchit la disponibilité des capteurs/actionneurs.
 *
 * Appliquer la même politique de verrouillage que pour dashboard_update().
 */
void dashboard_set_device_status(size_t terrarium_idx, const terrarium_device_status_t *status);

#endif /* REAL_MODE_DASHBOARD_H */
