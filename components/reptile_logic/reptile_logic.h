#ifndef REPTILE_LOGIC_H
#define REPTILE_LOGIC_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  REPTILE_EVENT_NONE = 0,
  REPTILE_EVENT_MALADIE,
  REPTILE_EVENT_CROISSANCE,
} reptile_event_t;

typedef struct {
  uint32_t faim;
  uint32_t eau;
  uint32_t temperature;
  uint32_t humidite; /* pourcentage d'humidite */
  uint32_t humeur;
  reptile_event_t event;
  time_t last_update;
} reptile_t;

typedef enum {
  REPTILE_FAMINE_THRESHOLD = 30,
  REPTILE_EAU_THRESHOLD = 30,
  REPTILE_TEMP_THRESHOLD_LOW = 26,
  REPTILE_TEMP_THRESHOLD_HIGH = 34,
  REPTILE_HUMEUR_THRESHOLD = 40,
} reptile_threshold_t;

esp_err_t reptile_init(reptile_t *r, bool simulation);
void reptile_update(reptile_t *r, uint32_t elapsed_ms);
esp_err_t reptile_load(reptile_t *r);
esp_err_t reptile_save(reptile_t *r);
void reptile_feed(reptile_t *r);
void reptile_give_water(reptile_t *r);
void reptile_heat(reptile_t *r);
void reptile_soothe(reptile_t *r);
reptile_event_t reptile_check_events(reptile_t *r);
bool reptile_sensors_available(void);

#ifdef __cplusplus
}
#endif

#endif // REPTILE_LOGIC_H
