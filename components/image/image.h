/*****************************************************************************
 * | File      	 :   image.h
 * | Author      :   Waveshare team
 * | Function    :   
 * | Info        :
 *                   
 *----------------
 * |This version :   V1.0
 * | Date        :   2024-11-19
 * | Info        :   Basic version
 *
 ******************************************************************************/
#ifndef __IMAGE_H
#define __IMAGE_H

/* Include LVGL types for image descriptors */
#include "lvgl.h"

extern const unsigned char gImage_picture[];
extern const unsigned char gImage_Bitmap[];
extern const unsigned char gImage_picture_90[];
extern const unsigned char gImage_Bitmap_90[];

/* Reptile idle sprite */
LV_IMG_DECLARE(gImage_reptile_idle);
/* Reptile mood sprites */
LV_IMG_DECLARE(gImage_reptile_happy);
LV_IMG_DECLARE(gImage_reptile_sad);
/* Reptile action sprites */
LV_IMG_DECLARE(gImage_reptile_manger);
LV_IMG_DECLARE(gImage_reptile_boire);
LV_IMG_DECLARE(gImage_reptile_chauffer);

#endif /* __IMAGE_H */
