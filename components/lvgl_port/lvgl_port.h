#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

void lvgl_port_init(void);

/**
 * @brief Acquire the LVGL core mutex.
 *
 * This helper allows tasks other than the GUI task to access LVGL while the
 * GUI task is paused. Pass UINT32_MAX to wait indefinitely.
 *
 * @param timeout_ms Maximum time to wait in milliseconds. Use 0 for a
 *                   non-blocking attempt or UINT32_MAX for an infinite wait.
 * @return true when the mutex has been acquired, false otherwise.
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/**
 * @brief Release the LVGL core mutex previously acquired with lvgl_port_lock().
 */
void lvgl_port_unlock(void);

#define LVGL_PORT_LOCK_INFINITE UINT32_MAX

#ifdef __cplusplus
}
#endif
