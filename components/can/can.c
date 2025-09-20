/*****************************************************************************
 * | File         :   can.c
 * | Author       :   Waveshare team
 * | Function     :   CAN driver code for CAN communication
 * | Info         :
 * |                 This file implements basic CAN communication functions 
 * |                 using the ESP-IDF TWAI driver. It includes initialization, 
 * |                 alert handling, and message transmission/reception.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-28
 * | Info         :   Basic version, includes functions to initialize, 
 * |                 read alerts, and transmit/receive CAN messages.
 *
 ******************************************************************************/

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_log.h"
#include "can.h"  // Include header file for CAN driver functions

static bool can_active = false;

/**
 * @brief Initializes the CAN (TWAI) interface.
 *
 * This function installs and starts the TWAI driver with the specified
 * configurations. It also sets up alert conditions to monitor the CAN bus.
 *
 * @param t_config Timing configuration for CAN communication.
 * @param f_config Filter configuration for CAN messages.
 * @param g_config General configuration for the CAN interface.
 *
 * @return ESP_OK on success, ESP_FAIL otherwise.
 */
esp_err_t can_init(twai_timing_config_t t_config, twai_filter_config_t f_config, twai_general_config_t g_config)
{
    // Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
    {
        ESP_LOGI(CAN_TAG, "Driver installed"); // Log driver installation success
    }
    else
    {
        ESP_LOGI(CAN_TAG, "Failed to install driver"); // Log driver installation failure
        return ESP_FAIL;
    }

    // Start TWAI driver
    if (twai_start() == ESP_OK)
    {
        ESP_LOGI(CAN_TAG, "Driver started"); // Log driver start success
        can_active = true;
    }
    else
    {
        ESP_LOGI(CAN_TAG, "Failed to start driver"); // Log driver start failure
        return ESP_FAIL;
    }

    // Configure alerts for specific CAN events
    uint32_t alerts_to_enable = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED |
                                TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                                TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR;
    if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK)
    {
        ESP_LOGI(CAN_TAG, "CAN Alerts reconfigured"); // Log alert reconfiguration success
    }
    else
    {
        ESP_LOGI(CAN_TAG, "Failed to reconfigure alerts"); // Log alert reconfiguration failure
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool can_is_active(void)
{
    return can_active;
}

/**
 * @brief Reads and handles alerts from the CAN interface.
 *
 * This function reads triggered alerts and provides detailed logs for each alert type.
 *
 * @return A 32-bit value representing the triggered alert type.
 */
uint32_t can_read_alerts()
{
    uint32_t alerts_triggered;                                           // Variable to store triggered alerts
    twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS)); // Read alerts
    twai_status_info_t twaistatus;                                       // Variable to store TWAI status information
    twai_get_status_info(&twaistatus);                                   // Get TWAI status information

    if (alerts_triggered & TWAI_ALERT_ERR_PASS)
    {
        ESP_LOGI(CAN_TAG, "Alert: TWAI controller is in error passive state.");
        return TWAI_ALERT_ERR_PASS;
    }

    if (alerts_triggered & TWAI_ALERT_BUS_ERROR)
    {
        ESP_LOGI(CAN_TAG, "Alert: Bus error occurred.");
        ESP_LOGI(CAN_TAG, "Bus error count: %" PRIu32, twaistatus.bus_error_count);
        return TWAI_ALERT_BUS_ERROR;
    }

    if (alerts_triggered & TWAI_ALERT_TX_FAILED)
    {
        ESP_LOGI(CAN_TAG, "Alert: Transmission failed.");
        ESP_LOGI(CAN_TAG, "TX buffered: %" PRIu32, twaistatus.msgs_to_tx);
        return TWAI_ALERT_TX_FAILED;
    }

    if (alerts_triggered & TWAI_ALERT_TX_SUCCESS)
    {
        ESP_LOGI(CAN_TAG, "Alert: Transmission successful.");
        return TWAI_ALERT_TX_SUCCESS;
    }

    if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL)
    {
        ESP_LOGI(CAN_TAG, "Alert: RX queue full, frame lost.");
        return TWAI_ALERT_RX_QUEUE_FULL;
    }

    if (alerts_triggered & TWAI_ALERT_RX_DATA)
    {
        return TWAI_ALERT_RX_DATA;
    }

    return 0;
}

/**
 * @brief Transmits a CAN message.
 *
 * This function queues a CAN message for transmission and logs the result.
 *
 * @param message The CAN message to be transmitted.
 *
 * @return ESP_OK on success, otherwise the error code returned by
 * `twai_transmit`.
 */
esp_err_t can_write_Byte(can_message_t message)
{
    esp_err_t ret = twai_transmit(&message, portMAX_DELAY);
    if (ret == ESP_OK)
    {
        printf("Message queued for transmission\n"); // Log success
    }
    else
    {
        printf("Failed to queue message for transmission\n"); // Log failure
    }
    return ret;
}

/**
 * @brief Receives a CAN message.
 *
 * This function attempts to read a single CAN frame and only copies the
 * content to @p out_message when a frame has effectively been received. When
 * no frame is available the provided buffer remains untouched, preventing
 * callers from observing indeterminate data.
 *
 * @param[out] out_message Storage for the received CAN message.
 *
 * @return ESP_OK when a frame has been retrieved and copied into
 *         @p out_message, an error code returned by @c twai_receive otherwise.
 */
esp_err_t can_read_Byte(can_message_t *out_message)
{
    if (out_message == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    can_message_t message = {0}; // Ensure deterministic contents even on failure paths
    esp_err_t ret = twai_receive(&message, 0);
    if (ret == ESP_OK)
    {
        if (message.extd)
        {
            ESP_LOGI(CAN_TAG, "Message is in Extended Format");
        }
        else
        {
            ESP_LOGI(CAN_TAG, "Message is in Standard Format");
        }

        printf("ID: %" PRIx32 "\nByte:", message.identifier);
        if (!message.rtr)
        {
            for (int i = 0; i < message.data_length_code; i++)
            {
                printf(" %d = %02x,", i, message.data[i]);
            }
            printf("\n");
        }

        *out_message = message;
    }

    return ret;
}
