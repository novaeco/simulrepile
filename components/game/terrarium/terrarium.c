#include "terrarium.h"
#include "esp_log.h"
#include <string.h>

#define TAG "terrarium"
#define MAX_ITEMS 16
#define ITEM_NAME_LEN 32

static char items[MAX_ITEMS][ITEM_NAME_LEN];
static size_t item_count;

static struct {
    float temperature;
    float humidity;
    float uv_index;
} environment;

bool terrarium_add_item(const char *item)
{
    if (!item || item_count >= MAX_ITEMS) {
        return false;
    }
    strncpy(items[item_count], item, ITEM_NAME_LEN - 1);
    items[item_count][ITEM_NAME_LEN - 1] = '\0';
    item_count++;
    ESP_LOGI(TAG, "Added item: %s", item);
    return true;
}

void terrarium_update_environment(float temperature, float humidity, float uv_index)
{
    environment.temperature = temperature;
    environment.humidity = humidity;
    environment.uv_index = uv_index;
    ESP_LOGI(TAG, "Environment updated T=%.1fC H=%.1f%% UV=%.1f", temperature, humidity, uv_index);
}
