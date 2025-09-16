#include "lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <LovyanGFX.hpp>
#include <assert.h>
#include <lvgl.h>

static SemaphoreHandle_t s_lvgl_mutex;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_RGB _panel;
  lgfx::Bus_RGB _bus;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.panel_width = 1024;
      cfg.panel_height = 600;
      cfg.pin_hsync = 46; // HSYNC
      cfg.pin_vsync = 3;  // VSYNC
      cfg.pin_de = 5;     // DE
      cfg.pin_pclk = 7;   // PCLK

      // RGB565 mapping according to Waveshare ESP32-S3 Touch LCD 7B schematic
      cfg.pin_r0 = -1;   // R0 (NC)
      cfg.pin_r1 = -1;   // R1 (NC)
      cfg.pin_r2 = -1;   // R2 (NC)
      cfg.pin_r3 = 1;    // R3
      cfg.pin_r4 = 2;    // R4
      cfg.pin_r5 = 42;   // R5
      cfg.pin_r6 = 41;   // R6
      cfg.pin_r7 = 40;   // R7

      cfg.pin_g0 = -1;   // G0 (NC)
      cfg.pin_g1 = -1;   // G1 (NC)
      cfg.pin_g2 = 39;   // G2
      cfg.pin_g3 = 0;    // G3
      cfg.pin_g4 = 45;   // G4
      cfg.pin_g5 = 48;   // G5
      cfg.pin_g6 = 47;   // G6
      cfg.pin_g7 = 21;   // G7

      cfg.pin_b0 = -1;   // B0 (NC)
      cfg.pin_b1 = -1;   // B1 (NC)
      cfg.pin_b2 = -1;   // B2 (NC)
      cfg.pin_b3 = 14;   // B3
      cfg.pin_b4 = 38;   // B4
      cfg.pin_b5 = 18;   // B5
      cfg.pin_b6 = 17;   // B6
      cfg.pin_b7 = 10;   // B7
      _bus.config(cfg);
    }
    {
      auto cfg = _panel.config();
      cfg.memory_width = 1024;
      cfg.memory_height = 600;
      cfg.panel_width = 1024;
      cfg.panel_height = 600;
      _panel.config(cfg);
    }
    _panel.setBus(&_bus);
    setPanel(&_panel);
  }
};

static LGFX gfx;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels((lgfx::rgb565_t *)&color_p->full, w * h, true);
  gfx.endWrite();
  lv_disp_flush_ready(drv);
}

void lvgl_port_init(void) {
  lv_init();
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
