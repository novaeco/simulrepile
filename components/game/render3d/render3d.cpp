#include <LovyanGFX.hpp>
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
    int cx = cam_x + (int)(terrarium_sprite.width() * zoom / 2.0f);
    int cy = cam_y + (int)(terrarium_sprite.height() * zoom / 2.0f);
    terrarium_sprite.pushRotateZoom(cx, cy, 0, zoom, zoom);

    int decor_cx = cam_x + (int)((20 + decor_sprite.width() / 2.0f) * zoom);
    int decor_cy = cam_y + (int)((60 + decor_sprite.height() / 2.0f) * zoom);
    decor_sprite.pushRotateZoom(decor_cx, decor_cy, 0, zoom, zoom);

    int rept_cx = cam_x + (int)((80 + reptile_sprite.width() / 2.0f) * zoom);
    int rept_cy = cam_y + (int)((80 + reptile_sprite.height() / 2.0f) * zoom);
    reptile_sprite.pushRotateZoom(rept_cx, rept_cy, 0, zoom, zoom);
    lcd.endWrite();
}

void render3d_clear(uint16_t color)
{
    lcd.startWrite();
    lcd.fillScreen(color);
    lcd.endWrite();
}

