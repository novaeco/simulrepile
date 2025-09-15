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

    /* Use camera position if provided. Negative translation mimics viewpoint
     * shifting in this very simple scene. */
    int16_t cam_x = cam ? -cam->x : 0;
    int16_t cam_y = cam ? -cam->y : 0;

    if (!terrarium_sprite.created()) {
        init_sprite(terrarium_sprite, 160, 120, TFT_BROWN);
        init_sprite(decor_sprite,     40,  40,  TFT_DARKGREEN);
        init_sprite(reptile_sprite,   40,  20,  TFT_RED);
    }

    lcd.startWrite();
    terrarium_sprite.pushSprite(cam_x, cam_y);
    decor_sprite.pushSprite(cam_x + 20, cam_y + 60);
    reptile_sprite.pushSprite(cam_x + 80, cam_y + 80);
    lcd.endWrite();
}

