#include "lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <lvgl.h>

static SemaphoreHandle_t s_lvgl_mutex;
static bool s_lvgl_port_initialized;

LGFX &lgfx_get_display(void) {
  static LGFX display;
  return display;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  LGFX &gfx = lgfx_get_display();
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels((lgfx::rgb565_t *)&color_p->full, w * h, true);
  gfx.endWrite();
  lv_disp_flush_ready(drv);
}

void lvgl_port_init(void) {
  if (s_lvgl_port_initialized) {
    ESP_LOGW("lvgl", "lvgl_port_init called multiple times; ignoring");
    return;
  }

  lv_init();
  LGFX &gfx = lgfx_get_display();
  gfx.init();

  s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
  assert(s_lvgl_mutex != nullptr);

  size_t buf_sz = 1024 * 40; // 1/15 of screen
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
      buf_sz * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
      buf_sz * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  assert(buf1 && esp_ptr_external_ram(buf1));
  assert(buf2 && esp_ptr_external_ram(buf2));
  ESP_LOGI("lvgl", "Draw buffers allocated in PSRAM: %p %p", buf1, buf2);
  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_sz);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 1024;
  disp_drv.ver_res = 600;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  s_lvgl_port_initialized = true;
}

bool lvgl_port_lock(uint32_t timeout_ms) {
  if (s_lvgl_mutex == nullptr) {
    return false;
  }
  TickType_t timeout_ticks = timeout_ms == LVGL_PORT_LOCK_INFINITE
                                 ? portMAX_DELAY
                                 : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(s_lvgl_mutex, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void) {
  if (s_lvgl_mutex != nullptr) {
    xSemaphoreGiveRecursive(s_lvgl_mutex);
  }
}
