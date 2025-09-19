#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  UI_THEME_ICON_TERRARIUM_OK = 0,
  UI_THEME_ICON_TERRARIUM_ALERT,
  UI_THEME_ICON_CURRENCY,
} ui_theme_icon_t;

typedef enum {
  UI_THEME_BUTTON_PRIMARY = 0,
  UI_THEME_BUTTON_SECONDARY,
} ui_theme_button_kind_t;

typedef enum {
  UI_THEME_NAV_ICON_SYMBOL = 0,
  UI_THEME_NAV_ICON_IMAGE,
} ui_theme_nav_icon_kind_t;

typedef enum {
  UI_THEME_TABLE_DEFAULT = 0,
  UI_THEME_TABLE_DENSE,
} ui_theme_table_mode_t;

typedef enum {
  UI_THEME_BADGE_INFO = 0,
  UI_THEME_BADGE_SUCCESS,
  UI_THEME_BADGE_WARNING,
  UI_THEME_BADGE_CRITICAL,
} ui_theme_badge_kind_t;

void ui_theme_init(void);
void ui_theme_apply_screen(lv_obj_t *screen);

lv_obj_t *ui_theme_create_card(lv_obj_t *parent);

void ui_theme_apply_title(lv_obj_t *label);
void ui_theme_apply_body(lv_obj_t *label);
void ui_theme_apply_caption(lv_obj_t *label);

lv_obj_t *ui_theme_create_button(lv_obj_t *parent, const char *text,
                                 ui_theme_button_kind_t kind,
                                 lv_event_cb_t event_cb, void *user_data);

lv_obj_t *ui_theme_create_badge(lv_obj_t *parent, ui_theme_badge_kind_t kind,
                                const char *text);
void ui_theme_badge_set_kind(lv_obj_t *badge, ui_theme_badge_kind_t kind);
void ui_theme_set_card_selected(lv_obj_t *card, bool selected);

lv_obj_t *ui_theme_create_nav_card(lv_obj_t *parent, const char *title,
                                   const char *subtitle,
                                   const void *icon_src,
                                   ui_theme_nav_icon_kind_t icon_kind,
                                   lv_event_cb_t event_cb,
                                   void *user_data);

void ui_theme_apply_table(lv_obj_t *table, ui_theme_table_mode_t mode);
void ui_theme_apply_dropdown(lv_obj_t *dd);

const lv_image_dsc_t *ui_theme_get_icon(ui_theme_icon_t icon);

#ifdef __cplusplus
}
#endif

