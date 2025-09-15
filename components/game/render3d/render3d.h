#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declarations for rendering context structures */
/**
 * @brief Visual description of a terrarium instance.
 */
typedef struct Terrarium {
    char name[32];        /**< Display name */
    char species[32];     /**< Hosted species */
    char decor[32];       /**< Active decor */
    char substrate[32];   /**< Selected substrate */
    float temperature;    /**< Current temperature */
    float humidity;       /**< Current humidity */
    float uv_index;       /**< Current UV index */
    float health_ratio;   /**< Health ratio 0..1 */
    float growth_ratio;   /**< Growth ratio 0..1 */
    bool inhabited;       /**< True if a reptile lives inside */
    bool sick;            /**< True if reptile is sick */
    bool alive;           /**< True if reptile alive */
    bool selected;        /**< True when terrarium is focused */
    bool heater_on;       /**< Heater actuator state */
    bool light_on;        /**< Lighting actuator state */
    bool mist_on;         /**< Mister actuator state */
} Terrarium;

/**
 * @brief Basic camera definition for 3D rendering.
 *
 * Only the position of the viewpoint is tracked for now. More complex
 * orientation parameters can be added later if needed.
 */
typedef struct Camera {
    int16_t x; /**< X position of the camera. */
    int16_t y; /**< Y position of the camera. */
    int16_t z; /**< Zoom level (100 = 1x). */
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

/**
 * @brief Clear the rendering surface.
 *
 * Utility function exposed to C modules so they can reset the drawing surface
 * before submitting terrarium meshes. Colour follows RGB565 format.
 */
void render3d_clear(uint16_t color);

#ifdef __cplusplus
}
#endif

