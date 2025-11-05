#include <LovyanGFX.hpp>

#include "bsp/pins_lcd.h"

#include "esp_err.h"
#include "esp_log.h"

class LGFXWaveshare7B : public lgfx::LGFX_Device {
public:
    LGFXWaveshare7B()
    {
        using namespace lgfx::v1;

        auto panel_cfg = _panel.config();
        panel_cfg.memory_width = 1024;
        panel_cfg.memory_height = 600;
        panel_cfg.panel_width = 1024;
        panel_cfg.panel_height = 600;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = 0;
        panel_cfg.readable = false;
        panel_cfg.bus_shared = false;
        _panel.config(panel_cfg);

        auto bus_cfg = _bus.config();
        bus_cfg.panel = &_panel;
        bus_cfg.freq_write = 30'000'000;
        bus_cfg.pin_pclk = LCD_PIN_PCLK;
        bus_cfg.pin_vsync = LCD_PIN_VSYNC;
        bus_cfg.pin_hsync = LCD_PIN_HSYNC;
        bus_cfg.pin_henable = LCD_PIN_DE;
        bus_cfg.pin_d0 = LCD_PIN_B3;
        bus_cfg.pin_d1 = LCD_PIN_B4;
        bus_cfg.pin_d2 = LCD_PIN_B5;
        bus_cfg.pin_d3 = LCD_PIN_B6;
        bus_cfg.pin_d4 = LCD_PIN_B7;
        bus_cfg.pin_d5 = LCD_PIN_G2;
        bus_cfg.pin_d6 = LCD_PIN_G3;
        bus_cfg.pin_d7 = LCD_PIN_G4;
        bus_cfg.pin_d8 = LCD_PIN_G5;
        bus_cfg.pin_d9 = LCD_PIN_G6;
        bus_cfg.pin_d10 = LCD_PIN_G7;
        bus_cfg.pin_d11 = LCD_PIN_R3;
        bus_cfg.pin_d12 = LCD_PIN_R4;
        bus_cfg.pin_d13 = LCD_PIN_R5;
        bus_cfg.pin_d14 = LCD_PIN_R6;
        bus_cfg.pin_d15 = LCD_PIN_R7;
        bus_cfg.hsync_pulse_width = 162;
        bus_cfg.hsync_back_porch = 152;
        bus_cfg.hsync_front_porch = 48;
        bus_cfg.vsync_pulse_width = 45;
        bus_cfg.vsync_back_porch = 13;
        bus_cfg.vsync_front_porch = 3;
        bus_cfg.hsync_polarity = 0;
        bus_cfg.vsync_polarity = 0;
        bus_cfg.pclk_active_neg = 1;
        bus_cfg.de_idle_high = 0;
        bus_cfg.pclk_idle_high = 0;
        _bus.config(bus_cfg);
        _panel.setBus(&_bus);

        auto detail_cfg = _panel.config_detail();
        detail_cfg.use_psram = 2;
        _panel.config_detail(detail_cfg);

        auto light_cfg = _light.config();
        light_cfg.freq = 5000;
        light_cfg.pin_bl = LCD_PIN_BACKLIGHT;
        light_cfg.pwm_channel = 7;
        light_cfg.invert = false;
        _light.config(light_cfg);
        _panel.setLight(&_light);

        setPanel(&_panel);
    }

private:
    lgfx::Bus_RGB _bus;
    lgfx::Panel_RGB _panel;
    lgfx::Light_PWM _light;
};

static LGFXWaveshare7B s_lgfx;
static const char *TAG = "lgfx";

extern "C" esp_err_t waveshare_7b_lgfx_init(void)
{
    static bool s_initialized = false;
    if (s_initialized) {
        return ESP_OK;
    }
    if (!s_lgfx.init()) {
        ESP_LOGE(TAG, "LovyanGFX init failed");
        return ESP_FAIL;
    }
    s_lgfx.fillScreen(0x0000);
    s_initialized = true;
    ESP_LOGI(TAG, "LovyanGFX RGB panel ready");
    return ESP_OK;
}

extern "C" lgfx::LGFX_Device *waveshare_7b_get_lgfx(void)
{
    return &s_lgfx;
}
