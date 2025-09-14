#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declarations for rendering context structures */
typedef struct Terrarium Terrarium;
typedef struct Camera Camera;

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

