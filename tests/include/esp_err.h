#pragma once

#include <stdint.h>

typedef int32_t esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x103
#define ESP_ERR_NOT_FOUND 0x104
#define ESP_ERR_INVALID_STATE 0x105
#define ESP_ERR_TIMEOUT 0x107

static inline const char *esp_err_to_name(esp_err_t err) {
  switch (err) {
  case ESP_OK:
    return "ESP_OK";
  case ESP_FAIL:
    return "ESP_FAIL";
  case ESP_ERR_INVALID_ARG:
    return "ESP_ERR_INVALID_ARG";
  case ESP_ERR_NO_MEM:
    return "ESP_ERR_NO_MEM";
  case ESP_ERR_NOT_FOUND:
    return "ESP_ERR_NOT_FOUND";
  case ESP_ERR_INVALID_STATE:
    return "ESP_ERR_INVALID_STATE";
  default:
    return "ESP_ERR_UNKNOWN";
  }
}
