#include "ui_dashboard.h"

#include "app_config.h"
#include "i18n.h"
#include "sim/sim_engine.h"
#include "ui_theme.h"

#include <stdio.h>

static lv_obj_t *s_container;
static lv_obj_t *s_labels[APP_MAX_TERRARIUMS];

void ui_dashboard_init(lv_obj_t *parent)
{
    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(60));
    lv_obj_align(s_container, LV_ALIGN_TOP_MID, 0, 0);
    ui_theme_style_panel(s_container);

    for (size_t i = 0; i < APP_MAX_TERRARIUMS; ++i) {
        s_labels[i] = lv_label_create(s_container);
        lv_obj_align(s_labels[i], LV_ALIGN_TOP_LEFT, 8, 8 + (int)i * 60);
        lv_label_set_text(s_labels[i], "Terrarium --");
    }
}

void ui_dashboard_show(size_t terrarium_index)
{
    (void)terrarium_index;
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_dashboard_refresh(size_t terrarium_index)
{
    if (terrarium_index >= APP_MAX_TERRARIUMS) {
        return;
    }
    const sim_terrarium_state_t *state = sim_engine_get_state(terrarium_index);
    if (!state) {
        lv_label_set_text(s_labels[terrarium_index], "Slot empty");
        return;
    }
    static char text[128];
    snprintf(text, sizeof(text), "%s\nT:%.1f°C H:%.1f%% UV:%.1f",
             state->nickname,
             state->health.temperature_c,
             state->health.humidity_percent,
             state->health.uv_index);
    lv_label_set_text(s_labels[terrarium_index], text);
}
