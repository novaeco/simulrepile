#include "ui_slots.h"

#include "app_config.h"
#include "i18n.h"
#include "sim/sim_engine.h"
#include "ui_root.h"
#include "ui_theme.h"

#include <stdio.h>

static lv_obj_t *s_slot_buttons[APP_MAX_TERRARIUMS];
static lv_obj_t *s_slot_labels[APP_MAX_TERRARIUMS];

static void slot_event_cb(lv_event_t *e)
{
    size_t index = (size_t)lv_event_get_user_data(e);
    ui_root_show_dashboard(index);
}

void ui_slots_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(40));
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    ui_theme_style_panel(cont);

    for (size_t i = 0; i < APP_MAX_TERRARIUMS; ++i) {
        s_slot_buttons[i] = lv_btn_create(cont);
        lv_obj_set_size(s_slot_buttons[i], 230, 90);
        lv_obj_align(s_slot_buttons[i], LV_ALIGN_TOP_LEFT, 10 + (int)(i % 2) * 240, 10 + (int)(i / 2) * 100);
        lv_obj_add_event_cb(s_slot_buttons[i], slot_event_cb, LV_EVENT_CLICKED, (void *)i);

        s_slot_labels[i] = lv_label_create(s_slot_buttons[i]);
        lv_label_set_text(s_slot_labels[i], "");
    }
}

void ui_slots_refresh(size_t terrarium_index)
{
    if (terrarium_index >= APP_MAX_TERRARIUMS) {
        return;
    }
    const sim_terrarium_state_t *state = sim_engine_get_state(terrarium_index);
    if (!state) {
        lv_label_set_text(s_slot_labels[terrarium_index], i18n_translate("slots.empty"));
        return;
    }
    static char txt[96];
    snprintf(txt, sizeof(txt), "%s\n%.1f°C  %.0f%%\n%s", state->nickname, state->health.temperature_c,
             state->health.humidity_percent, state->species.display_name);
    lv_label_set_text(s_slot_labels[terrarium_index], txt);
}
