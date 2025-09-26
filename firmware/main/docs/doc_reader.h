#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOCS_ROOT_PATH "/sdcard/docs"

size_t doc_reader_list_documents(char *buffer, size_t buffer_len);
int doc_reader_load(const char *path, char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
