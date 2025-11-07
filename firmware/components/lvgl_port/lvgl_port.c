#include "lvgl_port.h"

#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "bsp/waveshare_7b_lgfx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lvgl.h"

#define LVGL_PORT_HOR_RES            WAVESHARE_7B_LCD_HOR_RES
#define LVGL_PORT_VER_RES            WAVESHARE_7B_LCD_VER_RES
#define LVGL_TICK_TASK_STACK_SIZE    2048
#define LVGL_RENDER_TASK_STACK_SIZE  6144
#define LVGL_TICK_TASK_PRIORITY      6
#define LVGL_RENDER_TASK_PRIORITY    5
#define LVGL_TOUCH_QUEUE_LENGTH      16
#define LVGL_RENDER_REFRESH_MS       (1000 / 60)

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} lvgl_touch_event_t;

static const char *TAG = "lvgl_port";

static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_render_sem;
static QueueHandle_t s_touch_queue;
static TaskHandle_t s_tick_task;
static TaskHandle_t s_render_task;
static lv_display_t *s_display;
static lv_indev_t *s_touch_indev;
static bool s_initialized;
static uint8_t *s_framebuffers[2];
static size_t s_framebuffer_size;

static struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} s_touch_state;

static void lvgl_tick_task(void *arg);
static void lvgl_render_task(void *arg);
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void lvgl_rounder_cb(lv_display_t *disp, lv_area_t *area);
static void lvgl_drv_update_cb(lv_display_t *disp);
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void lvgl_port_reset_state(void);

esp_err_t lvgl_port_init(void)
{
    esp_err_t err = ESP_OK;

    if (s_initialized) {
        return ESP_OK;
    }

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateRecursiveMutex();
        ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "mutex alloc failed");
    }

    lv_init();

    uint32_t bytes_per_px = lv_color_format_get_size(LV_COLOR_FORMAT_RGB565);
    s_framebuffer_size = (size_t)LVGL_PORT_HOR_RES * LVGL_PORT_VER_RES * bytes_per_px;

    for (size_t i = 0; i < 2; ++i) {
        s_framebuffers[i] = heap_caps_malloc(s_framebuffer_size,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
        if (!s_framebuffers[i]) {
            err = ESP_ERR_NO_MEM;
            goto cleanup;
        }
        memset(s_framebuffers[i], 0, s_framebuffer_size);
    }

    ESP_GOTO_ON_ERROR(waveshare_7b_lgfx_init(LVGL_PORT_HOR_RES, LVGL_PORT_VER_RES), cleanup, TAG,
                      "LovyanGFX init failed");

    s_render_sem = xSemaphoreCreateBinary();
    if (!s_render_sem) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    s_touch_queue = xQueueCreate(LVGL_TOUCH_QUEUE_LENGTH, sizeof(lvgl_touch_event_t));
    if (!s_touch_queue) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    s_display = lv_display_create(LVGL_PORT_HOR_RES, LVGL_PORT_VER_RES);
    if (!s_display) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_framebuffers[0], s_framebuffers[1], s_framebuffer_size, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);
    lv_display_set_rounder_cb(s_display, lvgl_rounder_cb);
    lv_display_set_driver_update_cb(s_display, lvgl_drv_update_cb);
    lv_display_set_default(s_display);

    s_touch_indev = lv_indev_create();
    if (!s_touch_indev) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, lvgl_touch_read_cb);
    lv_indev_set_display(s_touch_indev, s_display);

    BaseType_t task_created = xTaskCreatePinnedToCore(lvgl_tick_task, "lv_tick", LVGL_TICK_TASK_STACK_SIZE, NULL,
                                                      LVGL_TICK_TASK_PRIORITY, &s_tick_task, 0);
    if (task_created != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    task_created = xTaskCreatePinnedToCore(lvgl_render_task, "lv_render", LVGL_RENDER_TASK_STACK_SIZE, NULL,
                                           LVGL_RENDER_TASK_PRIORITY, &s_render_task, 1);
    if (task_created != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    s_initialized = true;
    lvgl_port_invalidate();
    ESP_LOGI(TAG, "LVGL port ready (%zu KB double buffer in PSRAM)", (s_framebuffer_size * 2) / 1024);
    return ESP_OK;

cleanup:
    lvgl_port_reset_state();
    return err;
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
    if (!s_render_sem) {
        return;
    }

    if (xPortIsInsideInterrupt()) {
        BaseType_t higher_woken = pdFALSE;
        xSemaphoreGiveFromISR(s_render_sem, &higher_woken);
        if (higher_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        xSemaphoreGive(s_render_sem);
    }
}

void lvgl_port_feed_touch_event(bool pressed, uint16_t x, uint16_t y)
{
    if (!s_touch_queue) {
        return;
    }

    lvgl_touch_event_t event = {
        .pressed = pressed,
        .x = x,
        .y = y,
    };

    if (xPortIsInsideInterrupt()) {
        BaseType_t higher_woken = pdFALSE;
        xQueueSendFromISR(s_touch_queue, &event, &higher_woken);
        if (higher_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        (void)xQueueSend(s_touch_queue, &event, 0);
    }
}

static void lvgl_tick_task(void *arg)
{
    (void)arg;
    const TickType_t delay = pdMS_TO_TICKS(1);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, delay);
        lv_tick_inc(1);
    }
}

static void lvgl_render_task(void *arg)
{
    (void)arg;
    const TickType_t refresh_ticks = pdMS_TO_TICKS(LVGL_RENDER_REFRESH_MS);

    while (true) {
        if (s_render_sem) {
            xSemaphoreTake(s_render_sem, refresh_ticks);
        } else {
            vTaskDelay(refresh_ticks);
        }

        lvgl_port_lock();
        uint32_t wait_ms = lv_timer_handler();
        lvgl_port_unlock();

        if (wait_ms > 0) {
            TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
            if (wait_ticks > refresh_ticks) {
                wait_ticks = refresh_ticks;
            }
            vTaskDelay(wait_ticks);
        } else {
            taskYIELD();
        }
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;

    int32_t width = area->x2 - area->x1 + 1;
    int32_t height = area->y2 - area->y1 + 1;
    if (width <= 0 || height <= 0) {
        lv_display_flush_ready(disp);
        return;
    }

    if (!waveshare_7b_lgfx_flush(area->x1, area->y1, width, height, px_map)) {
        ESP_LOGE(TAG, "LovyanGFX flush failed");
    }

    lv_display_flush_ready(disp);
}

static void lvgl_rounder_cb(lv_display_t *disp, lv_area_t *area)
{
    (void)disp;

    if (area->x1 < 0) {
        area->x1 = 0;
    }
    if (area->y1 < 0) {
        area->y1 = 0;
    }
    if (area->x2 >= (int32_t)(LVGL_PORT_HOR_RES - 1)) {
        area->x2 = LVGL_PORT_HOR_RES - 1;
    }
    if (area->y2 >= (int32_t)(LVGL_PORT_VER_RES - 1)) {
        area->y2 = LVGL_PORT_VER_RES - 1;
    }
}

static void lvgl_drv_update_cb(lv_display_t *disp)
{
    (void)disp;

    esp_err_t err = waveshare_7b_lgfx_init(LVGL_PORT_HOR_RES, LVGL_PORT_VER_RES);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LovyanGFX re-init failed: %s", esp_err_to_name(err));
    }
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    lvgl_touch_event_t event;
    while (s_touch_queue && xQueueReceive(s_touch_queue, &event, 0) == pdTRUE) {
        s_touch_state.x = event.x;
        s_touch_state.y = event.y;
        s_touch_state.pressed = event.pressed;
    }

    data->point.x = s_touch_state.x;
    data->point.y = s_touch_state.y;
    data->state = s_touch_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

static void lvgl_port_reset_state(void)
{
    if (s_tick_task) {
        vTaskDelete(s_tick_task);
        s_tick_task = NULL;
    }
    if (s_render_task) {
        vTaskDelete(s_render_task);
        s_render_task = NULL;
    }
    if (s_touch_indev) {
        lv_indev_delete(s_touch_indev);
        s_touch_indev = NULL;
    }
    if (s_display) {
        lv_display_delete(s_display);
        s_display = NULL;
    }
    if (s_touch_queue) {
        vQueueDelete(s_touch_queue);
        s_touch_queue = NULL;
    }
    if (s_render_sem) {
        vSemaphoreDelete(s_render_sem);
        s_render_sem = NULL;
    }
    for (size_t i = 0; i < 2; ++i) {
        if (s_framebuffers[i]) {
            heap_caps_free(s_framebuffers[i]);
            s_framebuffers[i] = NULL;
        }
    }
    s_framebuffer_size = 0;
    memset(&s_touch_state, 0, sizeof(s_touch_state));

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
}
