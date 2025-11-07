#include <LovyanGFX.hpp>

#include "bsp/pins_lcd.h"
#include "bsp/waveshare_7b_lgfx.h"

#include "esp_err.h"
#include "esp_log.h"

class LGFXWaveshare7B : public lgfx::LGFX_Device {
public:
    LGFXWaveshare7B()
    {
        using namespace lgfx::v1;

        auto detail_cfg = _panel.config_detail();
        detail_cfg.use_psram = 2;
        _panel.config_detail(detail_cfg);

        _panel.setBus(&_bus);
        _panel.setLight(&_light);
        setPanel(&_panel);
    }

    void configure(uint16_t hor_res, uint16_t ver_res)
    {
        using namespace lgfx::v1;

        auto panel_cfg = _panel.config();
        panel_cfg.memory_width = hor_res;
        panel_cfg.memory_height = ver_res;
        panel_cfg.panel_width = hor_res;
        panel_cfg.panel_height = ver_res;
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

        auto light_cfg = _light.config();
        light_cfg.freq = 5000;
        light_cfg.pin_bl = LCD_PIN_BACKLIGHT;
        light_cfg.pwm_channel = 7;
        light_cfg.invert = false;
        _light.config(light_cfg);
        _panel.setLight(&_light);
    }

private:
    lgfx::Bus_RGB _bus;
    lgfx::Panel_RGB _panel;
    lgfx::Light_PWM _light;
};

static LGFXWaveshare7B s_lgfx;
static const char *TAG = "lgfx";

extern "C" esp_err_t waveshare_7b_lgfx_init(uint16_t hor_res, uint16_t ver_res)
{
    static bool s_first_init = true;

    s_lgfx.configure(hor_res, ver_res);

    if (!s_lgfx.init()) {
        ESP_LOGE(TAG, "LovyanGFX init failed");
        return ESP_FAIL;
    }

    if (s_first_init) {
        s_lgfx.fillScreen(0x0000);
        ESP_LOGI(TAG, "LovyanGFX RGB panel ready (%ux%u)", hor_res, ver_res);
        s_first_init = false;
    }

    return ESP_OK;
}

extern "C" bool waveshare_7b_lgfx_flush(int32_t x, int32_t y, int32_t w, int32_t h, const void *pixel_data)
{
    if (!pixel_data || w <= 0 || h <= 0) {
        return false;
    }

    s_lgfx.startWrite();
    s_lgfx.pushImage(x, y, w, h, reinterpret_cast<const uint16_t *>(pixel_data));
    s_lgfx.endWrite();
    return true;
}

extern "C" void waveshare_7b_lgfx_set_backlight(uint8_t percent)
{
#if LCD_PIN_BACKLIGHT >= 0
    if (percent > 100) {
        percent = 100;
    }
    uint32_t brightness = (percent * 255U + 50U) / 100U;
    s_lgfx.setBrightness((uint8_t)brightness);
#else
    (void)percent;
#endif
}
