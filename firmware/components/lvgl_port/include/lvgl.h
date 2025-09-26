#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lv_disp_t lv_disp_t;
typedef struct lv_obj_t lv_obj_t;

typedef int lv_color_t;

typedef enum {
    LV_RES_OK = 0,
    LV_RES_INV,
} lv_res_t;

static inline void lv_timer_handler(void)
{
}

#ifdef __cplusplus
}
#endif
