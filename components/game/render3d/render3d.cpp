#include <LovyanGFX.hpp>
#include "render3d.h"

using namespace lgfx;

static LGFX lcd;
static LGFX_Sprite terrarium_sprite(&lcd);
static LGFX_Sprite decor_sprite(&lcd);
static LGFX_Sprite reptile_sprite(&lcd);

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
        init_sprite(terrarium_sprite, 160, 120, TFT_BROWN);
        init_sprite(decor_sprite,     40,  40,  TFT_DARKGREEN);
        init_sprite(reptile_sprite,   40,  20,  TFT_RED);
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

