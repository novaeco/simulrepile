#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "i2c_scanner"
#define SCAN_ADDR_START 0x08
#define SCAN_ADDR_END   0x77
#define CH422G_ADDR_MIN 0x20
#define CH422G_ADDR_MAX 0x23

static i2c_master_bus_handle_t s_bus;

static void report_bus_levels(void)
{
    int sda = gpio_get_level(CONFIG_I2C_MASTER_SDA_GPIO);
    int scl = gpio_get_level(CONFIG_I2C_MASTER_SCL_GPIO);
    ESP_LOGI(TAG,
             "Bus levels: SDA=%d SCL=%d (0=bas, 1=haut).",
             sda, scl);
}

static void bus_drive_idle(void)
{
    const uint64_t mask = (1ULL << CONFIG_I2C_MASTER_SDA_GPIO) |
                          (1ULL << CONFIG_I2C_MASTER_SCL_GPIO);
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    gpio_set_level(CONFIG_I2C_MASTER_SDA_GPIO, 1);
    gpio_set_level(CONFIG_I2C_MASTER_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(CONFIG_I2C_MASTER_SCL_GPIO, 0);
        esp_rom_delay_us(5);
        gpio_set_level(CONFIG_I2C_MASTER_SCL_GPIO, 1);
        esp_rom_delay_us(5);
    }
    gpio_set_level(CONFIG_I2C_MASTER_SDA_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(CONFIG_I2C_MASTER_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(CONFIG_I2C_MASTER_SDA_GPIO, 1);
    esp_rom_delay_us(5);

    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static void init_bus(void)
{
    bus_drive_idle();

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_I2C_MASTER_SCL_GPIO,
        .sda_io_num = CONFIG_I2C_MASTER_SDA_GPIO,
        .glitch_ignore_cnt = 7,
    };
#if CONFIG_I2C_MASTER_ENABLE_INTERNAL_PULLUPS
    cfg.flags.enable_internal_pullup = 1;
#endif
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

static void scan_once(void)
{
    bool ch422g_seen = false;
    for (uint16_t addr = SCAN_ADDR_START; addr <= SCAN_ADDR_END; ++addr) {
        esp_err_t ret = i2c_master_probe(s_bus, addr, 50);
        if (ret == ESP_OK) {
            if (addr >= CH422G_ADDR_MIN && addr <= CH422G_ADDR_MAX) {
                ch422g_seen = true;
                ESP_LOGI(TAG, "CH422G candidat détecté à 0x%02X", (int)addr);
            } else {
                ESP_LOGD(TAG, "Peripheral detected at 0x%02X", (int)addr);
            }
        } else if (ret != ESP_ERR_INVALID_RESPONSE && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG,
                     "Probe 0x%02X returned %s",
                     (int)addr, esp_err_to_name(ret));
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (!ch422g_seen) {
        ESP_LOGW(TAG,
                 "Aucun périphérique n'a répondu entre 0x%02X et 0x%02X.",
                 CH422G_ADDR_MIN, CH422G_ADDR_MAX);
    }
}

void app_main(void)
{
    esp_err_t wdt_ret = esp_task_wdt_deinit();
    if (wdt_ret != ESP_OK && wdt_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_task_wdt_deinit failed: %s", esp_err_to_name(wdt_ret));
    }
    ESP_LOGI(TAG,
             "Scanner I2C initialisé (SDA=%d SCL=%d, fréquence=%d Hz)",
             CONFIG_I2C_MASTER_SDA_GPIO,
             CONFIG_I2C_MASTER_SCL_GPIO,
             CONFIG_I2C_MASTER_FREQUENCY_HZ);

    report_bus_levels();
    init_bus();

    while (true) {
        report_bus_levels();
        scan_once();
        ESP_LOGI(TAG, "--- Nouvelle itération de scan dans 1 seconde ---");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
