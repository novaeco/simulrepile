#include "sensors.h"
#include "i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define SHT31_ADDR_PRIMARY 0x44
#define SHT31_ADDR_SECONDARY 0x45
#define TMP117_ADDR_BASE 0x48
#define BH1750_ADDR_LOW 0x23
#define BH1750_ADDR_HIGH 0x5C
#define TCA9548_ADDR 0x70

#define SHT31_CACHE_VALID_MS 500U
#define MAX_I2C_DEVICES 16
#define SENSORS_REAL_MAX_CHANNELS 8

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t handle;
} shared_device_t;

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t address;
    bool present;
} sensor_dev_t;

typedef struct {
    sensor_dev_t sht31;
    sensor_dev_t tmp117;
    sensor_dev_t bh1750;
    uint8_t mux_mask;
    bool uses_mux;
    bool bh1750_ready;
    float sht31_cached_temp;
    float sht31_cached_hum;
    TickType_t sht31_cache_ts;
    bool sht31_cache_valid;
} sensor_channel_t;

static const char *TAG = "sensors_real";

static const uint8_t k_sht31_addresses[] = {
    SHT31_ADDR_PRIMARY,
    SHT31_ADDR_SECONDARY,
};

static const uint8_t k_tmp117_addresses[] = {
    TMP117_ADDR_BASE + 0,
    TMP117_ADDR_BASE + 1,
    TMP117_ADDR_BASE + 2,
    TMP117_ADDR_BASE + 3,
    TMP117_ADDR_BASE + 4,
    TMP117_ADDR_BASE + 5,
    TMP117_ADDR_BASE + 6,
    TMP117_ADDR_BASE + 7,
};

static const uint8_t k_bh1750_addresses[] = {
    BH1750_ADDR_LOW,
    BH1750_ADDR_HIGH,
};

static shared_device_t s_devices[MAX_I2C_DEVICES];
static size_t s_device_count = 0;
static sensor_channel_t s_channels[SENSORS_REAL_MAX_CHANNELS];
static size_t s_channel_count = 0;
static bool s_mux_present = false;
static uint8_t s_mux_active_mask = 0;
static i2c_master_dev_handle_t s_mux_dev = NULL;

static esp_err_t mux_select(uint8_t mask)
{
    if (!s_mux_present) {
        return ESP_OK;
    }
    if (mask == s_mux_active_mask) {
        return ESP_OK;
    }
    esp_err_t ret = DEV_I2C_Write_Nbyte(s_mux_dev, &mask, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select TCA9548 mask 0x%02X: %s", mask, esp_err_to_name(ret));
        return ret;
    }
    s_mux_active_mask = mask;
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

static void mux_disable_all(void)
{
    if (s_mux_present) {
        if (mux_select(0) != ESP_OK) {
            ESP_LOGW(TAG, "Unable to disable all TCA9548 channels");
        }
    }
}

static esp_err_t get_or_create_device(uint8_t addr, i2c_master_dev_handle_t *out)
{
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i].address == addr) {
            *out = s_devices[i].handle;
            return ESP_OK;
        }
    }
    if (s_device_count >= MAX_I2C_DEVICES) {
        ESP_LOGE(TAG, "I2C device table full");
        return ESP_ERR_NO_MEM;
    }
    i2c_master_dev_handle_t handle = NULL;
    esp_err_t ret = DEV_I2C_Set_Slave_Addr(&handle, addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02X: %s", addr, esp_err_to_name(ret));
        return ret;
    }
    s_devices[s_device_count++] = (shared_device_t){
        .address = addr,
        .handle = handle,
    };
    *out = handle;
    return ESP_OK;
}

static bool probe_device(uint8_t mux_mask,
                         const uint8_t *addresses,
                         size_t address_count,
                         sensor_dev_t *out_dev)
{
    if (!out_dev) {
        return false;
    }
    memset(out_dev, 0, sizeof(*out_dev));
    if (s_mux_present) {
        if (mux_select(mux_mask) != ESP_OK) {
            return false;
        }
    }
    for (size_t i = 0; i < address_count; ++i) {
        uint8_t addr = addresses[i];
        if (DEV_I2C_Probe(addr) != ESP_OK) {
            continue;
        }
        i2c_master_dev_handle_t handle = NULL;
        if (get_or_create_device(addr, &handle) != ESP_OK) {
            continue;
        }
        out_dev->dev = handle;
        out_dev->address = addr;
        out_dev->present = true;
        return true;
    }
    return false;
}

static bool init_bh1750(sensor_channel_t *channel)
{
    if (!channel->bh1750.present) {
        return false;
    }
    if (mux_select(channel->mux_mask) != ESP_OK) {
        return false;
    }
    uint8_t cmd = 0x01; // power on
    if (DEV_I2C_Write_Nbyte(channel->bh1750.dev, &cmd, 1) != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 0x%02X power on failed", channel->bh1750.address);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t mode = 0x10; // continuous high-res mode
    if (DEV_I2C_Write_Nbyte(channel->bh1750.dev, &mode, 1) != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 0x%02X mode set failed", channel->bh1750.address);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(180));
    channel->bh1750_ready = true;
    return true;
}

static void bh1750_power_down(sensor_channel_t *channel)
{
    if (!channel->bh1750.present) {
        return;
    }
    if (mux_select(channel->mux_mask) != ESP_OK) {
        return;
    }
    uint8_t cmd = 0x00;
    DEV_I2C_Write_Nbyte(channel->bh1750.dev, &cmd, 1);
}

static esp_err_t update_sht31_cache(sensor_channel_t *channel)
{
    if (!channel->sht31.present) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t now = xTaskGetTickCount();
    if (channel->sht31_cache_valid) {
        TickType_t elapsed = now - channel->sht31_cache_ts;
        if (elapsed <= pdMS_TO_TICKS(SHT31_CACHE_VALID_MS)) {
            return ESP_OK;
        }
    }
    if (mux_select(channel->mux_mask) != ESP_OK) {
        channel->sht31_cache_valid = false;
        return ESP_FAIL;
    }
    uint8_t cmd[2] = {0x2C, 0x06};
    esp_err_t ret = DEV_I2C_Write_Nbyte(channel->sht31.dev, cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT31 0x%02X command failed: %s",
                 channel->sht31.address,
                 esp_err_to_name(ret));
        channel->sht31_cache_valid = false;
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(15));
    uint8_t data[6] = {0};
    ret = DEV_I2C_Read_Nbyte(channel->sht31.dev, 0x00, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT31 0x%02X read failed: %s",
                 channel->sht31.address,
                 esp_err_to_name(ret));
        channel->sht31_cache_valid = false;
        return ret;
    }
    uint16_t raw_t = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t raw_h = (uint16_t)((data[3] << 8) | data[4]);
    channel->sht31_cached_temp = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    channel->sht31_cached_hum = 100.0f * ((float)raw_h / 65535.0f);
    channel->sht31_cache_ts = now;
    channel->sht31_cache_valid = true;
    return ESP_OK;
}

static void reset_state(void)
{
    memset(s_devices, 0, sizeof(s_devices));
    memset(s_channels, 0, sizeof(s_channels));
    s_device_count = 0;
    s_channel_count = 0;
    s_mux_present = false;
    s_mux_active_mask = 0;
    s_mux_dev = NULL;
}

static bool setup_channel_with_mux(uint8_t mux_mask, sensor_channel_t *out_channel)
{
    sensor_channel_t channel = {
        .mux_mask = mux_mask,
        .uses_mux = true,
    };

    probe_device(mux_mask, k_sht31_addresses, ARRAY_SIZE(k_sht31_addresses), &channel.sht31);
    probe_device(mux_mask, k_tmp117_addresses, ARRAY_SIZE(k_tmp117_addresses), &channel.tmp117);
    probe_device(mux_mask, k_bh1750_addresses, ARRAY_SIZE(k_bh1750_addresses), &channel.bh1750);

    if (channel.bh1750.present) {
        if (!init_bh1750(&channel)) {
            channel.bh1750.present = false;
            channel.bh1750_ready = false;
        }
    }

    if (!channel.sht31.present && !channel.tmp117.present && !channel.bh1750.present) {
        return false;
    }

    *out_channel = channel;
    return true;
}

typedef struct {
    sensor_dev_t devs[SENSORS_REAL_MAX_CHANNELS];
    size_t count;
} device_list_t;

static void list_reset(device_list_t *list)
{
    list->count = 0;
    memset(list->devs, 0, sizeof(list->devs));
}

static void detect_direct_devices(const uint8_t *addresses,
                                  size_t address_count,
                                  device_list_t *out_list)
{
    list_reset(out_list);
    for (size_t i = 0; i < address_count && out_list->count < SENSORS_REAL_MAX_CHANNELS; ++i) {
        uint8_t addr = addresses[i];
        if (DEV_I2C_Probe(addr) != ESP_OK) {
            continue;
        }
        i2c_master_dev_handle_t handle = NULL;
        if (get_or_create_device(addr, &handle) != ESP_OK) {
            continue;
        }
        out_list->devs[out_list->count++] = (sensor_dev_t){
            .dev = handle,
            .address = addr,
            .present = true,
        };
    }
}

static size_t max_size(size_t a, size_t b)
{
    return (a > b) ? a : b;
}

static size_t max3(size_t a, size_t b, size_t c)
{
    return max_size(max_size(a, b), c);
}

static bool setup_channels_direct(void)
{
    device_list_t sht_list;
    device_list_t tmp_list;
    device_list_t lux_list;

    detect_direct_devices(k_sht31_addresses, ARRAY_SIZE(k_sht31_addresses), &sht_list);
    detect_direct_devices(k_tmp117_addresses, ARRAY_SIZE(k_tmp117_addresses), &tmp_list);
    detect_direct_devices(k_bh1750_addresses, ARRAY_SIZE(k_bh1750_addresses), &lux_list);

    size_t needed = max3(sht_list.count, tmp_list.count, lux_list.count);
    if (needed == 0) {
        return false;
    }
    if (needed > SENSORS_REAL_MAX_CHANNELS) {
        needed = SENSORS_REAL_MAX_CHANNELS;
    }

    for (size_t i = 0; i < needed && s_channel_count < SENSORS_REAL_MAX_CHANNELS; ++i) {
        sensor_channel_t channel = {0};
        channel.mux_mask = 0;
        channel.uses_mux = false;
        if (i < sht_list.count) {
            channel.sht31 = sht_list.devs[i];
        }
        if (i < tmp_list.count) {
            channel.tmp117 = tmp_list.devs[i];
        }
        if (i < lux_list.count) {
            channel.bh1750 = lux_list.devs[i];
            if (!init_bh1750(&channel)) {
                channel.bh1750.present = false;
                channel.bh1750_ready = false;
            }
        }
        if (!channel.sht31.present && !channel.tmp117.present && !channel.bh1750.present) {
            continue;
        }
        s_channels[s_channel_count++] = channel;
    }
    return (s_channel_count > 0);
}

static esp_err_t sensors_real_init(void)
{
    reset_state();
    DEV_I2C_Port port = DEV_I2C_Init();
    (void)port;

    if (DEV_I2C_Probe(TCA9548_ADDR) == ESP_OK) {
        if (DEV_I2C_Set_Slave_Addr(&s_mux_dev, TCA9548_ADDR) == ESP_OK) {
            s_mux_present = true;
            ESP_LOGI(TAG, "Detected TCA9548 I2C multiplexer");
            mux_disable_all();
        } else {
            ESP_LOGE(TAG, "Failed to register TCA9548 multiplexer");
            s_mux_dev = NULL;
            s_mux_present = false;
        }
    }

    bool any_channel = false;

    if (s_mux_present) {
        for (uint8_t idx = 0; idx < SENSORS_REAL_MAX_CHANNELS; ++idx) {
            uint8_t mask = (uint8_t)(1u << idx);
            sensor_channel_t channel = {0};
            if (!setup_channel_with_mux(mask, &channel)) {
                continue;
            }
            ESP_LOGI(TAG,
                     "Channel %u via mux mask 0x%02X:%s%s%s",
                     (unsigned)s_channel_count,
                     mask,
                     channel.sht31.present ? " SHT31" : "",
                     channel.tmp117.present ? " TMP117" : "",
                     channel.bh1750.present ? " BH1750" : "");
            s_channels[s_channel_count++] = channel;
            any_channel = true;
            if (s_channel_count >= SENSORS_REAL_MAX_CHANNELS) {
                break;
            }
        }
        mux_disable_all();
    }

    if (!any_channel) {
        any_channel = setup_channels_direct();
    }

    if (!any_channel) {
        ESP_LOGW(TAG, "No sensors detected");
        reset_state();
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Initialized %u sensor channel(s)", (unsigned)s_channel_count);
    return ESP_OK;
}

static float read_temperature_channel(sensor_channel_t *channel)
{
    if (!channel) {
        return NAN;
    }
    float sum = 0.0f;
    uint32_t count = 0;

    if (channel->tmp117.present) {
        if (mux_select(channel->mux_mask) == ESP_OK) {
            uint16_t raw_tmp = 0;
            if (DEV_I2C_Read_Word(channel->tmp117.dev, 0x00, &raw_tmp) == ESP_OK) {
                sum += (int16_t)raw_tmp * 0.0078125f;
                ++count;
            }
        }
    }

    if (channel->sht31.present) {
        if (update_sht31_cache(channel) == ESP_OK) {
            sum += channel->sht31_cached_temp;
            ++count;
        }
    }

    if (count == 0) {
        return NAN;
    }
    return sum / (float)count;
}

static float read_humidity_channel(sensor_channel_t *channel)
{
    if (!channel || !channel->sht31.present) {
        return NAN;
    }
    if (update_sht31_cache(channel) != ESP_OK) {
        return NAN;
    }
    return channel->sht31_cached_hum;
}

static float read_lux_channel(sensor_channel_t *channel)
{
    if (!channel || !channel->bh1750.present || !channel->bh1750_ready) {
        return NAN;
    }
    if (mux_select(channel->mux_mask) != ESP_OK) {
        return NAN;
    }
    uint8_t data[2] = {0};
    if (DEV_I2C_Read_Nbyte(channel->bh1750.dev, 0x00, data, sizeof(data)) != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 0x%02X read failed", channel->bh1750.address);
        return NAN;
    }
    uint16_t raw = (uint16_t)((data[0] << 8) | data[1]);
    if (raw == 0xFFFFu) {
        return NAN;
    }
    return (float)raw / 1.2f;
}

static float sensors_real_read_temperature(void)
{
    if (s_channel_count == 0) {
        return NAN;
    }
    float sum = 0.0f;
    uint32_t count = 0;
    for (size_t i = 0; i < s_channel_count; ++i) {
        float val = read_temperature_channel(&s_channels[i]);
        if (!isnan(val)) {
            sum += val;
            ++count;
        }
    }
    return (count > 0) ? (sum / (float)count) : NAN;
}

static float sensors_real_read_humidity(void)
{
    if (s_channel_count == 0) {
        return NAN;
    }
    float sum = 0.0f;
    uint32_t count = 0;
    for (size_t i = 0; i < s_channel_count; ++i) {
        float val = read_humidity_channel(&s_channels[i]);
        if (!isnan(val)) {
            sum += val;
            ++count;
        }
    }
    return (count > 0) ? (sum / (float)count) : NAN;
}

static float sensors_real_read_lux(void)
{
    if (s_channel_count == 0) {
        return NAN;
    }
    float sum = 0.0f;
    uint32_t count = 0;
    for (size_t i = 0; i < s_channel_count; ++i) {
        float val = read_lux_channel(&s_channels[i]);
        if (!isnan(val)) {
            sum += val;
            ++count;
        }
    }
    return (count > 0) ? (sum / (float)count) : NAN;
}

static void sensors_real_deinit(void)
{
    for (size_t i = 0; i < s_channel_count; ++i) {
        bh1750_power_down(&s_channels[i]);
    }
    mux_disable_all();

    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i].handle) {
            i2c_master_bus_rm_device(s_devices[i].handle);
            s_devices[i].handle = NULL;
        }
    }
    s_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    if (s_mux_dev) {
        i2c_master_bus_rm_device(s_mux_dev);
        s_mux_dev = NULL;
    }
    s_mux_present = false;
    s_mux_active_mask = 0;
    s_channel_count = 0;
    memset(s_channels, 0, sizeof(s_channels));
}

static size_t sensors_real_channel_count(void)
{
    return s_channel_count;
}

static float sensors_real_read_temperature_channel(size_t channel)
{
    if (channel >= s_channel_count) {
        return NAN;
    }
    return read_temperature_channel(&s_channels[channel]);
}

static float sensors_real_read_humidity_channel(size_t channel)
{
    if (channel >= s_channel_count) {
        return NAN;
    }
    return read_humidity_channel(&s_channels[channel]);
}

static float sensors_real_read_lux_channel(size_t channel)
{
    if (channel >= s_channel_count) {
        return NAN;
    }
    return read_lux_channel(&s_channels[channel]);
}

const sensor_driver_t sensors_real_driver = {
    .init = sensors_real_init,
    .read_temperature = sensors_real_read_temperature,
    .read_humidity = sensors_real_read_humidity,
    .read_lux = sensors_real_read_lux,
    .deinit = sensors_real_deinit,
    .get_channel_count = sensors_real_channel_count,
    .read_temperature_channel = sensors_real_read_temperature_channel,
    .read_humidity_channel = sensors_real_read_humidity_channel,
    .read_lux_channel = sensors_real_read_lux_channel,
};

