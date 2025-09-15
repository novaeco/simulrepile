#include "ui.h"
#include "lvgl.h"
#include "reptiles.h"
#include "terrarium.h"
#include "room.h"
#include <stdlib.h>
#include <string.h>

static void species_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[64];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    const reptile_info_t *info = reptiles_find(buf);
    if (info) {
        terrarium_set_reptile(info);
    }
}

static void decor_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[32];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    terrarium_set_decor(buf);
}

static void substrate_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[32];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    terrarium_set_substrate(buf);
}

static void equipment_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_list_get_btn_text(btn);
    terrarium_add_equipment(txt);
}

static void start_event_cb(lv_event_t *e)
{
    (void)e;
    room_show();
}

void terrarium_ui_show(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);

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
    lv_obj_align(dd_species, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_event_cb(dd_species, species_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Decor selector */
    lv_obj_t *dd_decor = lv_dropdown_create(scr);
    lv_obj_set_width(dd_decor, 200);
    lv_dropdown_set_options_static(dd_decor, "Rocks\nPlants\nCave\nBranches");
    lv_obj_align(dd_decor, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_event_cb(dd_decor, decor_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Substrate selector */
    lv_obj_t *dd_sub = lv_dropdown_create(scr);
    lv_obj_set_width(dd_sub, 200);
    lv_dropdown_set_options_static(dd_sub, "Sand\nSoil\nBark\nPaper");
    lv_obj_align(dd_sub, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_add_event_cb(dd_sub, substrate_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Equipment list */
    lv_obj_t *list_obj = lv_list_create(scr);
    lv_obj_set_size(list_obj, 200, 100);
    lv_obj_align(list_obj, LV_ALIGN_TOP_MID, 0, 170);
    const char *equipments[] = {"Lamp", "Thermostat", "Mister"};
    for (size_t i = 0; i < sizeof(equipments)/sizeof(equipments[0]); ++i) {
        lv_obj_t *btn = lv_list_add_btn(list_obj, NULL, equipments[i]);
        lv_obj_add_event_cb(btn, equipment_event_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Start button */
    lv_obj_t *btn_start = lv_btn_create(scr);
    lv_obj_set_size(btn_start, 100, 40);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *label = lv_label_create(btn_start);
    lv_label_set_text(label, "Start");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn_start, start_event_cb, LV_EVENT_CLICKED, NULL);
}

