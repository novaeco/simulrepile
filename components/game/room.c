#include "room.h"
#include "render3d/render3d.h"
#include "lvgl.h"

#define GRID_SIZE 5
#define TERRARIUM_SPACING_X 200
#define TERRARIUM_SPACING_Y 150

static Camera camera = {0, 0, 100};

static void room_render(void)
{
    render3d_clear(0x0000); /* black background */
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            Camera local = {
                .x = camera.x - x * TERRARIUM_SPACING_X,
                .y = camera.y - y * TERRARIUM_SPACING_Y,
                .z = camera.z,
            };
            render_terrarium(NULL, &local);
        }
    }
}

static void gesture_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_USER_1) {
        const lv_point_t *d = lv_event_get_param(e);
        camera.x -= d->x;
        camera.y -= d->y;
        room_render();
    } else if (code == LV_EVENT_USER_2) {
        const int *diff = lv_event_get_param(e);
        camera.z += *diff;
        if (camera.z < 50) camera.z = 50;
        if (camera.z > 200) camera.z = 200;
        room_render();
    }
}

void room_show(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(scr, gesture_handler, LV_EVENT_ALL, NULL);
    lv_scr_load(scr);
    camera.x = camera.y = 0;
    camera.z = 100;
    room_render();
}
