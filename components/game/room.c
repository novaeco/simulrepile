#include "room.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>

/**
 * @file room.c
 * @brief Display a selectable grid of terrariums.
 */

#define TAG "room"
#define GRID_SIZE 5

static bool occupied[GRID_SIZE][GRID_SIZE];

typedef struct {
    uint8_t x;
    uint8_t y;
} terrarium_ctx_t;

static void terrarium_event_handler(lv_event_t *e) {
    terrarium_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) return;
    if (occupied[ctx->y][ctx->x]) {
        ESP_LOGW(TAG, "Terrarium %u,%u already occupied", ctx->x, ctx->y);
        return;
    }
    occupied[ctx->y][ctx->x] = true;
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    ESP_LOGI(TAG, "Selected terrarium %u,%u", ctx->x, ctx->y);
}

void room_show(void)
{
    memset(occupied, 0, sizeof(occupied));
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);

    static terrarium_ctx_t ctx[GRID_SIZE][GRID_SIZE];

    /* Create a container with a 5x5 grid layout */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(grid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(grid);

    static const lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t row_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };

    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            lv_obj_t *btn = lv_btn_create(grid);
            lv_obj_set_size(btn, 60, 60);
            lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, x, 1,
                                 LV_GRID_ALIGN_STRETCH, y, 1);

            ctx[y][x].x = x;
            ctx[y][x].y = y;
            lv_obj_add_event_cb(btn, terrarium_event_handler, LV_EVENT_CLICKED,
                                &ctx[y][x]);

            /* Display cell index for debugging */
            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text_fmt(label, "%d", y * GRID_SIZE + x + 1);
            lv_obj_center(label);
        }
    }
}
