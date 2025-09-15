#include "room.h"
#include "render3d/render3d.h"
#include "lvgl.h"
#include "game.h"
#include "terrarium_ui/ui.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define GRID_SIZE 5
#define TERRARIUM_SPACING_X 200
#define TERRARIUM_SPACING_Y 150
#define ROOM_REFRESH_PERIOD_MS 500

static Camera camera = {0, 0, 100};
static lv_timer_t *refresh_timer;
static lv_obj_t *info_label;
static lv_obj_t *economy_label;

static void room_render(void);
static void update_info_panels(void);

static void build_render_descriptor(size_t index, Terrarium *out)
{
    memset(out, 0, sizeof(*out));

    size_t count = game_get_terrarium_count();
    if (index < count) {
        game_terrarium_snapshot_t snap;
        if (!game_get_terrarium_snapshot(index, &snap)) {
            return;
        }

        if (snap.name[0] != '\0') {
            strncpy(out->name, snap.name, sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = '\0';
        } else {
            snprintf(out->name, sizeof(out->name), "T%u", (unsigned)(index + 1));
        }
        if (snap.species[0] != '\0') {
            strncpy(out->species, snap.species, sizeof(out->species) - 1);
            out->species[sizeof(out->species) - 1] = '\0';
        }
        strncpy(out->decor, snap.terrarium.decor, sizeof(out->decor) - 1);
        out->decor[sizeof(out->decor) - 1] = '\0';
        strncpy(out->substrate, snap.terrarium.substrate, sizeof(out->substrate) - 1);
        out->substrate[sizeof(out->substrate) - 1] = '\0';
        out->temperature = snap.terrarium.temperature;
        out->humidity = snap.terrarium.humidity;
        out->uv_index = snap.terrarium.uv_index;
        if (snap.max_health > 0.0f) {
            out->health_ratio = snap.health / snap.max_health;
        }
        if (out->health_ratio < 0.0f) out->health_ratio = 0.0f;
        if (out->health_ratio > 1.0f) out->health_ratio = 1.0f;
        if (REPTILE_GROWTH_MATURE > 0.0f) {
            out->growth_ratio = snap.growth / REPTILE_GROWTH_MATURE;
        }
        if (out->growth_ratio < 0.0f) out->growth_ratio = 0.0f;
        if (out->growth_ratio > 1.0f) out->growth_ratio = 1.0f;
        out->inhabited = true;
        if (snap.has_reptile) {
            out->sick = snap.sick;
            out->alive = snap.alive;
        } else {
            if (out->species[0] == '\0') {
                strncpy(out->species, "Vide", sizeof(out->species) - 1);
                out->species[sizeof(out->species) - 1] = '\0';
            }
            out->sick = false;
            out->alive = true;
            out->health_ratio = 0.0f;
            out->growth_ratio = 0.0f;
        }
        out->selected = (index == game_get_current_slot());
        out->heater_on = snap.terrarium.heater_on;
        out->light_on = snap.terrarium.light_on;
        out->mist_on = snap.terrarium.mist_on;
    } else {
        /* Addition slot */
        snprintf(out->name, sizeof(out->name), "Ajouter");
        out->inhabited = false;
        out->selected = false;
    }
}

static void room_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    room_render();
}

static void terrarium_btn_event(lv_event_t *e)
{
    size_t index = (size_t)lv_event_get_user_data(e);
    size_t count = game_get_terrarium_count();

    if (index < count) {
        if (!game_select_terrarium(index)) {
            return;
        }
        camera.x = (int16_t)((index % GRID_SIZE) * TERRARIUM_SPACING_X);
        camera.y = (int16_t)((index / GRID_SIZE) * TERRARIUM_SPACING_Y);
        update_info_panels();
        room_render();
        terrarium_ui_show();
    } else if (index == count && count < GAME_MAX_TERRARIUMS) {
        size_t new_idx = game_add_terrarium();
        if (new_idx == SIZE_MAX) {
            return;
        }
        game_select_terrarium(new_idx);
        camera.x = (int16_t)((new_idx % GRID_SIZE) * TERRARIUM_SPACING_X);
        camera.y = (int16_t)((new_idx / GRID_SIZE) * TERRARIUM_SPACING_Y);
        update_info_panels();
        room_render();
        terrarium_ui_show();
    }
}

static void room_render(void)
{
    render3d_clear(0x0000);

    size_t count = game_get_terrarium_count();
    size_t render_slots = count;
    if (count < GAME_MAX_TERRARIUMS) {
        render_slots += 1; /* Reserve slot for addition */
    }
    size_t max_slots = GRID_SIZE * GRID_SIZE;
    if (render_slots > max_slots) {
        render_slots = max_slots;
    }

    for (size_t idx = 0; idx < render_slots; ++idx) {
        size_t grid_x = idx % GRID_SIZE;
        size_t grid_y = idx / GRID_SIZE;
        Camera local = {
            .x = (int16_t)(camera.x - (int32_t)grid_x * TERRARIUM_SPACING_X),
            .y = (int16_t)(camera.y - (int32_t)grid_y * TERRARIUM_SPACING_Y),
            .z = camera.z,
        };
        Terrarium desc;
        build_render_descriptor(idx, &desc);
        render_terrarium(&desc, &local);
    }

    update_info_panels();
}

static void gesture_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (refresh_timer) {
            lv_timer_del(refresh_timer);
            refresh_timer = NULL;
        }
        return;
    }

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

static void update_info_panels(void)
{
    if (info_label) {
        size_t count = game_get_terrarium_count();
        size_t current = game_get_current_slot();
        if (count == 0 || current >= count) {
            lv_label_set_text(info_label, "Aucun terrarium sélectionné");
        } else {
            game_terrarium_snapshot_t snap;
            if (game_get_terrarium_snapshot(current, &snap)) {
                float health_pct = (snap.max_health > 0.0f)
                                      ? (snap.health / snap.max_health) * 100.0f
                                      : 0.0f;
                if (health_pct < 0.0f) health_pct = 0.0f;
                if (health_pct > 100.0f) health_pct = 100.0f;
                float growth_pct = (REPTILE_GROWTH_MATURE > 0.0f)
                                       ? (snap.growth / REPTILE_GROWTH_MATURE) * 100.0f
                                       : 0.0f;
                if (growth_pct < 0.0f) growth_pct = 0.0f;
                if (growth_pct > 100.0f) growth_pct = 100.0f;
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "%s\n%s\nSanté: %.1f%%\nCroissance: %.1f%%\nT: %.1f°C  H: %.1f%%\nUV: %.1f",
                         snap.name[0] ? snap.name : snap.species[0] ? snap.species : "Terrarium",
                         snap.species[0] ? snap.species : "Aucun reptile",
                         health_pct, growth_pct,
                         snap.terrarium.temperature,
                         snap.terrarium.humidity,
                         snap.terrarium.uv_index);
                lv_label_set_text(info_label, buf);
            }
        }
    }

    if (economy_label) {
        const economy_t *eco = game_get_economy();
        if (eco) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Jour %u\nBudget: %.2f €\nBien-être: %.1f",
                     (unsigned)eco->day, eco->budget, eco->wellbeing);
            lv_label_set_text(economy_label, buf);
        }
    }
}

void room_show(void)
{
    if (refresh_timer) {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
    info_label = NULL;
    economy_label = NULL;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(scr, gesture_handler, LV_EVENT_ALL, NULL);
    lv_scr_load(scr);

    camera.x = camera.y = 0;
    camera.z = 100;

    size_t count = game_get_terrarium_count();
    size_t add_slot = (count < GAME_MAX_TERRARIUMS) ? count : SIZE_MAX;

    for (size_t y = 0; y < GRID_SIZE; ++y) {
        for (size_t x = 0; x < GRID_SIZE; ++x) {
            size_t idx = y * GRID_SIZE + x;
            lv_obj_t *btn = lv_btn_create(scr);
            lv_obj_set_size(btn, 110, 70);
            lv_obj_set_pos(btn, x * 115, y * 85);
            lv_obj_add_event_cb(btn, terrarium_btn_event, LV_EVENT_CLICKED, (void *)idx);

            lv_obj_t *label = lv_label_create(btn);
            if (idx < count) {
                game_terrarium_snapshot_t snap;
                if (game_get_terrarium_snapshot(idx, &snap) && snap.name[0]) {
                    lv_label_set_text(label, snap.name);
                } else {
                    lv_label_set_text_fmt(label, "T%u", (unsigned)(idx + 1));
                }
            } else if (idx == add_slot) {
                lv_label_set_text(label, "+");
            } else {
                lv_label_set_text(label, "");
                lv_obj_add_state(btn, LV_STATE_DISABLED);
            }

            if (idx == game_get_current_slot()) {
                lv_obj_add_state(btn, LV_STATE_CHECKED);
            }

            lv_obj_center(label);
        }
    }

    info_label = lv_label_create(scr);
    lv_obj_set_width(info_label, 220);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(info_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    economy_label = lv_label_create(scr);
    lv_obj_set_width(economy_label, 220);
    lv_label_set_long_mode(economy_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(economy_label, LV_ALIGN_TOP_RIGHT, -10, 140);

    refresh_timer = lv_timer_create(room_refresh_cb, ROOM_REFRESH_PERIOD_MS, NULL);

    room_render();
}
