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

/* Terrarium substrate icons */
LV_IMG_DECLARE(gImage_substrate_sable);
LV_IMG_DECLARE(gImage_substrate_tropical);
LV_IMG_DECLARE(gImage_substrate_roche);

/* Terrarium decor icons */
LV_IMG_DECLARE(gImage_decor_lianes);
LV_IMG_DECLARE(gImage_decor_rochers);
LV_IMG_DECLARE(gImage_decor_caverne);

#endif /* __IMAGE_H */
