#include "input_gestures.h"
#include <math.h>

typedef struct {
    lv_point_t last_point;
    int last_distance;
    size_t last_touch_cnt;
} gesture_state_t;

static gesture_state_t gstate;

void input_gestures_update(const lv_point_t *points, size_t touch_cnt)
{
    lv_obj_t *act = lv_scr_act();

    if (touch_cnt == 1) {
        if (gstate.last_touch_cnt == 1) {
            lv_point_t delta;
            delta.x = points[0].x - gstate.last_point.x;
            delta.y = points[0].y - gstate.last_point.y;
            if (delta.x != 0 || delta.y != 0) {
                lv_event_send(act, LV_EVENT_USER_1, &delta);
            }
        }
        gstate.last_point = points[0];
    } else if (touch_cnt >= 2) {
        int dx = points[0].x - points[1].x;
        int dy = points[0].y - points[1].y;
        int dist = (int)sqrtf((float)(dx * dx + dy * dy));
        if (gstate.last_touch_cnt >= 2) {
            int diff = dist - gstate.last_distance;
            if (diff != 0) {
                lv_event_send(act, LV_EVENT_USER_2, &diff);
            }
        }
        gstate.last_distance = dist;
    }

    gstate.last_touch_cnt = touch_cnt;
    if (touch_cnt == 0) {
        gstate.last_distance = 0;
    }
}
