#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAVESHARE_IO_VARIANT_UNKNOWN = 0,
    WAVESHARE_IO_VARIANT_CH422G,
    WAVESHARE_IO_VARIANT_CH32V003,
} waveshare_io_variant_t;

esp_err_t waveshare_io_init(void);

bool waveshare_io_ready(void);

waveshare_io_variant_t waveshare_io_get_variant(void);

const char *waveshare_io_variant_name(waveshare_io_variant_t variant);

uint8_t waveshare_io_get_ch422g_address(void);

esp_err_t waveshare_io_output_set(uint8_t line, bool level_high);

esp_err_t waveshare_io_output_get(uint8_t line, bool *level_high);

esp_err_t waveshare_io_sd_select(void);

esp_err_t waveshare_io_sd_deselect(void);

#define WAVESHARE_IO_LINE_TOUCH_RST 1u
#define WAVESHARE_IO_LINE_BACKLIGHT 2u
#define WAVESHARE_IO_LINE_LCD_RST 3u

#define WAVESHARE_IO_EXIO_DISABLED UINT8_MAX

#define WAVESHARE_IO_LINE_FROM_EXIO_CONST(exio) \
    ((uint8_t)(((exio) > 0u) ? ((exio) - 1u) : WAVESHARE_IO_EXIO_DISABLED))

static inline uint8_t waveshare_io_line_from_exio(uint8_t exio)
{
    return WAVESHARE_IO_LINE_FROM_EXIO_CONST(exio);
}

static inline bool waveshare_io_line_valid(uint8_t line)
{
    return line < 8u;
}

#ifdef __cplusplus
}
#endif

