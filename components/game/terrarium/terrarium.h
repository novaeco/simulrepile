#ifndef TERRARIUM_H
#define TERRARIUM_H

#include <stdbool.h>

/**
 * @brief Add an item to the terrarium inventory.
 *
 * Stores the item name in an internal list for later reference.
 *
 * @param item Name of the item to add.
 * @return true on success, false if the list is full or item is NULL.
 */
bool terrarium_add_item(const char *item);

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

#endif // TERRARIUM_H
