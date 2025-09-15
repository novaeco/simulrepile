#include "room.h"
#include "render3d/render3d.h"
#include "lvgl.h"
#include "game.h"
#include "terrarium_ui/ui.h"

#define GRID_SIZE 5
#define TERRARIUM_SPACING_X 200
#define TERRARIUM_SPACING_Y 150

static Camera camera = {0, 0, 100};

static void terrarium_btn_event(lv_event_t *e) {
    size_t index = (size_t)lv_event_get_user_data(e);
    if (index < game_get_terrarium_count()) {
        game_select_terrarium(index);
    } else if (index == game_get_terrarium_count()) {
        size_t new_idx = game_add_terrarium();
        if (new_idx != SIZE_MAX) {
            game_select_terrarium(new_idx);
        } else {
            return;
        }
    } else {
        return;
    }
    terrarium_ui_show();
}

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
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            size_t idx = y * GRID_SIZE + x;
            lv_obj_t *btn = lv_btn_create(scr);
            lv_obj_set_size(btn, 100, 80);
            lv_obj_set_pos(btn, x * 110, y * 90);
            lv_obj_add_event_cb(btn, terrarium_btn_event, LV_EVENT_CLICKED, (void *)idx);
            lv_obj_t *label = lv_label_create(btn);
            if (idx < game_get_terrarium_count()) {
                lv_label_set_text_fmt(label, "T%u", (unsigned)(idx + 1));
            } else {
                lv_label_set_text(label, "+");
            }
            lv_obj_center(label);
        }
    }
    room_render();
}
