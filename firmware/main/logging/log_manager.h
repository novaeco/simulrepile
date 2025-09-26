#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_manager_init(void);
void log_manager_info(const char *fmt, ...);
void log_manager_error(const char *fmt, ...);
size_t log_manager_copy_recent(char *buffer, size_t buffer_len);
void log_manager_flush_to_sd(void);

#ifdef __cplusplus
}
#endif
