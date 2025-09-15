#include <LovyanGFX.hpp>
#include <cstdio>
#include "render3d.h"
#include "assets.h"

using namespace lgfx;

static LGFX lcd;
static LGFX_Sprite terrarium_sprite(&lcd);
static LGFX_Sprite decor_sprite(&lcd);
static LGFX_Sprite reptile_sprite(&lcd);

static asset_blob_t terrarium_tex;
static asset_blob_t decor_tex;
static asset_blob_t reptile_tex;

extern const unsigned char _binary_assets_textures_terrarium_bin_start[];
extern const unsigned char _binary_assets_textures_terrarium_bin_end[];
extern const unsigned char _binary_assets_textures_decor_bin_start[];
extern const unsigned char _binary_assets_textures_decor_bin_end[];
extern const unsigned char _binary_assets_textures_reptile_bin_start[];
extern const unsigned char _binary_assets_textures_reptile_bin_end[];

static bool load_texture(asset_blob_t *asset, const char *sd_path,
                         const unsigned char *blob_start, const unsigned char *blob_end)
{
    if (assets_load_sd(sd_path, asset)) {
        return true;
    }
    size_t size = blob_end - blob_start;
    return assets_load_embedded(blob_start, size, asset);
}

static void init_sprite_with_texture(LGFX_Sprite &spr, int w, int h, uint16_t color,
                                    asset_blob_t *tex)
{
    init_sprite(spr, w, h, color);
    if (tex->data && tex->size >= (size_t)(w * h * 2)) {
        spr.pushImage(0, 0, w, h, (const uint16_t *)tex->data);
    }
}

static void init_sprite(LGFX_Sprite &spr, int w, int h, uint16_t color)
{
    spr.setPsram(true);                 // allocate buffer in PSRAM
    spr.setColorDepth(16);              // RGB565
    spr.createSprite(w, h);
    spr.fillSprite(color);
}

static float clampf(float v, float min_v, float max_v)
{
    if (v < min_v) {
        return min_v;
    }
    if (v > max_v) {
        return max_v;
    }
    return v;
}

static uint16_t status_color(float ratio)
{
    if (ratio >= 0.75f) {
        return TFT_GREEN;
    }
    if (ratio >= 0.45f) {
        return TFT_YELLOW;
    }
    return TFT_RED;
}

void render_terrarium(Terrarium *t, Camera *cam)
{
    (void)t;

    float zoom = cam ? cam->z / 100.0f : 1.0f;
    int16_t cam_x = cam ? (int16_t)(-cam->x * zoom) : 0;
    int16_t cam_y = cam ? (int16_t)(-cam->y * zoom) : 0;

    if (!terrarium_sprite.created()) {
        load_texture(&terrarium_tex, "/sdcard/textures/terrarium.bin",
                     _binary_assets_textures_terrarium_bin_start,
                     _binary_assets_textures_terrarium_bin_end);
        load_texture(&decor_tex, "/sdcard/textures/decor.bin",
                     _binary_assets_textures_decor_bin_start,
                     _binary_assets_textures_decor_bin_end);
        load_texture(&reptile_tex, "/sdcard/textures/reptile.bin",
                     _binary_assets_textures_reptile_bin_start,
                     _binary_assets_textures_reptile_bin_end);

        init_sprite_with_texture(terrarium_sprite, 160, 120, TFT_BROWN, &terrarium_tex);
        init_sprite_with_texture(decor_sprite,     40,  40,  TFT_DARKGREEN, &decor_tex);
        init_sprite_with_texture(reptile_sprite,   40,  20,  TFT_RED, &reptile_tex);
    }

    lcd.startWrite();
    int sprite_w = terrarium_sprite.width();
    int sprite_h = terrarium_sprite.height();
    int scaled_w = (int)(sprite_w * zoom);
    int scaled_h = (int)(sprite_h * zoom);
    int cx = cam_x + scaled_w / 2;
    int cy = cam_y + scaled_h / 2;
    terrarium_sprite.pushRotateZoom(cx, cy, 0, zoom, zoom);

    int decor_cx = cam_x + (int)((20 + decor_sprite.width() / 2.0f) * zoom);
    int decor_cy = cam_y + (int)((60 + decor_sprite.height() / 2.0f) * zoom);
    decor_sprite.pushRotateZoom(decor_cx, decor_cy, 0, zoom, zoom);

    int rept_cx = cam_x + (int)((80 + reptile_sprite.width() / 2.0f) * zoom);
    int rept_cy = cam_y + (int)((80 + reptile_sprite.height() / 2.0f) * zoom);
    reptile_sprite.pushRotateZoom(rept_cx, rept_cy, 0, zoom, zoom);

    int top_left_x = cam_x;
    int top_left_y = cam_y;
    if (scaled_w > 0 && scaled_h > 0) {
        top_left_x = cam_x - scaled_w / 2;
        top_left_y = cam_y - scaled_h / 2;
    }

    if (t) {
        uint16_t border_color = t->selected ? TFT_YELLOW : TFT_DARKGREY;
        lcd.drawRect(top_left_x, top_left_y, scaled_w, scaled_h, border_color);

        lcd.setTextWrap(false);
        lcd.setTextSize(1);
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);

        const char *name = (t->name[0] != '\0') ? t->name : "Terrarium";
        lcd.setCursor(top_left_x + 4, top_left_y + 10);
        lcd.print(name);

        if (t->inhabited) {
            lcd.setCursor(top_left_x + 4, top_left_y + 22);
            lcd.print(t->species);

            char env_line[48];
            std::snprintf(env_line, sizeof(env_line), "%.0fC %.0f%% UV%.1f",
                          t->temperature, t->humidity, t->uv_index);
            lcd.setCursor(top_left_x + 4, top_left_y + scaled_h - 40);
            lcd.print(env_line);

            float health = clampf(t->health_ratio, 0.0f, 1.0f);
            int bar_w = scaled_w - 8;
            int bar_x = top_left_x + 4;
            int bar_y = top_left_y + scaled_h - 28;
            lcd.drawRect(bar_x, bar_y, bar_w, 6, TFT_DARKGREY);
            int filled = (int)((bar_w - 2) * health);
            if (filled > 0) {
                lcd.fillRect(bar_x + 1, bar_y + 1, filled, 4, status_color(health));
            }

            float growth = clampf(t->growth_ratio, 0.0f, 1.0f);
            bar_y += 8;
            lcd.drawRect(bar_x, bar_y, bar_w, 6, TFT_DARKGREY);
            filled = (int)((bar_w - 2) * growth);
            if (filled > 0) {
                lcd.fillRect(bar_x + 1, bar_y + 1, filled, 4, TFT_SKYBLUE);
            }

            int indicator_y = top_left_y + scaled_h - 12;
            int indicator_x = top_left_x + 4;
            lcd.fillRect(indicator_x, indicator_y, 12, 6,
                         t->heater_on ? TFT_ORANGE : TFT_DARKGREY);
            lcd.fillRect(indicator_x + 16, indicator_y, 12, 6,
                         t->light_on ? TFT_YELLOW : TFT_DARKGREY);
            lcd.fillRect(indicator_x + 32, indicator_y, 12, 6,
                         t->mist_on ? TFT_CYAN : TFT_DARKGREY);

            if (!t->alive) {
                lcd.drawLine(top_left_x, top_left_y, top_left_x + scaled_w,
                             top_left_y + scaled_h, TFT_RED);
                lcd.drawLine(top_left_x, top_left_y + scaled_h,
                             top_left_x + scaled_w, top_left_y, TFT_RED);
            } else if (t->sick) {
                lcd.drawRect(top_left_x + 2, top_left_y + 2,
                             scaled_w - 4, scaled_h - 4, TFT_ORANGE);
            }
        } else {
            lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            lcd.setCursor(top_left_x + scaled_w / 2 - 6,
                          top_left_y + scaled_h / 2 - 8);
            lcd.print("+");
        }
    }

    lcd.endWrite();
}

void render3d_clear(uint16_t color)
{
    lcd.startWrite();
    lcd.fillScreen(color);
    lcd.endWrite();
}

