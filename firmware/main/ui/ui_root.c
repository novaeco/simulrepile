#include "ui_root.h"

#include "app_config.h"
#include "asset_manager.h"
#include "doc_reader.h"
#include "i18n.h"
#include "logging/log_manager.h"
#include "persist/save_manager.h"
#include "sim/sim_engine.h"
#include "ui_dashboard.h"
#include "ui_docs.h"
#include "ui_settings.h"
#include "ui_slots.h"
#include "ui_theme.h"

#include "esp_log.h"

static const char *TAG = "ui_root";

static lv_obj_t *s_root = NULL;
static lv_disp_t *s_disp = NULL;

static size_t find_state_index(const sim_terrarium_state_t *state)
{
    for (size_t i = 0; i < sim_engine_terrarium_count(); ++i) {
        if (sim_engine_get_state(i) == state) {
            return i;
        }
    }
    return 0;
}

static void on_sim_update(const sim_terrarium_state_t *state, void *user_ctx)
{
    (void)user_ctx;
    size_t index = find_state_index(state);
    ui_root_update_terrarium(index);
}

void ui_root_init(lv_disp_t *disp)
{
    if (!disp) {
        ESP_LOGE(TAG, "Display not provided");
        return;
    }

    s_disp = disp;
    s_root = lv_obj_create(lv_disp_get_scr_act(disp));
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    ui_theme_apply_root(s_root);

    ui_slots_create(s_root);
    ui_dashboard_init(s_root);
    ui_docs_init(s_root);
    ui_settings_init(s_root);

    log_manager_info("UI root initialised");

    sim_engine_register_event_callback(on_sim_update, NULL);
}

void ui_root_show_dashboard(size_t terrarium_index)
{
    ui_dashboard_show(terrarium_index);
}

void ui_root_show_documents(void)
{
    ui_docs_show();
}

void ui_root_show_settings(void)
{
    ui_settings_show();
}

void ui_root_update_terrarium(size_t index)
{
    ui_dashboard_refresh(index);
    ui_slots_refresh(index);
}
