#include "ui.h"
#include "lvgl.h"
#include "reptiles.h"
#include "terrarium.h"
#include "room.h"
#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static lv_obj_t *stats_label;
static lv_timer_t *stats_timer;

static void update_stats_label(void);
static void screen_delete_event(lv_event_t *e);
static void stats_timer_cb(lv_timer_t *timer);
static void heater_event_cb(lv_event_t *e);
static void light_event_cb(lv_event_t *e);
static void mist_event_cb(lv_event_t *e);
static void name_event_cb(lv_event_t *e);
static void phase_event_cb(lv_event_t *e);

static void species_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[64];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    const reptile_info_t *info = reptiles_find(buf);
    if (info) {
        game_set_reptile(info);
        game_commit_current_terrarium();
        update_stats_label();
    }
}

static void decor_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[32];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    terrarium_set_decor(buf);
    game_commit_current_terrarium();
    update_stats_label();
}

static void substrate_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[32];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    terrarium_set_substrate(buf);
    game_commit_current_terrarium();
    update_stats_label();
}

static void equipment_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_list_get_btn_text(btn);
    terrarium_add_equipment(txt);
    game_commit_current_terrarium();
    update_stats_label();
}

static void heater_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    terrarium_set_heater(on);
    game_commit_current_terrarium();
    update_stats_label();
}

static void light_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    terrarium_set_light(on);
    game_commit_current_terrarium();
    update_stats_label();
}

static void mist_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    terrarium_set_mist(on);
    game_commit_current_terrarium();
    update_stats_label();
}

static void name_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    const char *txt = lv_textarea_get_text(ta);
    if (txt) {
        game_set_terrarium_name(txt);
        update_stats_label();
    }
}

static void phase_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *label = lv_event_get_user_data(e);
    int value = lv_slider_get_value(slider);
    if (value < 0) value = 0;
    if (value > 240) value = 240;
    float hours = value / 10.0f;
    game_set_terrarium_phase_offset(hours);
    if (label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Décalage: %.1fh", hours);
        lv_label_set_text(label, buf);
    }
    update_stats_label();
}

static void start_event_cb(lv_event_t *e)
{
    (void)e;
    game_commit_current_terrarium();
    game_save();
    room_show();
}

static void delete_event_cb(lv_event_t *e)
{
    (void)e;
    game_remove_terrarium(game_get_current_slot());
    game_save();
    room_show();
}

static void update_stats_label(void)
{
    if (!stats_label)
        return;

    size_t current = game_get_current_slot();
    game_terrarium_snapshot_t snap;
    if (!game_get_terrarium_snapshot(current, &snap)) {
        lv_label_set_text(stats_label, "");
        return;
    }

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
             "Santé: %.1f%%\nCroissance: %.1f%%\nTempérature: %.1f°C\nHumidité: %.1f%%\nUV: %.1f",
             health_pct, growth_pct,
             snap.terrarium.temperature,
             snap.terrarium.humidity,
             snap.terrarium.uv_index);
    lv_label_set_text(stats_label, buf);
}

static void stats_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_stats_label();
}

static void screen_delete_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE)
        return;
    if (stats_timer) {
        lv_timer_del(stats_timer);
        stats_timer = NULL;
    }
    stats_label = NULL;
}

void terrarium_ui_show(void)
{
    if (stats_timer) {
        lv_timer_del(stats_timer);
        stats_timer = NULL;
    }
    stats_label = NULL;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(scr, screen_delete_event, LV_EVENT_DELETE, NULL);
    lv_scr_load(scr);

    game_terrarium_snapshot_t snap;
    bool have_snap = game_get_terrarium_snapshot(game_get_current_slot(), &snap);

    /* Name field */
    lv_obj_t *ta_name = lv_textarea_create(scr);
    lv_obj_set_width(ta_name, 200);
    lv_textarea_set_max_length(ta_name, TERRARIUM_ITEM_NAME_LEN - 1);
    lv_obj_align(ta_name, LV_ALIGN_TOP_MID, 0, 10);
    lv_textarea_set_placeholder_text(ta_name, "Nom du terrarium");
    if (have_snap && snap.name[0]) {
        lv_textarea_set_text(ta_name, snap.name);
    }
    lv_obj_add_event_cb(ta_name, name_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Species selector */
    lv_obj_t *dd_species = lv_dropdown_create(scr);
    lv_obj_set_width(dd_species, 200);
    size_t count = 0;
    const reptile_info_t *list = reptiles_get(&count);
    size_t len = 0;
    for (size_t i = 0; i < count; ++i) {
        len += strlen(list[i].species) + 1;
    }
    char *opts = malloc(len + 1);
    if (opts) {
        char *p = opts;
        for (size_t i = 0; i < count; ++i) {
            size_t l = strlen(list[i].species);
            memcpy(p, list[i].species, l);
            p += l;
            *p++ = (i == count - 1) ? '\0' : '\n';
        }
        lv_dropdown_set_options(dd_species, opts);
    }
    lv_obj_align(dd_species, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_event_cb(dd_species, species_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (have_snap && snap.species[0]) {
        int sel = -1;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(list[i].species, snap.species) == 0) {
                sel = (int)i;
                break;
            }
        }
        if (sel >= 0) {
            lv_dropdown_set_selected(dd_species, sel);
        }
    }
    if (opts) {
        free(opts);
    }

    /* Decor selector */
    lv_obj_t *dd_decor = lv_dropdown_create(scr);
    lv_obj_set_width(dd_decor, 200);
    lv_dropdown_set_options_static(dd_decor, "Rocks\nPlants\nCave\nBranches");
    lv_obj_align(dd_decor, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_add_event_cb(dd_decor, decor_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (have_snap && snap.terrarium.decor[0]) {
        const char *decor_opts[] = {"Rocks", "Plants", "Cave", "Branches"};
        for (size_t i = 0; i < sizeof(decor_opts) / sizeof(decor_opts[0]); ++i) {
            if (strcmp(decor_opts[i], snap.terrarium.decor) == 0) {
                lv_dropdown_set_selected(dd_decor, (int)i);
                break;
            }
        }
    }

    /* Substrate selector */
    lv_obj_t *dd_sub = lv_dropdown_create(scr);
    lv_obj_set_width(dd_sub, 200);
    lv_dropdown_set_options_static(dd_sub, "Sand\nSoil\nBark\nPaper");
    lv_obj_align(dd_sub, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_add_event_cb(dd_sub, substrate_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (have_snap && snap.terrarium.substrate[0]) {
        const char *subs_opts[] = {"Sand", "Soil", "Bark", "Paper"};
        for (size_t i = 0; i < sizeof(subs_opts) / sizeof(subs_opts[0]); ++i) {
            if (strcmp(subs_opts[i], snap.terrarium.substrate) == 0) {
                lv_dropdown_set_selected(dd_sub, (int)i);
                break;
            }
        }
    }

    /* Actuator switches */
    lv_obj_t *sw_panel = lv_obj_create(scr);
    lv_obj_set_size(sw_panel, 220, 110);
    lv_obj_align(sw_panel, LV_ALIGN_TOP_LEFT, 20, 220);
    lv_obj_clear_flag(sw_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_heat = lv_label_create(sw_panel);
    lv_label_set_text(lbl_heat, "Chauffage");
    lv_obj_align(lbl_heat, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *sw_heat = lv_switch_create(sw_panel);
    lv_obj_align(sw_heat, LV_ALIGN_TOP_RIGHT, 0, 0);
    if (have_snap && snap.terrarium.heater_on) {
        lv_obj_add_state(sw_heat, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_heat, heater_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_light = lv_label_create(sw_panel);
    lv_label_set_text(lbl_light, "Lumière");
    lv_obj_align(lbl_light, LV_ALIGN_CENTER, -60, 0);
    lv_obj_t *sw_light = lv_switch_create(sw_panel);
    lv_obj_align(sw_light, LV_ALIGN_CENTER, 60, 0);
    if (have_snap && snap.terrarium.light_on) {
        lv_obj_add_state(sw_light, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_light, light_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_mist = lv_label_create(sw_panel);
    lv_label_set_text(lbl_mist, "Brumisation");
    lv_obj_align(lbl_mist, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *sw_mist = lv_switch_create(sw_panel);
    lv_obj_align(sw_mist, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    if (have_snap && snap.terrarium.mist_on) {
        lv_obj_add_state(sw_mist, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_mist, mist_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Stats label */
    stats_label = lv_label_create(scr);
    lv_obj_set_width(stats_label, 200);
    lv_label_set_long_mode(stats_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(stats_label, LV_ALIGN_TOP_RIGHT, -20, 20);

    /* Phase slider */
    lv_obj_t *phase_label = lv_label_create(scr);
    lv_obj_align(phase_label, LV_ALIGN_BOTTOM_MID, 0, -150);
    lv_obj_t *phase_slider = lv_slider_create(scr);
    lv_slider_set_range(phase_slider, 0, 240);
    int phase_val = 0;
    if (have_snap) {
        phase_val = (int)(snap.phase_offset * 10.0f + 0.5f);
        if (phase_val < 0) phase_val = 0;
        if (phase_val > 240) phase_val = phase_val % 240;
    }
    lv_slider_set_value(phase_slider, phase_val, LV_ANIM_OFF);
    lv_obj_set_width(phase_slider, 220);
    lv_obj_align(phase_slider, LV_ALIGN_BOTTOM_MID, 0, -120);
    lv_obj_add_event_cb(phase_slider, phase_event_cb, LV_EVENT_VALUE_CHANGED, phase_label);
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Décalage: %.1fh", phase_val / 10.0f);
        lv_label_set_text(phase_label, buf);
    }

    /* Equipment list */
    lv_obj_t *list_obj = lv_list_create(scr);
    lv_obj_set_size(list_obj, 200, 100);
    lv_obj_align(list_obj, LV_ALIGN_BOTTOM_LEFT, 20, -90);
    const char *equipments[] = {"Lamp", "Thermostat", "Mister"};
    for (size_t i = 0; i < sizeof(equipments)/sizeof(equipments[0]); ++i) {
        lv_obj_t *btn = lv_list_add_btn(list_obj, NULL, equipments[i]);
        lv_obj_add_event_cb(btn, equipment_event_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Start button */
    lv_obj_t *btn_start = lv_btn_create(scr);
    lv_obj_set_size(btn_start, 100, 40);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_t *label = lv_label_create(btn_start);
    lv_label_set_text(label, "Start");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn_start, start_event_cb, LV_EVENT_CLICKED, NULL);

    /* Delete button */
    lv_obj_t *btn_del = lv_btn_create(scr);
    lv_obj_set_size(btn_del, 100, 40);
    lv_obj_align(btn_del, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_t *lbl = lv_label_create(btn_del);
    lv_label_set_text(lbl, "Delete");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_del, delete_event_cb, LV_EVENT_CLICKED, NULL);

    update_stats_label();
    stats_timer = lv_timer_create(stats_timer_cb, 500, NULL);
}

