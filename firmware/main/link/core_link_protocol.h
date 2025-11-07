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
    CORE_LINK_MSG_STATE_DELTA = 0x11,
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

typedef uint16_t core_link_delta_field_mask_t;

enum {
    CORE_LINK_DELTA_FIELD_SCIENTIFIC_NAME = 0x0001,
    CORE_LINK_DELTA_FIELD_COMMON_NAME = 0x0002,
    CORE_LINK_DELTA_FIELD_TEMP_DAY = 0x0004,
    CORE_LINK_DELTA_FIELD_TEMP_NIGHT = 0x0008,
    CORE_LINK_DELTA_FIELD_HUMIDITY_DAY = 0x0010,
    CORE_LINK_DELTA_FIELD_HUMIDITY_NIGHT = 0x0020,
    CORE_LINK_DELTA_FIELD_LUX_DAY = 0x0040,
    CORE_LINK_DELTA_FIELD_LUX_NIGHT = 0x0080,
    CORE_LINK_DELTA_FIELD_HYDRATION = 0x0100,
    CORE_LINK_DELTA_FIELD_STRESS = 0x0200,
    CORE_LINK_DELTA_FIELD_HEALTH = 0x0400,
    CORE_LINK_DELTA_FIELD_LAST_FEED = 0x0800,
    CORE_LINK_DELTA_FIELD_ACTIVITY = 0x1000,
};

#define CORE_LINK_DELTA_STRING_BYTES (CORE_LINK_NAME_MAX_LEN + 1)

typedef struct {
    core_link_touch_type_t type;
    uint8_t point_id;
    uint16_t x;
    uint16_t y;
} core_link_touch_event_t;

#ifdef __cplusplus
}
#endif
