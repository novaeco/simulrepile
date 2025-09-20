#ifndef LVGL_COMPAT_H
#define LVGL_COMPAT_H

#include "lvgl.h"

#ifndef LV_UNUSED
#define LV_UNUSED(x) (void)(x)
#endif

/* Alignment compatibility ------------------------------------------------- */
#ifndef LV_ALIGN_START
#define LV_ALIGN_START LV_ALIGN_LEFT_MID
#endif

#ifndef LV_ALIGN_END
#define LV_ALIGN_END LV_ALIGN_RIGHT_MID
#endif

static inline void lv_obj_set_style_align_self(lv_obj_t *obj, lv_align_t align, lv_style_selector_t selector) {
    lv_obj_set_style_align(obj, align, selector);
}

/* Tooltip compatibility ---------------------------------------------------- */
void lv_obj_set_tooltip_text(lv_obj_t *obj, const char *text);

/* Tabview helper wrappers -------------------------------------------------- */
static inline uint32_t lv_tabview_get_active(lv_obj_t *obj) {
    return (uint32_t)lv_tabview_get_tab_active(obj);
}

static inline void lv_tabview_set_anim_time(lv_obj_t *obj, uint32_t time_ms) {
    LV_UNUSED(obj);
    LV_UNUSED(time_ms);
}

/* Text wrap compatibility -------------------------------------------------- */
typedef enum {
    LV_TEXT_WRAP_NONE = 0,
    LV_TEXT_WRAP_WORD = 1,
    LV_TEXT_WRAP_CHAR = 2
} lv_text_wrap_t;

static inline void lv_obj_set_style_text_wrap(lv_obj_t *obj, lv_text_wrap_t wrap, lv_style_selector_t selector) {
    LV_UNUSED(obj);
    LV_UNUSED(wrap);
    LV_UNUSED(selector);
}

/* Legacy meter compatibility ---------------------------------------------- */
typedef struct lvgl_compat_meter_scale lv_meter_scale_t;
typedef struct lvgl_compat_meter_indicator lv_meter_indicator_t;

lv_obj_t *lv_meter_create(lv_obj_t *parent);
lv_meter_scale_t *lv_meter_add_scale(lv_obj_t *meter);
void lv_meter_set_scale_ticks(lv_obj_t *meter, lv_meter_scale_t *scale,
                              uint16_t tick_cnt, uint16_t tick_width,
                              uint16_t tick_len, lv_color_t tick_color);
void lv_meter_set_scale_major_ticks(lv_obj_t *meter, lv_meter_scale_t *scale,
                                    uint16_t tick_cnt, uint16_t tick_width,
                                    uint16_t tick_len, lv_color_t tick_color,
                                    int16_t label_gap);
void lv_meter_set_scale_range(lv_obj_t *meter, lv_meter_scale_t *scale,
                              int32_t min, int32_t max,
                              uint32_t angle_range, uint32_t rotation);

lv_meter_indicator_t *lv_meter_add_arc(lv_obj_t *meter, lv_meter_scale_t *scale,
                                       uint16_t width, lv_color_t color,
                                       int32_t radius_mod);

lv_meter_indicator_t *lv_meter_add_needle_line(lv_obj_t *meter, lv_meter_scale_t *scale,
                                               uint16_t width, lv_color_t color,
                                               int32_t radius_mod);

void lv_meter_set_indicator_value(lv_obj_t *meter, lv_meter_indicator_t *indicator,
                                  int32_t value);
void lv_meter_set_indicator_start_value(lv_obj_t *meter, lv_meter_indicator_t *indicator,
                                        int32_t value);
void lv_meter_set_indicator_end_value(lv_obj_t *meter, lv_meter_indicator_t *indicator,
                                      int32_t value);

#endif /* LVGL_COMPAT_H */
