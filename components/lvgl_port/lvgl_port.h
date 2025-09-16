#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#include <LovyanGFX.hpp>

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

extern "C" {
#endif

void lvgl_port_init(void);

/**
 * @brief Acquire the LVGL core mutex.
 *
 * This helper allows tasks other than the GUI task to access LVGL while the
 * GUI task is paused. Pass UINT32_MAX to wait indefinitely.
 *
 * @param timeout_ms Maximum time to wait in milliseconds. Use 0 for a
 *                   non-blocking attempt or UINT32_MAX for an infinite wait.
 * @return true when the mutex has been acquired, false otherwise.
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/**
 * @brief Release the LVGL core mutex previously acquired with lvgl_port_lock().
 */
void lvgl_port_unlock(void);

#define LVGL_PORT_LOCK_INFINITE UINT32_MAX

#ifdef __cplusplus
}

LGFX &lgfx_get_display(void);
#endif
