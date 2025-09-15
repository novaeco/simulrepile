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

void terrarium_update_environment(float temperature, float humidity, float uv_index)
{
    if (current_reptile) {
        temperature = current_reptile->temperature;
        humidity = current_reptile->humidity;
        uv_index = current_reptile->uv_index;
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
        terrarium_update_environment(reptile->temperature,
                                    reptile->humidity,
                                    reptile->uv_index);
    }
}

const terrarium_t *terrarium_get_state(void)
{
    return &state;
}
