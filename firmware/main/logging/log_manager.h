#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_manager_init(void);
void log_manager_info(const char *fmt, ...);
void log_manager_error(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
