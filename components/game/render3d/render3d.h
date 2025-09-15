#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declarations for rendering context structures */
typedef struct Terrarium Terrarium;

/**
 * @brief Basic camera definition for 3D rendering.
 *
 * Only the position of the viewpoint is tracked for now. More complex
 * orientation parameters can be added later if needed.
 */
typedef struct Camera {
    int16_t x; /**< X position of the camera. */
    int16_t y; /**< Y position of the camera. */
    int16_t z; /**< Z position of the camera. */
} Camera;

/**
 * @brief Render a terrarium with LovyanGFX.
 *
 * Draws the terrarium container, basic decor elements and the hosted reptile
 * using textures allocated in PSRAM. Camera controls viewpoint placement.
 *
 * @param t Pointer to terrarium description.
 * @param cam Pointer to active camera description.
 */
void render_terrarium(Terrarium *t, Camera *cam);

#ifdef __cplusplus
}
#endif

