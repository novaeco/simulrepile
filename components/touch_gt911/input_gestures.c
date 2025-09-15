/* -------------------------------------------------------------------------- */
/*  Gesture handling                                                          */
/* -------------------------------------------------------------------------- */

#include "input_gestures.h"
#include <math.h>

typedef struct {
  lv_point_t last_point; /* Last single touch position          */
  int last_distance;     /* Last distance between two touches   */
  size_t last_touch_cnt; /* Number of touches in previous frame */
} gesture_state_t;

static gesture_state_t gstate; /* Persistent state between updates    */

/* Compute Euclidean distance between two points -----------------------------
 */
static int point_distance(const lv_point_t *a, const lv_point_t *b) {
  int dx = a->x - b->x;
  int dy = a->y - b->y;
  return (int)sqrtf((float)dx * dx + (float)dy * dy);
}

/* Update gesture state and propagate LVGL events ----------------------------
 */
void input_gestures_update(const lv_point_t *points, size_t touch_cnt) {
  /* Determine the currently active view/screen */
  lv_obj_t *active_view = lv_scr_act();

  if (touch_cnt == 1) {
    /* Drag gesture: compute delta between consecutive points */
    if (gstate.last_touch_cnt == 1) {
      lv_point_t delta = {
          .x = points[0].x - gstate.last_point.x,
          .y = points[0].y - gstate.last_point.y,
      };
      if (delta.x != 0 || delta.y != 0) {
        lv_event_send(active_view, LV_EVENT_USER_1, &delta);
      }
    }
    gstate.last_point = points[0];
    gstate.last_distance = 0; /* Reset pinch tracking */
  } else if (touch_cnt >= 2) {
    /* Pinch-to-zoom: measure distance between the first two points */
    int dist = point_distance(&points[0], &points[1]);

    if (gstate.last_touch_cnt >= 2) {
      int diff = dist - gstate.last_distance;
      if (diff != 0) {
        lv_event_send(active_view, LV_EVENT_USER_2, &diff);
      }
    }
    gstate.last_distance = dist;
  }

  gstate.last_touch_cnt = touch_cnt;

  if (touch_cnt == 0) {
    /* Clear state when all touches are released */
    gstate.last_distance = 0;
  }
}
