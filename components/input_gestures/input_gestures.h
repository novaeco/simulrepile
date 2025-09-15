#ifndef INPUT_GESTURES_H
#define INPUT_GESTURES_H

#include "lvgl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void input_gestures_update(const lv_point_t *points, size_t touch_cnt);

#ifdef __cplusplus
}
#endif

#endif // INPUT_GESTURES_H
