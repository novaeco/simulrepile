#include "lvgl_compat.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifndef LV_ASSERT_MALLOC
#define LV_ASSERT_MALLOC(p) LV_UNUSED(p)
#endif

typedef struct compat_tooltip_entry {
    struct compat_tooltip_entry *next;
    lv_obj_t *target;
    char *text;
} compat_tooltip_entry_t;

static compat_tooltip_entry_t *tooltip_entries;
static compat_tooltip_entry_t *tooltip_active_entry;
static lv_obj_t *tooltip_container;
static lv_obj_t *tooltip_label;
static lv_timer_t *tooltip_timer;

static compat_tooltip_entry_t *tooltip_find_entry(lv_obj_t *obj);
static void tooltip_detach_entry(compat_tooltip_entry_t *entry, bool remove_event_cb);
static void tooltip_event_cb(lv_event_t *e);
static void tooltip_ensure_ui(void);
static void tooltip_hide(void);
static void tooltip_timer_cb(lv_timer_t *timer);
static void tooltip_show(compat_tooltip_entry_t *entry, lv_event_t *e);
static void tooltip_update_position(const lv_point_t *point, lv_obj_t *target);
static const lv_point_t *tooltip_acquire_point(lv_event_t *e, lv_point_t *buffer);

struct lvgl_compat_meter_scale {
    struct lvgl_compat_meter_scale *next;
    lv_obj_t *meter;
    int32_t min;
    int32_t max;
    uint32_t angle_range;
    uint32_t rotation;
};

typedef enum {
    COMPAT_METER_INDICATOR_ARC,
    COMPAT_METER_INDICATOR_NEEDLE
} compat_meter_indicator_kind_t;

struct lvgl_compat_meter_indicator {
    struct lvgl_compat_meter_indicator *next;
    lv_meter_scale_t *scale;
    lv_obj_t *arc;
    compat_meter_indicator_kind_t kind;
    int32_t start_value;
    int32_t end_value;
};

typedef struct {
    lv_meter_scale_t *scales;
    lv_meter_indicator_t *indicators;
} compat_meter_ctx_t;

static compat_meter_ctx_t *meter_get_ctx(lv_obj_t *meter);
static compat_meter_ctx_t *meter_ensure_ctx(lv_obj_t *meter);
static void meter_delete_cb(lv_event_t *e);
static lv_value_precise_t scale_value_to_angle(const lv_meter_scale_t *scale, int32_t value);
static void indicator_sync_arc(lv_meter_indicator_t *indicator);

void lv_obj_set_tooltip_text(lv_obj_t *obj, const char *text)
{
    if (!obj) {
        return;
    }

    compat_tooltip_entry_t *entry = tooltip_find_entry(obj);

    if (!text || text[0] == '\0') {
        if (entry) {
            tooltip_detach_entry(entry, true);
        }
        return;
    }

    size_t len = strlen(text);
    if (len == 0U) {
        if (entry) {
            tooltip_detach_entry(entry, true);
        }
        return;
    }

    if (entry) {
        char *buffer = lv_malloc(len + 1U);
        LV_ASSERT_MALLOC(buffer);
        if (!buffer) {
            return;
        }
        memcpy(buffer, text, len + 1U);
        if (entry->text) {
            lv_free(entry->text);
        }
        entry->text = buffer;
        if (tooltip_active_entry == entry && tooltip_label) {
            lv_label_set_text(tooltip_label, entry->text);
            tooltip_update_position(NULL, entry->target);
        }
        return;
    }

    entry = lv_malloc(sizeof(*entry));
    LV_ASSERT_MALLOC(entry);
    if (!entry) {
        return;
    }

    char *buffer = lv_malloc(len + 1U);
    LV_ASSERT_MALLOC(buffer);
    if (!buffer) {
        lv_free(entry);
        return;
    }

    memcpy(buffer, text, len + 1U);
    entry->next = tooltip_entries;
    entry->target = obj;
    entry->text = buffer;
    tooltip_entries = entry;
    lv_obj_add_event_cb(obj, tooltip_event_cb, LV_EVENT_ALL, entry);
}

lv_obj_t *lv_meter_create(lv_obj_t *parent) {
    lv_obj_t *meter = lv_obj_create(parent);
    LV_ASSERT_MALLOC(meter);
    if (!meter) {
        return NULL;
    }
    lv_obj_remove_style_all(meter);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    compat_meter_ctx_t *ctx = lv_malloc(sizeof(*ctx));
    LV_ASSERT_MALLOC(ctx);
    if (!ctx) {
        return meter;
    }
    ctx->scales = NULL;
    ctx->indicators = NULL;
    lv_obj_set_user_data(meter, ctx);
    lv_obj_add_event_cb(meter, meter_delete_cb, LV_EVENT_DELETE, ctx);
    return meter;
}

lv_meter_scale_t *lv_meter_add_scale(lv_obj_t *meter) {
    compat_meter_ctx_t *ctx = meter_ensure_ctx(meter);
    if (!ctx) {
        return NULL;
    }
    lv_meter_scale_t *scale = lv_malloc(sizeof(*scale));
    LV_ASSERT_MALLOC(scale);
    if (!scale) {
        return NULL;
    }
    scale->next = ctx->scales;
    scale->meter = meter;
    scale->min = 0;
    scale->max = 100;
    scale->angle_range = 360;
    scale->rotation = 0;
    ctx->scales = scale;
    return scale;
}

void lv_meter_set_scale_ticks(lv_obj_t *meter, lv_meter_scale_t *scale,
                              uint16_t tick_cnt, uint16_t tick_width,
                              uint16_t tick_len, lv_color_t tick_color) {
    LV_UNUSED(meter);
    LV_UNUSED(scale);
    LV_UNUSED(tick_cnt);
    LV_UNUSED(tick_width);
    LV_UNUSED(tick_len);
    LV_UNUSED(tick_color);
}

void lv_meter_set_scale_major_ticks(lv_obj_t *meter, lv_meter_scale_t *scale,
                                    uint16_t tick_cnt, uint16_t tick_width,
                                    uint16_t tick_len, lv_color_t tick_color,
                                    int16_t label_gap) {
    LV_UNUSED(meter);
    LV_UNUSED(scale);
    LV_UNUSED(tick_cnt);
    LV_UNUSED(tick_width);
    LV_UNUSED(tick_len);
    LV_UNUSED(tick_color);
    LV_UNUSED(label_gap);
}

void lv_meter_set_scale_range(lv_obj_t *meter, lv_meter_scale_t *scale,
                              int32_t min, int32_t max,
                              uint32_t angle_range, uint32_t rotation) {
    LV_UNUSED(meter);
    if (!scale) {
        return;
    }
    if (min == max) {
        max = min + 1;
    }
    scale->min = min;
    scale->max = max;
    scale->angle_range = angle_range;
    scale->rotation = rotation;
}

static lv_obj_t *create_arc(lv_obj_t *meter, const lv_meter_scale_t *scale,
                            uint16_t width, lv_color_t color) {
    lv_obj_t *arc = lv_arc_create(meter);
    if (!arc) {
        return NULL;
    }
    lv_arc_set_range(arc, scale->min, scale->max);
    lv_arc_set_value(arc, scale->min);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_angles(arc, scale->rotation, scale->rotation + scale->angle_range);
    lv_obj_set_size(arc, LV_PCT(100), LV_PCT(100));
    lv_obj_center(arc);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_color_t bg_color = lv_color_mix(color, lv_color_white(), LV_OPA_40);
    lv_obj_set_style_arc_color(arc, bg_color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_KNOB);
    return arc;
}

lv_meter_indicator_t *lv_meter_add_arc(lv_obj_t *meter, lv_meter_scale_t *scale,
                                       uint16_t width, lv_color_t color,
                                       int32_t radius_mod) {
    LV_UNUSED(radius_mod);
    compat_meter_ctx_t *ctx = meter_ensure_ctx(meter);
    if (!ctx || !scale) {
        return NULL;
    }
    lv_obj_t *arc = create_arc(meter, scale, width, color);
    if (!arc) {
        return NULL;
    }
    lv_meter_indicator_t *indicator = lv_malloc(sizeof(*indicator));
    LV_ASSERT_MALLOC(indicator);
    if (!indicator) {
        lv_obj_del_async(arc);
        return NULL;
    }
    indicator->next = ctx->indicators;
    indicator->scale = scale;
    indicator->arc = arc;
    indicator->kind = COMPAT_METER_INDICATOR_ARC;
    indicator->start_value = scale->min;
    indicator->end_value = scale->min;
    ctx->indicators = indicator;
    indicator_sync_arc(indicator);
    return indicator;
}

lv_meter_indicator_t *lv_meter_add_needle_line(lv_obj_t *meter, lv_meter_scale_t *scale,
                                               uint16_t width, lv_color_t color,
                                               int32_t radius_mod) {
    LV_UNUSED(radius_mod);
    compat_meter_ctx_t *ctx = meter_ensure_ctx(meter);
    if (!ctx || !scale) {
        return NULL;
    }
    lv_obj_t *arc = create_arc(meter, scale, width, color);
    if (!arc) {
        return NULL;
    }
    lv_obj_set_style_arc_color(arc, lv_color_darken(color, LV_OPA_20), LV_PART_MAIN);
    lv_meter_indicator_t *indicator = lv_malloc(sizeof(*indicator));
    LV_ASSERT_MALLOC(indicator);
    if (!indicator) {
        lv_obj_del_async(arc);
        return NULL;
    }
    indicator->next = ctx->indicators;
    indicator->scale = scale;
    indicator->arc = arc;
    indicator->kind = COMPAT_METER_INDICATOR_NEEDLE;
    indicator->start_value = scale->min;
    indicator->end_value = scale->min;
    ctx->indicators = indicator;
    indicator_sync_arc(indicator);
    return indicator;
}

void lv_meter_set_indicator_value(lv_obj_t *meter, lv_meter_indicator_t *indicator,
                                  int32_t value) {
    LV_UNUSED(meter);
    if (!indicator) {
        return;
    }
    indicator->end_value = value;
    indicator_sync_arc(indicator);
}

void lv_meter_set_indicator_start_value(lv_obj_t *meter, lv_meter_indicator_t *indicator,
                                        int32_t value) {
    LV_UNUSED(meter);
    if (!indicator || indicator->kind != COMPAT_METER_INDICATOR_ARC) {
        return;
    }
    indicator->start_value = value;
    indicator_sync_arc(indicator);
}

void lv_meter_set_indicator_end_value(lv_obj_t *meter, lv_meter_indicator_t *indicator,
                                      int32_t value) {
    LV_UNUSED(meter);
    if (!indicator || indicator->kind != COMPAT_METER_INDICATOR_ARC) {
        return;
    }
    indicator->end_value = value;
    indicator_sync_arc(indicator);
}

static compat_tooltip_entry_t *tooltip_find_entry(lv_obj_t *obj)
{
    compat_tooltip_entry_t *entry = tooltip_entries;
    while (entry) {
        if (entry->target == obj) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static void tooltip_detach_entry(compat_tooltip_entry_t *entry, bool remove_event_cb)
{
    if (!entry) {
        return;
    }

    if (tooltip_active_entry == entry) {
        tooltip_hide();
    }

    compat_tooltip_entry_t **it = &tooltip_entries;
    while (*it) {
        if (*it == entry) {
            *it = entry->next;
            break;
        }
        it = &(*it)->next;
    }

    if (remove_event_cb && entry->target) {
        lv_obj_remove_event_cb_with_user_data(entry->target, tooltip_event_cb, entry);
    }

    if (entry->text) {
        lv_free(entry->text);
    }
    lv_free(entry);
}

static const lv_point_t *tooltip_acquire_point(lv_event_t *e, lv_point_t *buffer)
{
    if (!e || !buffer) {
        return NULL;
    }

#if LVGL_VERSION_MAJOR < 9
    const lv_point_t *event_point = lv_event_get_point(e);
    if (event_point) {
        *buffer = *event_point;
        return buffer;
    }
#else
    void *param = lv_event_get_param(e);
    if (param) {
        switch (lv_event_get_code(e)) {
            case LV_EVENT_PRESSING:
            case LV_EVENT_PRESS_LOST:
            case LV_EVENT_RELEASED:
            case LV_EVENT_LONG_PRESSED:
            case LV_EVENT_LONG_PRESSED_REPEAT:
            case LV_EVENT_GESTURE:
            case LV_EVENT_LEAVE: {
                const lv_point_t *event_point = (const lv_point_t *)param;
                *buffer = *event_point;
                return buffer;
            }
            default:
                break;
        }
    }
#endif

#if LVGL_VERSION_MAJOR < 9
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        indev = lv_indev_get_act();
    }
#else
    lv_indev_t *indev = lv_indev_get_act();
#endif

    if (indev) {
        lv_indev_get_point(indev, buffer);
        return buffer;
    }

    return NULL;
}

static void tooltip_event_cb(lv_event_t *e)
{
    compat_tooltip_entry_t *entry = lv_event_get_user_data(e);
    if (!entry) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
        case LV_EVENT_DELETE:
            entry->target = NULL;
            tooltip_detach_entry(entry, false);
            return;
        case LV_EVENT_LONG_PRESSED:
            tooltip_show(entry, e);
            break;
        case LV_EVENT_LONG_PRESSED_REPEAT:
        case LV_EVENT_PRESSING:
            if (tooltip_active_entry == entry) {
                lv_point_t buf;
                const lv_point_t *point = tooltip_acquire_point(e, &buf);
                tooltip_update_position(point, entry->target);
                if (tooltip_timer) {
                    lv_timer_reset(tooltip_timer);
                    lv_timer_resume(tooltip_timer);
                }
            }
            break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
        case LV_EVENT_LEAVE:
        case LV_EVENT_SCROLL:
        case LV_EVENT_SCROLL_BEGIN:
        case LV_EVENT_SCROLL_END:
        case LV_EVENT_GESTURE:
        case LV_EVENT_KEY:
            if (tooltip_active_entry == entry) {
                tooltip_hide();
            }
            break;
        default:
            break;
    }
}

static void tooltip_ensure_ui(void)
{
    if (!tooltip_container) {
        lv_obj_t *layer = lv_layer_top();
        tooltip_container = lv_obj_create(layer);
        if (!tooltip_container) {
            return;
        }
        lv_obj_remove_style_all(tooltip_container);
        lv_obj_set_style_bg_color(tooltip_container, lv_color_hex(0x2D2D30), 0);
        lv_obj_set_style_bg_opa(tooltip_container, LV_OPA_80, 0);
        lv_obj_set_style_radius(tooltip_container, 6, 0);
        lv_obj_set_style_pad_all(tooltip_container, 8, 0);
        lv_obj_set_style_border_width(tooltip_container, 0, 0);
        lv_obj_add_flag(tooltip_container,
                        LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT |
                        LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_HIDDEN);
        tooltip_label = lv_label_create(tooltip_container);
        if (tooltip_label) {
            lv_label_set_text(tooltip_label, "");
            lv_obj_set_style_text_color(tooltip_label, lv_color_white(), 0);
        }
    }

    if (!tooltip_timer) {
        tooltip_timer = lv_timer_create(tooltip_timer_cb, 2000, NULL);
        if (tooltip_timer) {
            lv_timer_pause(tooltip_timer);
        }
    }
}

static void tooltip_hide(void)
{
    tooltip_active_entry = NULL;
    if (tooltip_timer) {
        lv_timer_pause(tooltip_timer);
    }
    if (tooltip_container) {
        lv_obj_add_flag(tooltip_container, LV_OBJ_FLAG_HIDDEN);
    }
}

static void tooltip_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    tooltip_hide();
}

static void tooltip_show(compat_tooltip_entry_t *entry, lv_event_t *e)
{
    if (!entry || !entry->text || entry->text[0] == '\0') {
        return;
    }

    tooltip_ensure_ui();
    if (!tooltip_container || !tooltip_label) {
        return;
    }

    tooltip_active_entry = entry;
    lv_label_set_text(tooltip_label, entry->text);

    lv_point_t buf;
    const lv_point_t *point = tooltip_acquire_point(e, &buf);
    tooltip_update_position(point, entry->target);
    lv_obj_clear_flag(tooltip_container, LV_OBJ_FLAG_HIDDEN);

    if (tooltip_timer) {
        lv_timer_set_period(tooltip_timer, 2000);
        lv_timer_reset(tooltip_timer);
        lv_timer_resume(tooltip_timer);
    }
}

static void tooltip_update_position(const lv_point_t *point, lv_obj_t *target)
{
    if (!tooltip_container) {
        return;
    }

    lv_display_t *disp = NULL;
    if (target) {
        disp = lv_obj_get_display(target);
    }
    if (!disp) {
        disp = lv_display_get_default();
    }
    if (!disp) {
        return;
    }

    lv_point_t fallback = {0, 0};
    const lv_point_t *pt = point;
    if (!pt) {
        if (target) {
            lv_area_t coords;
            lv_obj_get_coords(target, &coords);
            fallback.x = coords.x2;
            fallback.y = coords.y1;
            pt = &fallback;
        } else {
            pt = &fallback;
        }
    }

    lv_coord_t x = pt->x + 12;
    lv_coord_t y = pt->y + 12;

    lv_obj_update_layout(tooltip_container);
    lv_coord_t w = lv_obj_get_width(tooltip_container);
    lv_coord_t h = lv_obj_get_height(tooltip_container);
    lv_coord_t max_x = lv_display_get_horizontal_resolution(disp);
    lv_coord_t max_y = lv_display_get_vertical_resolution(disp);

    if (x + w > max_x) {
        x = max_x - w - 4;
    }
    if (y + h > max_y) {
        y = max_y - h - 4;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    lv_obj_set_pos(tooltip_container, x, y);
}

static compat_meter_ctx_t *meter_get_ctx(lv_obj_t *meter) {
    return (compat_meter_ctx_t *)lv_obj_get_user_data(meter);
}

static compat_meter_ctx_t *meter_ensure_ctx(lv_obj_t *meter) {
    compat_meter_ctx_t *ctx = meter_get_ctx(meter);
    if (ctx) {
        return ctx;
    }
    ctx = lv_malloc(sizeof(*ctx));
    LV_ASSERT_MALLOC(ctx);
    if (!ctx) {
        return NULL;
    }
    ctx->scales = NULL;
    ctx->indicators = NULL;
    lv_obj_set_user_data(meter, ctx);
    lv_obj_add_event_cb(meter, meter_delete_cb, LV_EVENT_DELETE, ctx);
    return ctx;
}

static void meter_delete_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    compat_meter_ctx_t *ctx = (compat_meter_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) {
        ctx = meter_get_ctx(obj);
    }
    if (!ctx) {
        return;
    }
    lv_meter_indicator_t *indicator = ctx->indicators;
    while (indicator) {
        lv_meter_indicator_t *next = indicator->next;
        if (indicator->arc) {
            lv_obj_del_async(indicator->arc);
        }
        lv_free(indicator);
        indicator = next;
    }
    lv_meter_scale_t *scale = ctx->scales;
    while (scale) {
        lv_meter_scale_t *next = scale->next;
        lv_free(scale);
        scale = next;
    }
    lv_free(ctx);
    lv_obj_set_user_data(obj, NULL);
}

static lv_value_precise_t scale_value_to_angle(const lv_meter_scale_t *scale, int32_t value) {
    if (!scale) {
        return 0;
    }
    int32_t v = value;
    if (v < scale->min) {
        v = scale->min;
    } else if (v > scale->max) {
        v = scale->max;
    }
    int32_t range = scale->max - scale->min;
    if (range <= 0) {
        return scale->rotation;
    }
    int64_t delta = (int64_t)(v - scale->min) * (int64_t)scale->angle_range;
    lv_value_precise_t angle = (lv_value_precise_t)(delta / range) + (lv_value_precise_t)scale->rotation;
    return angle;
}

static void indicator_sync_arc(lv_meter_indicator_t *indicator) {
    if (!indicator || !indicator->scale || !indicator->arc) {
        return;
    }
    lv_arc_set_range(indicator->arc, indicator->scale->min, indicator->scale->max);
    lv_arc_set_bg_angles(indicator->arc,
                         indicator->scale->rotation,
                         indicator->scale->rotation + indicator->scale->angle_range);
    if (indicator->kind == COMPAT_METER_INDICATOR_NEEDLE) {
        int32_t clamped = indicator->end_value;
        if (clamped < indicator->scale->min) {
            clamped = indicator->scale->min;
        } else if (clamped > indicator->scale->max) {
            clamped = indicator->scale->max;
        }
        lv_arc_set_value(indicator->arc, clamped);
    } else {
        lv_value_precise_t start = scale_value_to_angle(indicator->scale, indicator->start_value);
        lv_value_precise_t end = scale_value_to_angle(indicator->scale, indicator->end_value);
        lv_arc_set_start_angle(indicator->arc, start);
        lv_arc_set_end_angle(indicator->arc, end);
    }
}
