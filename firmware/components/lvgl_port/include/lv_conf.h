#pragma once

#include <sdkconfig.h>

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE 1
#endif

/*====================
 * Color settings
 *====================*/
#define LV_COLOR_DEPTH 16

#if defined(CONFIG_LV_COLOR_16_SWAP) && CONFIG_LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 1
#else
#define LV_COLOR_16_SWAP 0
#endif

#ifndef CONFIG_APP_SD_MOUNT_POINT
#define CONFIG_APP_SD_MOUNT_POINT "/sdcard"
#endif

/*=========================
 * Stdlib wrapper settings
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

/*=================
 * Operating system
 *=================*/
#define LV_USE_OS   LV_OS_FREERTOS
#define LV_USE_FREERTOS_TASK_NOTIFY 1

/*====================
 * Logging
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_INFO
#define LV_LOG_USE_TIMESTAMP 1

/*========================
 * Rendering configuration
 *========================*/
#define LV_DRAW_LAYER_MAX_MEMORY (256 * 1024)
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

/*====================
 * Image decoders
 *====================*/
#define LV_USE_LODEPNG 1
#define LV_USE_LIBPNG 0

/*====================
 * File system
 *====================*/
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_STDIO_PATH CONFIG_APP_SD_MOUNT_POINT
#define LV_FS_STDIO_CACHE_SIZE 4096

/*====================
 * Fonts
 *====================*/
#define LV_FONT_DEFAULT &lv_font_montserrat_20
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1

/*====================
 * Themes and layouts
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*====================
 * Build options
 *====================*/
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_TEST 0

