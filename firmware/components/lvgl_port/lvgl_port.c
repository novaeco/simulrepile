#include "lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t s_mutex;

esp_err_t lvgl_port_init(void)
{
    if (s_mutex) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LVGL port initialized (stub double buffer)");
    return ESP_OK;
}

void lvgl_port_lock(void)
{
    if (!s_mutex) {
        return;
    }
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
}

void lvgl_port_unlock(void)
{
    if (!s_mutex) {
        return;
    }
    xSemaphoreGiveRecursive(s_mutex);
}

void lvgl_port_invalidate(void)
{
    // In a real implementation we would trigger a display refresh.
    (void)TAG;
}
