#include "doc_reader.h"

#include "app_config.h"
#include "esp_log.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "doc_reader";

size_t doc_reader_list_documents(char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return 0;
    }
    DIR *dir = opendir(DOCS_ROOT_PATH);
    size_t written = 0;
    if (!dir) {
        snprintf(buffer, buffer_len, "Aucun document");
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            int n = snprintf(buffer + written, buffer_len - written, "%s\n", entry->d_name);
            if (n < 0 || (size_t)n >= buffer_len - written) {
                break;
            }
            written += (size_t)n;
        }
    }
    closedir(dir);
    return written;
}

int doc_reader_load(const char *path, char *buffer, size_t buffer_len)
{
    if (!path || !buffer || buffer_len == 0) {
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return -1;
    }
    size_t read = fread(buffer, 1, buffer_len - 1, f);
    buffer[read] = '\0';
    fclose(f);
    return (int)read;
}
