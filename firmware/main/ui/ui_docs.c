#include "ui/ui_docs.h"

#include "esp_log.h"

static const char *TAG = "ui_docs";

void ui_docs_create(void)
{
    ESP_LOGI(TAG, "Creating document browser (stub)");
}

void ui_docs_show_document(const char *path)
{
    ESP_LOGI(TAG, "Displaying document: %s", path ? path : "<null>");
}
