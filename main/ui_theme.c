#include "ui_theme.h"

#include <stdbool.h>

#include "image.h"

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_16);

typedef struct {
  lv_style_t bg;
  lv_style_t card;
  lv_style_t title;
  lv_style_t body;
  lv_style_t caption;
  lv_style_t table_header;
  lv_style_t table_cell;
  lv_style_t table_cell_dense;
  lv_style_t table_cell_selected;
  lv_style_t button_base;
  lv_style_t button_primary;
  lv_style_t button_primary_pressed;
  lv_style_t button_secondary;
  lv_style_t button_secondary_pressed;
  lv_style_t dropdown_main;
  lv_style_t nav_card;
  lv_style_t nav_card_pressed;
  lv_style_t nav_card_icon;
} ui_theme_styles_t;

static ui_theme_styles_t s_styles;
static bool s_initialized;

static void ui_theme_init_styles(void) {
  if (s_initialized)
    return;
  s_initialized = true;

  lv_style_init(&s_styles.bg);
  lv_style_set_bg_color(&s_styles.bg, lv_color_hex(0xF3EFE2));
  lv_style_set_bg_grad_color(&s_styles.bg, lv_color_hex(0xE2F1E5));
  lv_style_set_bg_grad_dir(&s_styles.bg, LV_GRAD_DIR_VER);
  lv_style_set_bg_opa(&s_styles.bg, LV_OPA_COVER);

  lv_style_init(&s_styles.card);
  lv_style_set_bg_color(&s_styles.card, lv_color_hex(0xFFFFFF));
  lv_style_set_bg_grad_color(&s_styles.card, lv_color_hex(0xF5F8F3));
  lv_style_set_bg_grad_dir(&s_styles.card, LV_GRAD_DIR_VER);
  lv_style_set_radius(&s_styles.card, 18);
  lv_style_set_border_width(&s_styles.card, 1);
  lv_style_set_border_color(&s_styles.card, lv_color_hex(0xB7D3C2));
  lv_style_set_border_opa(&s_styles.card, LV_OPA_60);
  lv_style_set_shadow_width(&s_styles.card, 12);
  lv_style_set_shadow_ofs_y(&s_styles.card, 4);
  lv_style_set_shadow_color(&s_styles.card, lv_color_hex(0x9CBFA1));
  lv_style_set_pad_all(&s_styles.card, 20);
  lv_style_set_pad_gap(&s_styles.card, 16);

  lv_style_init(&s_styles.title);
  lv_style_set_text_font(&s_styles.title, &lv_font_montserrat_24);
  lv_style_set_text_color(&s_styles.title, lv_color_hex(0x204D3B));

  lv_style_init(&s_styles.body);
  lv_style_set_text_font(&s_styles.body, &lv_font_montserrat_20);
  lv_style_set_text_color(&s_styles.body, lv_color_hex(0x2F4F43));
  lv_style_set_text_line_space(&s_styles.body, 4);

  lv_style_init(&s_styles.caption);
  lv_style_set_text_font(&s_styles.caption, &lv_font_montserrat_16);
  lv_style_set_text_color(&s_styles.caption, lv_color_hex(0x4C6F52));
  lv_style_set_text_line_space(&s_styles.caption, 2);

  lv_style_init(&s_styles.table_header);
  lv_style_set_bg_color(&s_styles.table_header, lv_color_hex(0xE8F2EB));
  lv_style_set_bg_opa(&s_styles.table_header, LV_OPA_COVER);
  lv_style_set_border_width(&s_styles.table_header, 1);
  lv_style_set_border_color(&s_styles.table_header, lv_color_hex(0xB5CBB5));
  lv_style_set_text_font(&s_styles.table_header, &lv_font_montserrat_20);
  lv_style_set_text_color(&s_styles.table_header, lv_color_hex(0x1F3F2E));
  lv_style_set_pad_all(&s_styles.table_header, 8);
  lv_style_set_pad_gap(&s_styles.table_header, 6);

  lv_style_init(&s_styles.table_cell);
  lv_style_set_text_font(&s_styles.table_cell, &lv_font_montserrat_20);
  lv_style_set_text_color(&s_styles.table_cell, lv_color_hex(0x264C3F));
  lv_style_set_pad_all(&s_styles.table_cell, 10);
  lv_style_set_pad_gap(&s_styles.table_cell, 6);
  lv_style_set_text_align(&s_styles.table_cell, LV_TEXT_ALIGN_LEFT);
  lv_style_set_bg_opa(&s_styles.table_cell, LV_OPA_TRANSP);

  lv_style_init(&s_styles.table_cell_dense);
  lv_style_set_text_font(&s_styles.table_cell_dense, &lv_font_montserrat_16);
  lv_style_set_text_color(&s_styles.table_cell_dense, lv_color_hex(0x264C3F));
  lv_style_set_pad_all(&s_styles.table_cell_dense, 4);
  lv_style_set_pad_gap(&s_styles.table_cell_dense, 4);
  lv_style_set_text_align(&s_styles.table_cell_dense, LV_TEXT_ALIGN_CENTER);
  lv_style_set_text_line_space(&s_styles.table_cell_dense, 2);

  lv_style_init(&s_styles.table_cell_selected);
  lv_style_set_bg_color(&s_styles.table_cell_selected, lv_color_hex(0x3A7D60));
  lv_style_set_bg_opa(&s_styles.table_cell_selected, LV_OPA_COVER);
  lv_style_set_border_width(&s_styles.table_cell_selected, 1);
  lv_style_set_border_color(&s_styles.table_cell_selected,
                            lv_color_hex(0x285542));
  lv_style_set_text_color(&s_styles.table_cell_selected, lv_color_hex(0xFFFFFF));

  lv_style_init(&s_styles.button_base);
  lv_style_set_radius(&s_styles.button_base, 14);
  lv_style_set_pad_ver(&s_styles.button_base, 14);
  lv_style_set_pad_hor(&s_styles.button_base, 24);
  lv_style_set_min_height(&s_styles.button_base, 46);
  lv_style_set_border_width(&s_styles.button_base, 1);
  lv_style_set_text_font(&s_styles.button_base, &lv_font_montserrat_20);
  lv_style_set_shadow_width(&s_styles.button_base, 8);
  lv_style_set_shadow_ofs_y(&s_styles.button_base, 3);
  lv_style_set_shadow_color(&s_styles.button_base, lv_color_hex(0xA3C9A8));
  lv_style_set_bg_opa(&s_styles.button_base, LV_OPA_COVER);

  lv_style_init(&s_styles.button_primary);
  lv_style_set_bg_color(&s_styles.button_primary, lv_color_hex(0x2A9D8F));
  lv_style_set_bg_grad_color(&s_styles.button_primary, lv_color_hex(0x1F7A70));
  lv_style_set_bg_grad_dir(&s_styles.button_primary, LV_GRAD_DIR_VER);
  lv_style_set_border_color(&s_styles.button_primary, lv_color_hex(0x1B6A5F));
  lv_style_set_text_color(&s_styles.button_primary, lv_color_hex(0xFFFFFF));

  lv_style_init(&s_styles.button_primary_pressed);
  lv_style_set_bg_color(&s_styles.button_primary_pressed,
                        lv_color_hex(0x1F7A70));
  lv_style_set_bg_grad_color(&s_styles.button_primary_pressed,
                             lv_color_hex(0x155950));
  lv_style_set_bg_grad_dir(&s_styles.button_primary_pressed, LV_GRAD_DIR_VER);
  lv_style_set_text_color(&s_styles.button_primary_pressed,
                          lv_color_hex(0xFFFFFF));

  lv_style_init(&s_styles.button_secondary);
  lv_style_set_bg_color(&s_styles.button_secondary, lv_color_hex(0xF1FAF1));
  lv_style_set_bg_grad_color(&s_styles.button_secondary,
                             lv_color_hex(0xDBEFDF));
  lv_style_set_bg_grad_dir(&s_styles.button_secondary, LV_GRAD_DIR_VER);
  lv_style_set_border_color(&s_styles.button_secondary, lv_color_hex(0x3D8361));
  lv_style_set_text_color(&s_styles.button_secondary, lv_color_hex(0x2F4F43));

  lv_style_init(&s_styles.button_secondary_pressed);
  lv_style_set_bg_color(&s_styles.button_secondary_pressed,
                        lv_color_hex(0xC7E7D3));
  lv_style_set_bg_grad_color(&s_styles.button_secondary_pressed,
                             lv_color_hex(0xB1D9C2));
  lv_style_set_bg_grad_dir(&s_styles.button_secondary_pressed,
                           LV_GRAD_DIR_VER);
  lv_style_set_text_color(&s_styles.button_secondary_pressed,
                          lv_color_hex(0x1F3F2E));

  lv_style_init(&s_styles.dropdown_main);
  lv_style_set_radius(&s_styles.dropdown_main, 12);
  lv_style_set_bg_color(&s_styles.dropdown_main, lv_color_hex(0xFFFFFF));
  lv_style_set_border_color(&s_styles.dropdown_main, lv_color_hex(0x8FBC8F));
  lv_style_set_border_width(&s_styles.dropdown_main, 1);
  lv_style_set_pad_hor(&s_styles.dropdown_main, 12);
  lv_style_set_pad_ver(&s_styles.dropdown_main, 10);
  lv_style_set_text_font(&s_styles.dropdown_main, &lv_font_montserrat_20);
  lv_style_set_text_color(&s_styles.dropdown_main, lv_color_hex(0x2F4F43));

  lv_style_init(&s_styles.nav_card);
  lv_style_set_bg_color(&s_styles.nav_card, lv_color_hex(0xFFFFFF));
  lv_style_set_bg_grad_color(&s_styles.nav_card, lv_color_hex(0xECF6F1));
  lv_style_set_bg_grad_dir(&s_styles.nav_card, LV_GRAD_DIR_VER);
  lv_style_set_border_color(&s_styles.nav_card, lv_color_hex(0x7BBF9D));
  lv_style_set_border_width(&s_styles.nav_card, 1);
  lv_style_set_radius(&s_styles.nav_card, 18);
  lv_style_set_shadow_width(&s_styles.nav_card, 16);
  lv_style_set_shadow_ofs_y(&s_styles.nav_card, 5);
  lv_style_set_shadow_color(&s_styles.nav_card, lv_color_hex(0xA8D5B6));
  lv_style_set_pad_all(&s_styles.nav_card, 24);
  lv_style_set_pad_gap(&s_styles.nav_card, 16);

  lv_style_init(&s_styles.nav_card_pressed);
  lv_style_set_bg_color(&s_styles.nav_card_pressed, lv_color_hex(0xD7EEDF));
  lv_style_set_bg_grad_color(&s_styles.nav_card_pressed, lv_color_hex(0xC1E4D0));
  lv_style_set_shadow_ofs_y(&s_styles.nav_card_pressed, 2);
  lv_style_set_shadow_width(&s_styles.nav_card_pressed, 10);

  lv_style_init(&s_styles.nav_card_icon);
  lv_style_set_text_font(&s_styles.nav_card_icon, &lv_font_montserrat_24);
  lv_style_set_text_color(&s_styles.nav_card_icon, lv_color_hex(0x2A9D8F));
}

void ui_theme_init(void) { ui_theme_init_styles(); }

void ui_theme_apply_screen(lv_obj_t *screen) {
  ui_theme_init_styles();
  if (!screen)
    return;
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(screen, &s_styles.bg, 0);
}

lv_obj_t *ui_theme_create_card(lv_obj_t *parent) {
  ui_theme_init_styles();
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &s_styles.card, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_gap(card, 16, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 20, LV_PART_MAIN);
  return card;
}

void ui_theme_apply_title(lv_obj_t *label) {
  ui_theme_init_styles();
  if (!label)
    return;
  lv_obj_add_style(label, &s_styles.title, 0);
}

void ui_theme_apply_body(lv_obj_t *label) {
  ui_theme_init_styles();
  if (!label)
    return;
  lv_obj_add_style(label, &s_styles.body, 0);
}

void ui_theme_apply_caption(lv_obj_t *label) {
  ui_theme_init_styles();
  if (!label)
    return;
  lv_obj_add_style(label, &s_styles.caption, 0);
}

lv_obj_t *ui_theme_create_button(lv_obj_t *parent, const char *text,
                                 ui_theme_button_kind_t kind,
                                 lv_event_cb_t event_cb, void *user_data) {
  ui_theme_init_styles();
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_add_style(btn, &s_styles.button_base, LV_PART_MAIN);
  if (kind == UI_THEME_BUTTON_PRIMARY) {
    lv_obj_add_style(btn, &s_styles.button_primary, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_styles.button_primary_pressed,
                     LV_PART_MAIN | LV_STATE_PRESSED);
  } else {
    lv_obj_add_style(btn, &s_styles.button_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_styles.button_secondary_pressed,
                     LV_PART_MAIN | LV_STATE_PRESSED);
  }
  if (event_cb) {
    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, user_data);
  }
  lv_obj_t *label = lv_label_create(btn);
  if (text) {
    lv_label_set_text(label, text);
  } else {
    lv_label_set_text(label, "");
  }
  if (kind == UI_THEME_BUTTON_PRIMARY) {
    lv_obj_add_style(label, &s_styles.body, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  } else {
    lv_obj_add_style(label, &s_styles.body, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x2F4F43), 0);
  }
  lv_obj_center(label);
  return btn;
}

lv_obj_t *ui_theme_create_nav_card(lv_obj_t *parent, const char *title,
                                   const char *subtitle,
                                   const void *icon_src,
                                   ui_theme_nav_icon_kind_t icon_kind,
                                   lv_event_cb_t event_cb,
                                   void *user_data) {
  ui_theme_init_styles();
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(card, &s_styles.nav_card, LV_PART_MAIN);
  lv_obj_add_style(card, &s_styles.nav_card_pressed,
                   LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(card, 16, LV_PART_MAIN);
  lv_obj_set_style_min_width(card, 240, LV_PART_MAIN);
  lv_obj_set_style_max_width(card, 360, LV_PART_MAIN);
  lv_obj_set_flex_grow(card, 1);

  if (event_cb) {
    lv_obj_add_event_cb(card, event_cb, LV_EVENT_CLICKED, user_data);
  }

  lv_obj_t *icon = NULL;
  if (icon_kind == UI_THEME_NAV_ICON_IMAGE && icon_src) {
    icon = lv_img_create(card);
    lv_img_set_src(icon, icon_src);
    lv_obj_set_style_align_self(icon, LV_ALIGN_CENTER, 0);
  } else if (icon_kind == UI_THEME_NAV_ICON_SYMBOL && icon_src) {
    icon = lv_label_create(card);
    lv_obj_add_style(icon, &s_styles.nav_card_icon, 0);
    lv_label_set_text(icon, (const char *)icon_src);
    lv_obj_set_style_align_self(icon, LV_ALIGN_CENTER, 0);
  }

  if (title) {
    lv_obj_t *title_label = lv_label_create(card);
    ui_theme_apply_title(title_label);
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
  }

  if (subtitle) {
    lv_obj_t *subtitle_label = lv_label_create(card);
    ui_theme_apply_caption(subtitle_label);
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(subtitle_label, subtitle);
    lv_obj_set_width(subtitle_label, LV_PCT(100));
  }

  return card;
}

void ui_theme_apply_table(lv_obj_t *table, ui_theme_table_mode_t mode) {
  ui_theme_init_styles();
  if (!table)
    return;
  lv_obj_add_style(table, &s_styles.table_header, LV_PART_ITEMS);
  if (mode == UI_THEME_TABLE_DENSE) {
    lv_obj_add_style(table, &s_styles.table_cell_dense, LV_PART_ITEMS);
  } else {
    lv_obj_add_style(table, &s_styles.table_cell, LV_PART_ITEMS);
  }
  lv_obj_add_style(table, &s_styles.table_cell_selected,
                   LV_PART_ITEMS | LV_STATE_USER_1);
}

void ui_theme_apply_dropdown(lv_obj_t *dd) {
  ui_theme_init_styles();
  if (!dd)
    return;
  lv_obj_add_style(dd, &s_styles.dropdown_main, LV_PART_MAIN);
  lv_obj_add_style(dd, &s_styles.dropdown_main,
                   LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_add_style(dd, &s_styles.dropdown_main,
                   LV_PART_MAIN | LV_STATE_PRESSED);
}

const lv_image_dsc_t *ui_theme_get_icon(ui_theme_icon_t icon) {
  switch (icon) {
  case UI_THEME_ICON_TERRARIUM_OK:
    return &gImage_terrarium_ok;
  case UI_THEME_ICON_TERRARIUM_ALERT:
    return &gImage_terrarium_alert;
  case UI_THEME_ICON_CURRENCY:
    return &gImage_currency_card;
  default:
    break;
  }
  return NULL;
}

