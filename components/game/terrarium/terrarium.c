#include "terrarium.h"
#include "esp_log.h"
#include <string.h>

#define TAG "terrarium"

static terrarium_t state;

static const reptile_info_t *current_reptile;

bool terrarium_add_item(const char *item)
{
    if (!item || state.item_count >= TERRARIUM_MAX_ITEMS) {
        return false;
    }
    strncpy(state.items[state.item_count], item, TERRARIUM_ITEM_NAME_LEN - 1);
    state.items[state.item_count][TERRARIUM_ITEM_NAME_LEN - 1] = '\0';
    state.item_count++;
    ESP_LOGI(TAG, "Added item: %s", item);
    return true;
}

bool terrarium_set_decor(const char *decor)
{
    if (!decor) {
        return false;
    }
    strncpy(state.decor, decor, TERRARIUM_ITEM_NAME_LEN - 1);
    state.decor[TERRARIUM_ITEM_NAME_LEN - 1] = '\0';
    ESP_LOGI(TAG, "Decor set: %s", state.decor);
    return true;
}

bool terrarium_set_substrate(const char *substrate)
{
    if (!substrate) {
        return false;
    }
    strncpy(state.substrate, substrate, TERRARIUM_ITEM_NAME_LEN - 1);
    state.substrate[TERRARIUM_ITEM_NAME_LEN - 1] = '\0';
    ESP_LOGI(TAG, "Substrate set: %s", state.substrate);
    return true;
}

bool terrarium_add_equipment(const char *equip)
{
    return terrarium_add_item(equip);
}

void terrarium_set_heater(bool on)
{
    state.heater_on = on;
    ESP_LOGI(TAG, "Heater %s", on ? "ON" : "OFF");
}

void terrarium_set_light(bool on)
{
    state.light_on = on;
    ESP_LOGI(TAG, "Light %s", on ? "ON" : "OFF");
}

void terrarium_set_mist(bool on)
{
    state.mist_on = on;
    ESP_LOGI(TAG, "Mister %s", on ? "ON" : "OFF");
}

void terrarium_update_environment(float temperature, float humidity, float uv_index)
{
    if (current_reptile) {
        temperature = current_reptile->needs.temperature;
        humidity = current_reptile->needs.humidity;
        uv_index = current_reptile->needs.uv_index;
    }
    state.temperature = temperature;
    state.humidity = humidity;
    state.uv_index = uv_index;
    ESP_LOGI(TAG, "Environment updated T=%.1fC H=%.1f%% UV=%.1f", temperature, humidity, uv_index);
}

void terrarium_set_reptile(const reptile_info_t *reptile)
{
    current_reptile = reptile;
    if (reptile) {
        terrarium_update_environment(reptile->needs.temperature,
                                    reptile->needs.humidity,
                                    reptile->needs.uv_index);
    }
}

const terrarium_t *terrarium_get_state(void)
{
    return &state;
}
