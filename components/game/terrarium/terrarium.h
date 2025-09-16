#ifndef TERRARIUM_H
#define TERRARIUM_H

#include <stdbool.h>
#include <stddef.h>
#include "reptiles.h"

#define TERRARIUM_MAX_ITEMS 16
#define TERRARIUM_ITEM_NAME_LEN 32

typedef struct {
    size_t item_count;
    char items[TERRARIUM_MAX_ITEMS][TERRARIUM_ITEM_NAME_LEN];
    char decor[TERRARIUM_ITEM_NAME_LEN];
    char substrate[TERRARIUM_ITEM_NAME_LEN];
    float temperature;
    float humidity;
    float uv_index;
    bool heater_on;
    bool light_on;
    bool mist_on;
} terrarium_t;

/**
 * @brief Reset the terrarium state to its default values.
 */
void terrarium_reset(void);

/**
 * @brief Add an item to the terrarium inventory.
 *
 * Stores the item name in an internal list for later reference.
 *
 * @param item Name of the item to add.
 * @return true on success, false if the list is full or item is NULL.
 */
bool terrarium_add_item(const char *item);

/** Personalisation helpers */
bool terrarium_set_decor(const char *decor);
bool terrarium_set_substrate(const char *substrate);
bool terrarium_add_equipment(const char *equip);

/** Actuator control */
void terrarium_set_heater(bool on);
void terrarium_set_light(bool on);
void terrarium_set_mist(bool on);

/**
 * @brief Set the reptile currently hosted in the terrarium.
 *
 * The provided reptile description is stored for later reference so that
 * environmental parameters can automatically mirror the animal's needs.
 *
 * @param reptile Pointer to the reptile information. NULL clears the current
 *        association.
 */
void terrarium_set_reptile(const reptile_info_t *reptile);

/**
 * @brief Synchronise environment parameters with the hosted reptile.
 *
 * Updates the temperature, humidity and UV index of the terrarium to match
 * the requirements of the reptile currently housed.
 *
 * @param temperature Target temperature in Celsius.
 * @param humidity Target relative humidity in percent.
 * @param uv_index Target UV index.
 */
void terrarium_update_environment(float temperature, float humidity, float uv_index);

/**
 * @brief Retrieve current terrarium state snapshot.
 *
 * @return Pointer to internal terrarium state structure.
 */
const terrarium_t *terrarium_get_state(void);

#endif // TERRARIUM_H
