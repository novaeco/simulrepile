#include "sd.h"

#include <inttypes.h>
#include <string.h>
#include <sys/param.h>

#include "sdkconfig.h"
#include "ch422g.h"
#include "driver/gpio.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdmmc_types.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/spi_periph.h"
#include "sdspi_crc.h"
#include "sdspi_private.h"
#include "sys/lock.h"

#define TAG "sd"

#define SD_SPI_HOST SPI2_HOST
#define SD_SPI_DMA_CHANNEL SPI_DMA_CH_AUTO
#define SD_SPI_MAX_TRANSFER (4 * 1024)
#define SD_SPI_INIT_FREQ_KHZ 400

#define SD_SPI_MOSI 11
#define SD_SPI_MISO 13
#define SD_SPI_SCLK 12

#ifndef CONFIG_CH422G_EXIO_SD_CS
#define CONFIG_CH422G_EXIO_SD_CS 4
#endif

#define CH422G_EXIO_SD_CS CONFIG_CH422G_EXIO_SD_CS

#ifndef CONFIG_STORAGE_SD_USE_GPIO_CS
#define CONFIG_STORAGE_SD_USE_GPIO_CS 0
#endif

#ifndef CONFIG_STORAGE_SD_GPIO_FALLBACK
#define CONFIG_STORAGE_SD_GPIO_FALLBACK 0
#endif

#ifndef CONFIG_STORAGE_SD_GPIO_FALLBACK_AUTO_MOUNT
#define CONFIG_STORAGE_SD_GPIO_FALLBACK_AUTO_MOUNT 0
#endif

#if CONFIG_STORAGE_SD_USE_GPIO_CS || CONFIG_STORAGE_SD_GPIO_FALLBACK
#define STORAGE_SD_HAVE_DIRECT 1
#else
#define STORAGE_SD_HAVE_DIRECT 0
#endif

#if STORAGE_SD_HAVE_DIRECT
#define STORAGE_SD_GPIO_CS CONFIG_STORAGE_SD_GPIO_CS_NUM
#endif

static sdmmc_card_t *s_card = NULL;
static bool s_bus_ready = false;

#if STORAGE_SD_HAVE_DIRECT
static bool s_direct_cs_configured = false;
#endif

static bool s_use_direct_cs = CONFIG_STORAGE_SD_USE_GPIO_CS;
static bool s_forced_fallback = false;

static esp_err_t sd_bus_ensure(void)
{
    if (s_bus_ready) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = SD_SPI_MOSI,
        .miso_io_num = SD_SPI_MISO,
        .sclk_io_num = SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = SD_SPI_MAX_TRANSFER,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };

    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &buscfg, SD_SPI_DMA_CHANNEL);
    if (err == ESP_ERR_INVALID_STATE) {
        s_bus_ready = true;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        s_bus_ready = true;
    }
    return err;
}

#if STORAGE_SD_HAVE_DIRECT
static esp_err_t sd_configure_direct_cs(void)
{
    if (s_direct_cs_configured) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << STORAGE_SD_GPIO_CS,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config CS");
    ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "gpio high");
    s_direct_cs_configured = true;
    return ESP_OK;
}
#endif

static inline esp_err_t sd_ch422g_select(void)
{
    return ch422g_exio_set(CH422G_EXIO_SD_CS, false);
}

static inline esp_err_t sd_ch422g_deselect(void)
{
    return ch422g_exio_set(CH422G_EXIO_SD_CS, true);
}

#ifndef SDSPI_TRANSACTION_COUNT
#define SDSPI_TRANSACTION_COUNT 4
#endif
#ifndef SDSPI_MOSI_IDLE_VAL
#define SDSPI_MOSI_IDLE_VAL     0xff
#endif
#ifndef GPIO_UNUSED
#define GPIO_UNUSED 0xff
#endif
#ifndef SDSPI_BLOCK_BUF_SIZE
#define SDSPI_BLOCK_BUF_SIZE    (SDSPI_MAX_DATA_LEN + 4)
#endif
#ifndef SDSPI_RESPONSE_MAX_DELAY
#define SDSPI_RESPONSE_MAX_DELAY  8
#endif

typedef struct {
    spi_host_device_t host_id;
    spi_device_handle_t spi_handle;
    uint8_t *block_buf;
    SemaphoreHandle_t semphr_int;
    uint16_t duty_cycle_pos;
    uint8_t gpio_cs;
    uint8_t gpio_cd;
    uint8_t gpio_wp;
    uint8_t gpio_int;
    uint8_t poll_busy_start_command_timeout;
    uint8_t gpio_wp_polarity : 1;
    uint8_t data_crc_enabled : 1;
} ch422g_sdspi_slot_t;

static _lock_t s_ch422g_lock;
static bool s_ch422g_app_cmd;

static inline ch422g_sdspi_slot_t *ch422g_slot_from_handle(sdspi_dev_handle_t handle)
{
    if ((uint32_t)handle < SOC_SPI_PERIPH_NUM) {
        return NULL;
    }
    return (ch422g_sdspi_slot_t *)(uintptr_t)handle;
}

static bool ch422g_card_write_protected(ch422g_sdspi_slot_t *slot)
{
    if (slot->gpio_wp == GPIO_UNUSED) {
        return false;
    }
    return gpio_get_level(slot->gpio_wp) == (slot->gpio_wp_polarity ? 1 : 0);
}

static bool ch422g_card_missing(ch422g_sdspi_slot_t *slot)
{
    if (slot->gpio_cd == GPIO_UNUSED) {
        return false;
    }
    return gpio_get_level(slot->gpio_cd) == 1;
}

static esp_err_t ch422g_get_block_buf(ch422g_sdspi_slot_t *slot, uint8_t **out_buf)
{
    if (slot->block_buf == NULL) {
        slot->block_buf = heap_caps_malloc(SDSPI_BLOCK_BUF_SIZE, MALLOC_CAP_DMA);
        if (slot->block_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    *out_buf = slot->block_buf;
    return ESP_OK;
}

static void ch422g_release_bus(ch422g_sdspi_slot_t *slot)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xff},
    };
    spi_device_polling_transmit(slot->spi_handle, &t);
}

static void ch422g_go_idle_clockout(ch422g_sdspi_slot_t *slot)
{
    uint8_t data[12];
    memset(data, 0xff, sizeof(data));
    spi_transaction_t t = {
        .length = 10 * 8,
        .tx_buffer = data,
        .rx_buffer = data,
    };
    spi_device_polling_transmit(slot->spi_handle, &t);
}

static esp_err_t ch422g_poll_busy(ch422g_sdspi_slot_t *slot, int timeout_ms, bool polling)
{
    if (timeout_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    } else if (timeout_ms == 0) {
        return ESP_OK;
    }
    uint8_t t_rx;
    spi_transaction_t t = {
        .tx_buffer = &t_rx,
        .flags = SPI_TRANS_USE_RXDATA,
        .length = 8,
    };
    esp_err_t ret;

    int64_t t_end = esp_timer_get_time() + timeout_ms * 1000;
    int nonzero_count = 0;
    do {
        t_rx = SDSPI_MOSI_IDLE_VAL;
        t.rx_data[0] = 0;
        if (polling) {
            ret = spi_device_polling_transmit(slot->spi_handle, &t);
        } else {
            ret = spi_device_transmit(slot->spi_handle, &t);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        if (t.rx_data[0] != 0) {
            if (++nonzero_count == 2) {
                return ESP_OK;
            }
        }
    } while (esp_timer_get_time() < t_end);
    ESP_LOGD(TAG, "%s: timeout", __func__);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ch422g_poll_data_token(ch422g_sdspi_slot_t *slot, uint8_t *extra_ptr, size_t *extra_size, int timeout_ms)
{
    uint8_t t_rx[8];
    spi_transaction_t t = {
        .tx_buffer = &t_rx,
        .rx_buffer = &t_rx,
        .length = sizeof(t_rx) * 8,
    };
    esp_err_t ret;
    int64_t t_end = esp_timer_get_time() + timeout_ms * 1000;
    do {
        memset(t_rx, SDSPI_MOSI_IDLE_VAL, sizeof(t_rx));
        ret = spi_device_polling_transmit(slot->spi_handle, &t);
        if (ret != ESP_OK) {
            return ret;
        }
        bool found = false;
        for (size_t byte_idx = 0; byte_idx < sizeof(t_rx); byte_idx++) {
            uint8_t rd_data = t_rx[byte_idx];
            if (rd_data == TOKEN_BLOCK_START) {
                found = true;
                memcpy(extra_ptr, t_rx + byte_idx + 1, sizeof(t_rx) - byte_idx - 1);
                *extra_size = sizeof(t_rx) - byte_idx - 1;
                break;
            }
            if (rd_data != 0xff && rd_data != 0) {
                ESP_LOGD(TAG, "%s: received 0x%02x while waiting for data",
                         __func__, rd_data);
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (found) {
            return ESP_OK;
        }
    } while (esp_timer_get_time() < t_end);
    ESP_LOGD(TAG, "%s: timeout", __func__);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ch422g_shift_cmd_response(sdspi_hw_cmd_t *cmd, int sent_bytes)
{
    uint8_t *pr1 = &cmd->r1;
    int ncr_cnt = 1;
    while (true) {
        if ((*pr1 & SD_SPI_R1_NO_RESPONSE) == 0) {
            break;
        }
        pr1++;
        if (++ncr_cnt > 8) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    int copy_bytes = sent_bytes - SDSPI_CMD_SIZE - ncr_cnt;
    if (copy_bytes > 0) {
        memcpy(&cmd->r1, pr1, copy_bytes);
    }

    return ESP_OK;
}

static esp_err_t ch422g_start_command_default(ch422g_sdspi_slot_t *slot, int flags, sdspi_hw_cmd_t *cmd)
{
    size_t cmd_size = SDSPI_CMD_R1_SIZE;
    if ((flags & SDSPI_CMD_FLAG_RSP_R1) ||
        (flags & SDSPI_CMD_FLAG_NORSP) ||
        (flags & SDSPI_CMD_FLAG_RSP_R1B)) {
        cmd_size = SDSPI_CMD_R1_SIZE;
    } else if (flags & SDSPI_CMD_FLAG_RSP_R2) {
        cmd_size = SDSPI_CMD_R2_SIZE;
    } else if (flags & SDSPI_CMD_FLAG_RSP_R3) {
        cmd_size = SDSPI_CMD_R3_SIZE;
    }
    if (flags & SDSPI_CMD_FLAG_RSP_R4) {
        cmd_size = SDSPI_CMD_R4_SIZE;
    } else if (flags & SDSPI_CMD_FLAG_RSP_R5) {
        cmd_size = SDSPI_CMD_R5_SIZE;
    } else if (flags & SDSPI_CMD_FLAG_RSP_R7) {
        cmd_size = SDSPI_CMD_R7_SIZE;
    }
    cmd_size += (SDSPI_NCR_MAX_SIZE - SDSPI_NCR_MIN_SIZE);
    spi_transaction_t t = {
        .flags = 0,
        .length = cmd_size * 8,
        .tx_buffer = cmd,
        .rx_buffer = cmd,
    };
    esp_err_t ret = spi_device_polling_transmit(slot->spi_handle, &t);
    if (cmd->cmd_index == MMC_STOP_TRANSMISSION) {
        cmd->r1 = 0xff;
    }
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "%s: spi_device_polling_transmit returned 0x%x", __func__, ret);
        return ret;
    }
    if (flags & SDSPI_CMD_FLAG_NORSP) {
        cmd->r1 = 0x00;
    }
    ret = ch422g_shift_cmd_response(cmd, cmd_size);
    if (ret != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    if (flags & SDSPI_CMD_FLAG_RSP_R1B) {
        ret = ch422g_poll_busy(slot, cmd->timeout_ms, true);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t ch422g_start_command_read_blocks(ch422g_sdspi_slot_t *slot, sdspi_hw_cmd_t *cmd,
                                                  uint8_t *data, uint32_t rx_length, bool need_stop_command)
{
    spi_transaction_t t_command = {
        .length = (SDSPI_CMD_R1_SIZE + SDSPI_RESPONSE_MAX_DELAY) * 8,
        .tx_buffer = cmd,
        .rx_buffer = cmd,
    };
    esp_err_t ret = spi_device_polling_transmit(slot->spi_handle, &t_command);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t *cmd_u8 = (uint8_t *)cmd;
    size_t pre_scan_data_size = SDSPI_RESPONSE_MAX_DELAY;
    uint8_t *pre_scan_data_ptr = cmd_u8 + SDSPI_CMD_R1_SIZE;

    while ((cmd->r1 & SD_SPI_R1_NO_RESPONSE) != 0 && pre_scan_data_size > 0) {
        cmd->r1 = *pre_scan_data_ptr;
        ++pre_scan_data_ptr;
        --pre_scan_data_size;
    }
    if (cmd->r1 & SD_SPI_R1_NO_RESPONSE) {
        ESP_LOGD(TAG, "no response token found");
        return ESP_ERR_TIMEOUT;
    }

    while (rx_length > 0) {
        size_t extra_data_size = 0;
        const uint8_t *extra_data_ptr = NULL;
        bool need_poll = true;

        for (size_t i = 0; i < pre_scan_data_size; ++i) {
            if (pre_scan_data_ptr[i] == TOKEN_BLOCK_START) {
                extra_data_size = pre_scan_data_size - i - 1;
                extra_data_ptr = pre_scan_data_ptr + i + 1;
                need_poll = false;
                break;
            }
        }

        if (need_poll) {
            ret = ch422g_poll_data_token(slot, cmd_u8 + SDSPI_CMD_R1_SIZE, &extra_data_size, cmd->timeout_ms);
            if (ret != ESP_OK) {
                return ret;
            }
            if (extra_data_size) {
                extra_data_ptr = cmd_u8 + SDSPI_CMD_R1_SIZE;
            }
        }

        size_t will_receive = MIN(rx_length, SDSPI_MAX_DATA_LEN) - extra_data_size;
        uint8_t *rx_data;
        ret = ch422g_get_block_buf(slot, &rx_data);
        if (ret != ESP_OK) {
            return ret;
        }

        const size_t receive_extra_bytes = (rx_length > SDSPI_MAX_DATA_LEN) ? 4 : 2;
        memset(rx_data, 0xff, will_receive + receive_extra_bytes);
        spi_transaction_t t_data = {
            .length = (will_receive + receive_extra_bytes) * 8,
            .rx_buffer = rx_data,
            .tx_buffer = rx_data,
        };

        ret = spi_device_transmit(slot->spi_handle, &t_data);
        if (ret != ESP_OK) {
            return ret;
        }

        uint16_t crc = UINT16_MAX;
        memcpy(&crc, rx_data + will_receive, sizeof(crc));

        pre_scan_data_size = receive_extra_bytes - sizeof(crc);
        pre_scan_data_ptr = rx_data + will_receive + sizeof(crc);

        memcpy(data + extra_data_size, rx_data, will_receive);
        if (extra_data_size) {
            memcpy(data, extra_data_ptr, extra_data_size);
        }

        if (slot->data_crc_enabled) {
            uint16_t crc_of_data = sdspi_crc16(data, will_receive + extra_data_size);
            if (crc_of_data != crc) {
                ESP_LOGE(TAG, "data CRC failed, got=0x%04x expected=0x%04x", crc_of_data, crc);
                return ESP_ERR_INVALID_CRC;
            }
        }

        data += will_receive + extra_data_size;
        rx_length -= will_receive + extra_data_size;
    }

    if (need_stop_command) {
        sdspi_hw_cmd_t stop_cmd;
        make_hw_cmd(MMC_STOP_TRANSMISSION, 0, cmd->timeout_ms, &stop_cmd);
        ret = ch422g_start_command_default(slot, SDSPI_CMD_FLAG_RSP_R1B, &stop_cmd);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t ch422g_start_command_write_blocks(ch422g_sdspi_slot_t *slot, sdspi_hw_cmd_t *cmd,
                                                   const uint8_t *data, uint32_t tx_length, bool multi_block, bool stop_trans)
{
    if (ch422g_card_write_protected(slot)) {
        ESP_LOGW(TAG, "%s: card write protected", __func__);
        return ESP_ERR_INVALID_STATE;
    }

    const int send_bytes = SDSPI_CMD_R5_SIZE + SDSPI_NCR_MAX_SIZE - SDSPI_NCR_MIN_SIZE;
    spi_transaction_t t_command = {
        .length = send_bytes * 8,
        .tx_buffer = cmd,
        .rx_buffer = cmd,
    };
    esp_err_t ret = spi_device_polling_transmit(slot->spi_handle, &t_command);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ch422g_shift_cmd_response(cmd, send_bytes);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "%s: shift_cmd_response returned 0x%x", __func__, ret);
        return ret;
    }

    uint8_t start_token = multi_block ? TOKEN_BLOCK_START_WRITE_MULTI : TOKEN_BLOCK_START;

    while (tx_length > 0) {
        spi_transaction_t t_start_token = {
            .length = sizeof(start_token) * 8,
            .tx_buffer = &start_token,
        };
        ret = spi_device_polling_transmit(slot->spi_handle, &t_start_token);
        if (ret != ESP_OK) {
            return ret;
        }

        size_t will_send = MIN(tx_length, SDSPI_MAX_DATA_LEN);
        const uint8_t *tx_data = data;
        if (!esp_ptr_dma_capable(tx_data)) {
            uint8_t *tmp;
            ret = ch422g_get_block_buf(slot, &tmp);
            if (ret != ESP_OK) {
                return ret;
            }
            memcpy(tmp, tx_data, will_send);
            tx_data = tmp;
        }

        spi_transaction_t t_data = {
            .length = will_send * 8,
            .tx_buffer = tx_data,
        };
        ret = spi_device_transmit(slot->spi_handle, &t_data);
        if (ret != ESP_OK) {
            return ret;
        }

        uint16_t crc = sdspi_crc16(tx_data, will_send);
        const int size_crc_response = sizeof(crc) + 1;
        spi_transaction_t t_crc_rsp = {
            .length = size_crc_response * 8,
            .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        };
        memset(t_crc_rsp.tx_data, 0xff, sizeof(t_crc_rsp.tx_data));
        memcpy(t_crc_rsp.tx_data, &crc, sizeof(crc));

        ret = spi_device_polling_transmit(slot->spi_handle, &t_crc_rsp);
        if (ret != ESP_OK) {
            return ret;
        }

        uint8_t data_rsp = t_crc_rsp.rx_data[sizeof(crc)];
        if (!SD_SPI_DATA_RSP_VALID(data_rsp)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        switch (SD_SPI_DATA_RSP(data_rsp)) {
        case SD_SPI_DATA_ACCEPTED:
            break;
        case SD_SPI_DATA_CRC_ERROR:
            return ESP_ERR_INVALID_CRC;
        case SD_SPI_DATA_WR_ERROR:
            return ESP_FAIL;
        default:
            return ESP_ERR_INVALID_RESPONSE;
        }

        ret = ch422g_poll_busy(slot, cmd->timeout_ms, true);
        if (ret != ESP_OK) {
            return ret;
        }

        tx_length -= will_send;
        data += will_send;
    }

    if (stop_trans) {
        uint8_t stop_token[2] = {
            TOKEN_BLOCK_STOP_WRITE_MULTI,
            SDSPI_MOSI_IDLE_VAL,
        };
        spi_transaction_t t_stop_token = {
            .length = sizeof(stop_token) * 8,
            .tx_buffer = &stop_token,
        };
        ret = spi_device_polling_transmit(slot->spi_handle, &t_stop_token);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = ch422g_poll_busy(slot, cmd->timeout_ms, true);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

static void ch422g_r1_response_to_err(uint8_t r1, int cmd, esp_err_t *out_err)
{
    if (r1 & SD_SPI_R1_NO_RESPONSE) {
        ESP_LOGD(TAG, "cmd=%d, R1 response not found", cmd);
        *out_err = ESP_ERR_TIMEOUT;
    } else if (r1 & SD_SPI_R1_CMD_CRC_ERR) {
        ESP_LOGD(TAG, "cmd=%d, R1 response: command CRC error", cmd);
        *out_err = ESP_ERR_INVALID_CRC;
    } else if (r1 & SD_SPI_R1_ILLEGAL_CMD) {
        ESP_LOGD(TAG, "cmd=%d, R1 response: command not supported", cmd);
        *out_err = ESP_ERR_NOT_SUPPORTED;
    } else if (r1 & SD_SPI_R1_ADDR_ERR) {
        ESP_LOGD(TAG, "cmd=%d, R1 response: alignment error", cmd);
        *out_err = ESP_ERR_INVALID_ARG;
    } else if (r1 & SD_SPI_R1_PARAM_ERR) {
        ESP_LOGD(TAG, "cmd=%d, R1 response: size error", cmd);
        *out_err = ESP_ERR_INVALID_SIZE;
    } else if ((r1 & SD_SPI_R1_ERASE_RST) || (r1 & SD_SPI_R1_ERASE_SEQ_ERR)) {
        *out_err = ESP_ERR_INVALID_STATE;
    } else if (r1 & SD_SPI_R1_IDLE_STATE) {
    } else if (r1 != 0) {
        ESP_LOGD(TAG, "cmd=%d, R1 response: unexpected value 0x%02x", cmd, r1);
        *out_err = ESP_ERR_INVALID_RESPONSE;
    }
}

static void ch422g_r1_sdio_response_to_err(uint8_t r1, int cmd, esp_err_t *out_err)
{
    if (r1 & SD_SPI_R1_NO_RESPONSE) {
        ESP_LOGI(TAG, "cmd=%d, R1 response not found", cmd);
        *out_err = ESP_ERR_TIMEOUT;
    } else if (r1 & SD_SPI_R1_CMD_CRC_ERR) {
        ESP_LOGI(TAG, "cmd=%d, R1 response: command CRC error", cmd);
        *out_err = ESP_ERR_INVALID_CRC;
    } else if (r1 & SD_SPI_R1_ILLEGAL_CMD) {
        ESP_LOGI(TAG, "cmd=%d, R1 response: command not supported", cmd);
        *out_err = ESP_ERR_NOT_SUPPORTED;
    } else if (r1 & SD_SPI_R1_PARAM_ERR) {
        ESP_LOGI(TAG, "cmd=%d, R1 response: size error", cmd);
        *out_err = ESP_ERR_INVALID_SIZE;
    } else if (r1 & SDIO_R1_FUNC_NUM_ERR) {
        ESP_LOGI(TAG, "cmd=%d, R1 response: function number error", cmd);
        *out_err = ESP_ERR_INVALID_ARG;
    } else if (r1 & SD_SPI_R1_IDLE_STATE) {
    } else if (r1 != 0) {
        ESP_LOGI(TAG, "cmd=%d, R1 response: unexpected value 0x%02x", cmd, r1);
        *out_err = ESP_ERR_INVALID_RESPONSE;
    }
}

static esp_err_t ch422g_start_command(ch422g_sdspi_slot_t *slot, sdspi_hw_cmd_t *cmd,
                                      void *data, uint32_t data_size, int flags)
{
    if (slot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ch422g_card_missing(slot)) {
        return ESP_ERR_NOT_FOUND;
    }

    int cmd_index = cmd->cmd_index;
    uint32_t cmd_arg;
    memcpy(&cmd_arg, cmd->arguments, sizeof(cmd_arg));
    cmd_arg = __builtin_bswap32(cmd_arg);
    ESP_LOGV(TAG, "%s: slot=%p, CMD%d, arg=0x%08" PRIx32 " flags=0x%x, data=%p, data_size=%" PRIu32 " crc=0x%02x",
             __func__, (void *)slot, cmd_index, cmd_arg, flags, data, data_size, cmd->crc7);

    spi_device_acquire_bus(slot->spi_handle, portMAX_DELAY);
    esp_err_t ret = ch422g_poll_busy(slot, slot->poll_busy_start_command_timeout, true);
    if (ret != ESP_OK) {
        spi_device_release_bus(slot->spi_handle);
        return ret;
    }

    if (cmd_index == MMC_GO_IDLE_STATE) {
        ch422g_go_idle_clockout(slot);
    }

    ret = sd_ch422g_select();
    if (ret != ESP_OK) {
        spi_device_release_bus(slot->spi_handle);
        return ret;
    }

    if (flags & SDSPI_CMD_FLAG_DATA) {
        const bool multi_block = flags & SDSPI_CMD_FLAG_MULTI_BLK;
        const bool stop_transmission = multi_block && !(flags & SDSPI_CMD_FLAG_RSP_R5);
        if (flags & SDSPI_CMD_FLAG_WRITE) {
            ret = ch422g_start_command_write_blocks(slot, cmd, data, data_size, multi_block, stop_transmission);
        } else {
            ret = ch422g_start_command_read_blocks(slot, cmd, data, data_size, stop_transmission);
        }
    } else {
        ret = ch422g_start_command_default(slot, flags, cmd);
    }

    esp_err_t deselect_err = sd_ch422g_deselect();
    if (deselect_err != ESP_OK && ret == ESP_OK) {
        ret = deselect_err;
    }

    ch422g_release_bus(slot);
    spi_device_release_bus(slot->spi_handle);

    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "%s: cmd=%d error=0x%x", __func__, cmd_index, ret);
    } else {
        if (cmd_index == SD_CRC_ON_OFF) {
            slot->data_crc_enabled = (uint8_t)cmd_arg;
            ESP_LOGD(TAG, "data CRC set=%d", slot->data_crc_enabled);
        }
    }
    return ret;
}

static esp_err_t sdspi_ch422g_do_transaction(sdspi_dev_handle_t handle, sdmmc_command_t *cmdinfo)
{
    ch422g_sdspi_slot_t *slot = ch422g_slot_from_handle(handle);
    if (slot == NULL) {
        ESP_LOGW(TAG, "CH422G transaction fallback to IDF driver (handle=%d)", handle);
        return sdspi_host_do_transaction(handle, cmdinfo);
    }

    _lock_acquire(&s_ch422g_lock);
    WORD_ALIGNED_ATTR sdspi_hw_cmd_t hw_cmd;
    make_hw_cmd(cmdinfo->opcode, cmdinfo->arg, cmdinfo->timeout_ms, &hw_cmd);

    int flags = 0;
    if (SCF_CMD(cmdinfo->flags) == SCF_CMD_ADTC) {
        flags = SDSPI_CMD_FLAG_DATA | SDSPI_CMD_FLAG_WRITE;
    } else if (SCF_CMD(cmdinfo->flags) == (SCF_CMD_ADTC | SCF_CMD_READ)) {
        flags = SDSPI_CMD_FLAG_DATA;
    }

    if (cmdinfo->datalen > SDSPI_MAX_DATA_LEN) {
        flags |= SDSPI_CMD_FLAG_MULTI_BLK;
    }

    if (!s_ch422g_app_cmd && cmdinfo->opcode == SD_SEND_IF_COND) {
        flags |= SDSPI_CMD_FLAG_RSP_R7;
    } else if (!s_ch422g_app_cmd && cmdinfo->opcode == MMC_SEND_STATUS) {
        flags |= SDSPI_CMD_FLAG_RSP_R2;
    } else if (!s_ch422g_app_cmd && cmdinfo->opcode == SD_READ_OCR) {
        flags |= SDSPI_CMD_FLAG_RSP_R3;
    } else if (s_ch422g_app_cmd && cmdinfo->opcode == SD_APP_SD_STATUS) {
        flags |= SDSPI_CMD_FLAG_RSP_R2;
    } else if (!s_ch422g_app_cmd && cmdinfo->opcode == MMC_GO_IDLE_STATE &&
               !(cmdinfo->flags & SCF_RSP_R1)) {
        flags |= SDSPI_CMD_FLAG_NORSP;
    } else if (!s_ch422g_app_cmd && cmdinfo->opcode == SD_IO_SEND_OP_COND) {
        flags |= SDSPI_CMD_FLAG_RSP_R4;
    } else if (!s_ch422g_app_cmd && cmdinfo->opcode == SD_IO_RW_DIRECT) {
        flags |= SDSPI_CMD_FLAG_RSP_R5;
    } else if (!s_ch422g_app_cmd && cmdinfo->opcode == SD_IO_RW_EXTENDED) {
        flags |= SDSPI_CMD_FLAG_RSP_R5 | SDSPI_CMD_FLAG_DATA;
        if (cmdinfo->arg & SD_ARG_CMD53_WRITE) {
            flags |= SDSPI_CMD_FLAG_WRITE;
        }
        if (cmdinfo->arg & SD_ARG_CMD53_BLOCK_MODE) {
            flags |= SDSPI_CMD_FLAG_MULTI_BLK;
        }
    } else if (!s_ch422g_app_cmd &&
               (cmdinfo->opcode == MMC_ERASE || cmdinfo->opcode == MMC_STOP_TRANSMISSION)) {
        flags |= SDSPI_CMD_FLAG_RSP_R1B;
    } else {
        flags |= SDSPI_CMD_FLAG_RSP_R1;
    }

    esp_err_t ret = ch422g_start_command(slot, &hw_cmd, cmdinfo->data, cmdinfo->datalen, flags);

    if (ret == ESP_OK) {
        ESP_LOGV(TAG, "r1 = 0x%02x hw_cmd.r[0]=0x%08" PRIx32, hw_cmd.r1, hw_cmd.response[0]);
        if (flags & (SDSPI_CMD_FLAG_RSP_R1 | SDSPI_CMD_FLAG_RSP_R1B)) {
            cmdinfo->response[0] = hw_cmd.r1;
            ch422g_r1_response_to_err(hw_cmd.r1, cmdinfo->opcode, &ret);
        } else if (flags & SDSPI_CMD_FLAG_RSP_R2) {
            cmdinfo->response[0] = ((uint32_t)hw_cmd.r1) | ((hw_cmd.response[0] & 0xff) << 8);
        } else if (flags & (SDSPI_CMD_FLAG_RSP_R3 | SDSPI_CMD_FLAG_RSP_R7)) {
            ch422g_r1_response_to_err(hw_cmd.r1, cmdinfo->opcode, &ret);
            cmdinfo->response[0] = __builtin_bswap32(hw_cmd.response[0]);
        } else if (flags & SDSPI_CMD_FLAG_RSP_R4) {
            ch422g_r1_sdio_response_to_err(hw_cmd.r1, cmdinfo->opcode, &ret);
            cmdinfo->response[0] = __builtin_bswap32(hw_cmd.response[0]);
        } else if (flags & SDSPI_CMD_FLAG_RSP_R5) {
            ch422g_r1_sdio_response_to_err(hw_cmd.r1, cmdinfo->opcode, &ret);
            cmdinfo->response[0] = hw_cmd.response[0];
        }
        if (cmdinfo->opcode == MMC_GO_IDLE_STATE && hw_cmd.r1 == SD_SPI_R1_IDLE_STATE) {
            ESP_LOGI(TAG, "CMD0 response indicates idle state (R1=0x%02x)", hw_cmd.r1);
        }
    }

    if (ret == ESP_OK) {
        s_ch422g_app_cmd = (cmdinfo->opcode == MMC_APP_CMD);
    } else {
        s_ch422g_app_cmd = false;
    }
    _lock_release(&s_ch422g_lock);
    return ret;
}

bool sd_is_mounted(void)
{
    return s_card != NULL;
}

bool sd_uses_direct_cs(void)
{
#if STORAGE_SD_HAVE_DIRECT
    return s_use_direct_cs;
#else
    return false;
#endif
}

esp_err_t sd_mount(sdmmc_card_t **out_card)
{
    if (s_card != NULL) {
        if (out_card) {
            *out_card = s_card;
        }
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sd_bus_ensure(), TAG, "spi_bus_initialize");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_SPI_INIT_FREQ_KHZ;

    bool use_direct = s_use_direct_cs;
#if STORAGE_SD_HAVE_DIRECT && CONFIG_STORAGE_SD_GPIO_FALLBACK && !CONFIG_STORAGE_SD_GPIO_FALLBACK_AUTO_MOUNT
    if (use_direct && s_forced_fallback) {
        ESP_LOGW(TAG,
                 "Skipping SD mount: CH422G offline and fallback auto-mount disabled. "
                 "Wire EXIO%u→GPIO%d or enable the auto-mount Kconfig option once the "
                 "jumper is installed.",
                 CH422G_EXIO_SD_CS, STORAGE_SD_GPIO_CS);
        return ESP_ERR_INVALID_STATE;
    }
#endif
#if CONFIG_STORAGE_SD_USE_GPIO_CS
    s_forced_fallback = false;
#endif
#if !STORAGE_SD_HAVE_DIRECT
    (void)use_direct;
#endif

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = host.slot;
#if STORAGE_SD_HAVE_DIRECT
    if (!use_direct) {
#endif
        esp_err_t init_err = ch422g_init();
        if (init_err != ESP_OK) {
#if STORAGE_SD_HAVE_DIRECT && CONFIG_STORAGE_SD_GPIO_FALLBACK
            ESP_LOGW(TAG,
                     "CH422G init failed (%s). Falling back to GPIO%d for SD card CS.",
                     esp_err_to_name(init_err), STORAGE_SD_GPIO_CS);
            s_use_direct_cs = true;
            use_direct = true;
            s_forced_fallback = true;
#else
            return init_err;
#endif
        } else {
            ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS idle high");
            s_forced_fallback = false;
        }
#if STORAGE_SD_HAVE_DIRECT
    }

    if (use_direct) {
        ESP_RETURN_ON_ERROR(sd_configure_direct_cs(), TAG, "direct CS setup");
        ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS idle high");
        slot_cfg.gpio_cs = STORAGE_SD_GPIO_CS;
    } else {
        slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;
    }
#else
    slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;
#endif

#if !STORAGE_SD_HAVE_DIRECT
    host.do_transaction = sdspi_ch422g_do_transaction;
#else
    if (!use_direct) {
        host.do_transaction = sdspi_ch422g_do_transaction;
    }
#endif

#if !STORAGE_SD_HAVE_DIRECT
    ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS idle high");
#else
    if (!use_direct) {
        ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS idle high");
    }
#endif

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

#if STORAGE_SD_HAVE_DIRECT
    if (use_direct) {
        ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS release");
    } else {
        ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS release");
    }
#else
    ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS release");
#endif

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount failed: %s", esp_err_to_name(err));
        spi_bus_free(SD_SPI_HOST);
        s_bus_ready = false;
        s_card = NULL;
        return err;
    }

    sdmmc_card_print_info(stdout, s_card);

#if STORAGE_SD_HAVE_DIRECT
    if (use_direct) {
        gpio_set_level(STORAGE_SD_GPIO_CS, 1);
        if (CONFIG_STORAGE_SD_USE_GPIO_CS) {
            ESP_LOGI(TAG, "SD card detected and mounted via GPIO%d CS", STORAGE_SD_GPIO_CS);
        } else {
            ESP_LOGW(TAG, "SD card detected and mounted via GPIO%d fallback CS", STORAGE_SD_GPIO_CS);
        }
    } else {
        sd_ch422g_deselect();
        ESP_LOGI(TAG, "SD card detected and mounted via CH422G-controlled CS");
        s_forced_fallback = false;
    }
#else
    sd_ch422g_deselect();
    ESP_LOGI(TAG, "SD card detected and mounted via CH422G-controlled CS");
#endif

    if (out_card) {
        *out_card = s_card;
    }

    return ESP_OK;
}

esp_err_t sd_unmount(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_vfs_fat_sdcard_unmount failed: %s", esp_err_to_name(err));
    }
    s_card = NULL;

#if STORAGE_SD_HAVE_DIRECT
    if (s_use_direct_cs) {
        if (s_direct_cs_configured) {
            gpio_set_level(STORAGE_SD_GPIO_CS, 1);
        }
    } else {
        sd_ch422g_deselect();
    }
#else
    sd_ch422g_deselect();
#endif

    if (s_bus_ready) {
        spi_bus_free(SD_SPI_HOST);
        s_bus_ready = false;
    }

    return err;
}

esp_err_t sd_card_print_info_stream(FILE *stream)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!stream) {
        stream = stdout;
    }
    sdmmc_card_print_info(stream, s_card);
    return ESP_OK;
}

esp_err_t sd_card_print_info(void)
{
    return sd_card_print_info_stream(stdout);
}

esp_err_t sd_spi_cs_selftest(void)
{
#if STORAGE_SD_HAVE_DIRECT
    if (s_use_direct_cs) {
        ESP_RETURN_ON_ERROR(sd_configure_direct_cs(), TAG, "direct CS");
        ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 0), TAG, "CS low");
        esp_rom_delay_us(5);
        ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS high");
        return ESP_OK;
    }
#endif

    esp_err_t err = ch422g_init();
    if (err != ESP_OK) {
#if STORAGE_SD_HAVE_DIRECT && CONFIG_STORAGE_SD_GPIO_FALLBACK
        if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGW(TAG,
                     "CH422G init failed (%s). Switching self-test to GPIO%d fallback.",
                     esp_err_to_name(err), STORAGE_SD_GPIO_CS);
            s_use_direct_cs = true;
            s_forced_fallback = true;
            ESP_RETURN_ON_ERROR(sd_configure_direct_cs(), TAG, "direct CS");
            ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 0), TAG, "CS low");
            esp_rom_delay_us(5);
            ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS high");
#if !CONFIG_STORAGE_SD_GPIO_FALLBACK_AUTO_MOUNT
            ESP_LOGW(TAG,
                     "GPIO fallback auto-mount disabled – SD mounting will be deferred to "
                     "avoid watchdog resets. Once EXIO%u is wired to GPIO%d, enable "
                     "Component config → Storage / SD card → Automatically mount the "
                     "fallback CS.",
                     CH422G_EXIO_SD_CS, STORAGE_SD_GPIO_CS);
#endif
            return ESP_OK;
        }
#endif
        return err;
    }

    ESP_RETURN_ON_ERROR(sd_ch422g_select(), TAG, "CS low");
    esp_rom_delay_us(5);
    ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS high");
    s_forced_fallback = false;
    return ESP_OK;
}

bool sd_fallback_due_to_ch422g(void)
{
#if STORAGE_SD_HAVE_DIRECT
    return s_use_direct_cs && s_forced_fallback;
#else
    return false;
#endif
}
