#include "room.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>

#define TAG "room"
#define GRID_SIZE 5

static bool occupied[GRID_SIZE][GRID_SIZE];

typedef struct {
    uint8_t x;
    uint8_t y;
} terrarium_ctx_t;

static void terrarium_event_handler(lv_event_t *e)
{
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

    const int size = 60;
    const int spacing = 10;
    static terrarium_ctx_t ctx[GRID_SIZE][GRID_SIZE];

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            lv_obj_t *btn = lv_btn_create(scr);
            lv_obj_set_size(btn, size, size);
            lv_obj_align(btn, LV_ALIGN_TOP_LEFT,
                         x * (size + spacing) + spacing,
                         y * (size + spacing) + spacing);
            ctx[y][x].x = x;
            ctx[y][x].y = y;
            lv_obj_add_event_cb(btn, terrarium_event_handler, LV_EVENT_CLICKED, &ctx[y][x]);
        }
    }
}
