#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_LINK_PROTOCOL_VERSION 1
#define CORE_LINK_MAX_TERRARIUMS 4
#define CORE_LINK_NAME_MAX_LEN 31

typedef enum {
    CORE_LINK_MSG_HELLO = 0x01,
    CORE_LINK_MSG_HELLO_ACK = 0x02,
    CORE_LINK_MSG_REQUEST_STATE = 0x03,
    CORE_LINK_MSG_STATE_FULL = 0x10,
    CORE_LINK_MSG_PING = 0x1F,
    CORE_LINK_MSG_PONG = 0x20,
    CORE_LINK_MSG_TOUCH_EVENT = 0x80,
    CORE_LINK_MSG_DISPLAY_READY = 0x81,
    CORE_LINK_MSG_ERROR = 0xFE,
} core_link_msg_type_t;

typedef enum {
    CORE_LINK_TOUCH_DOWN = 0,
    CORE_LINK_TOUCH_MOVE = 1,
    CORE_LINK_TOUCH_UP = 2,
} core_link_touch_type_t;

typedef struct {
    uint8_t terrarium_id;
    char scientific_name[CORE_LINK_NAME_MAX_LEN + 1];
    char common_name[CORE_LINK_NAME_MAX_LEN + 1];
    float temp_day_c;
    float temp_night_c;
    float humidity_day_pct;
    float humidity_night_pct;
    float lux_day;
    float lux_night;
    float hydration_pct;
    float stress_pct;
    float health_pct;
    uint32_t last_feeding_timestamp;
    float activity_score;
} core_link_terrarium_snapshot_t;

typedef struct {
    uint32_t epoch_seconds;
    uint8_t terrarium_count;
    core_link_terrarium_snapshot_t terrariums[CORE_LINK_MAX_TERRARIUMS];
} core_link_state_frame_t;

typedef struct {
    core_link_touch_type_t type;
    uint8_t point_id;
    uint16_t x;
    uint16_t y;
} core_link_touch_event_t;

#ifdef __cplusplus
}
#endif
