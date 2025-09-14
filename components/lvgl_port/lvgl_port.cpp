#include "lvgl_port.h"
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "esp_heap_caps.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_RGB _panel;
  lgfx::Bus_RGB _bus;
public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.panel_width = 1024;
      cfg.panel_height = 600;
      // TODO: configure RGB pins according to board schematic
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

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels((lgfx::rgb565_t *)&color_p->full, w * h, true);
  gfx.endWrite();
  lv_disp_flush_ready(drv);
}

void lvgl_port_init(void)
{
  lv_init();
  gfx.init();

  size_t buf_sz = 1024 * 40; // 1/15 of screen
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_sz * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_sz * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
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
