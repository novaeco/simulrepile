#pragma once

#include "link/core_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void core_state_manager_init(void);
void core_state_manager_update(float delta_seconds);
void core_state_manager_apply_touch(const core_link_touch_event_t *event);
void core_state_manager_build_frame(core_link_state_frame_t *frame);

#ifdef __cplusplus
}
#endif
