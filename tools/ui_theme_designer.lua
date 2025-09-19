-- Terrarium UI theme palette for LVGL Theme Designer
-- Usage: dofile("ui_theme_designer.lua")(lvgl)

local M = {}

M.palette = {
  primary = 0x2A9D8F,
  primary_dark = 0x1F7A70,
  primary_light = 0x3DB9A7,
  secondary = 0xE8F2EB,
  accent = 0x3A7D60,
  background_top = 0xF3EFE2,
  background_bottom = 0xE2F1E5,
  warning = 0xE76F51,
  success = 0x4CAF50,
}

function M.apply(lvgl)
  local theme = {}

  theme.bg = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.bg)
  lvgl.lv_style_set_bg_color(theme.bg, lvgl.lv_color_hex(M.palette.background_top))
  lvgl.lv_style_set_bg_grad_color(theme.bg, lvgl.lv_color_hex(M.palette.background_bottom))
  lvgl.lv_style_set_bg_grad_dir(theme.bg, lvgl.LV_GRAD_DIR_VER)
  lvgl.lv_style_set_bg_opa(theme.bg, lvgl.LV_OPA_COVER)

  theme.card = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.card)
  lvgl.lv_style_set_radius(theme.card, 18)
  lvgl.lv_style_set_bg_color(theme.card, lvgl.lv_color_hex(0xFFFFFF))
  lvgl.lv_style_set_bg_grad_color(theme.card, lvgl.lv_color_hex(0xF5F8F3))
  lvgl.lv_style_set_bg_grad_dir(theme.card, lvgl.LV_GRAD_DIR_VER)
  lvgl.lv_style_set_border_color(theme.card, lvgl.lv_color_hex(0xB7D3C2))
  lvgl.lv_style_set_border_width(theme.card, 1)
  lvgl.lv_style_set_shadow_width(theme.card, 12)
  lvgl.lv_style_set_shadow_ofs_y(theme.card, 4)
  lvgl.lv_style_set_shadow_color(theme.card, lvgl.lv_color_hex(0x9CBFA1))
  lvgl.lv_style_set_pad_all(theme.card, 20)
  lvgl.lv_style_set_pad_gap(theme.card, 16)

  theme.button_primary = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.button_primary)
  lvgl.lv_style_set_radius(theme.button_primary, 14)
  lvgl.lv_style_set_bg_color(theme.button_primary, lvgl.lv_color_hex(M.palette.primary))
  lvgl.lv_style_set_bg_grad_color(theme.button_primary, lvgl.lv_color_hex(M.palette.primary_dark))
  lvgl.lv_style_set_bg_grad_dir(theme.button_primary, lvgl.LV_GRAD_DIR_VER)
  lvgl.lv_style_set_text_color(theme.button_primary, lvgl.lv_color_hex(0xFFFFFF))
  lvgl.lv_style_set_border_color(theme.button_primary, lvgl.lv_color_hex(0x1B6A5F))

  theme.button_secondary = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.button_secondary)
  lvgl.lv_style_set_radius(theme.button_secondary, 14)
  lvgl.lv_style_set_bg_color(theme.button_secondary, lvgl.lv_color_hex(0xF1FAF1))
  lvgl.lv_style_set_bg_grad_color(theme.button_secondary, lvgl.lv_color_hex(0xDBEFDF))
  lvgl.lv_style_set_bg_grad_dir(theme.button_secondary, lvgl.LV_GRAD_DIR_VER)
  lvgl.lv_style_set_border_color(theme.button_secondary, lvgl.lv_color_hex(M.palette.accent))
  lvgl.lv_style_set_text_color(theme.button_secondary, lvgl.lv_color_hex(0x2F4F43))

  theme.table_header = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.table_header)
  lvgl.lv_style_set_bg_color(theme.table_header, lvgl.lv_color_hex(0xE8F2EB))
  lvgl.lv_style_set_border_color(theme.table_header, lvgl.lv_color_hex(0xB5CBB5))
  lvgl.lv_style_set_border_width(theme.table_header, 1)
  lvgl.lv_style_set_pad_all(theme.table_header, 8)

  theme.table_cell_dense = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.table_cell_dense)
  lvgl.lv_style_set_text_color(theme.table_cell_dense, lvgl.lv_color_hex(0x264C3F))
  lvgl.lv_style_set_pad_all(theme.table_cell_dense, 4)
  lvgl.lv_style_set_text_align(theme.table_cell_dense, lvgl.LV_TEXT_ALIGN_CENTER)

  theme.table_cell_selected = lvgl.lv_style_t()
  lvgl.lv_style_init(theme.table_cell_selected)
  lvgl.lv_style_set_bg_color(theme.table_cell_selected, lvgl.lv_color_hex(M.palette.accent))
  lvgl.lv_style_set_text_color(theme.table_cell_selected, lvgl.lv_color_hex(0xFFFFFF))
  lvgl.lv_style_set_border_color(theme.table_cell_selected, lvgl.lv_color_hex(0x285542))

  return theme
end

return function(lvgl)
  return M.apply(lvgl)
end

