/**
 * @file wifi_prov.h
 * @brief WiFi provisioning using ESP-IDF provisioning manager
 *
 * Uses SoftAP transport for phone app-based WiFi configuration.
 * Credentials are stored in NVS and survive reboots.
 */

#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Event bits for WiFi connection status
#define WIFI_PROV_CONNECTED_BIT  BIT0
#define WIFI_PROV_FAIL_BIT       BIT1

/**
 * @brief Initialize WiFi provisioning system
 *
 * Sets up NVS, TCP/IP stack, WiFi driver, and event handlers.
 * Must be called before wifi_prov_start().
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_prov_init(void);

/**
 * @brief Start WiFi provisioning or connect to saved network
 *
 * If credentials exist in NVS, connects to saved WiFi network.
 * If not provisioned, starts SoftAP with name "PROV_XXXXXX"
 * and waits for phone app to provide credentials.
 *
 * This function blocks until WiFi is connected or fails.
 *
 * @return ESP_OK if connected, ESP_FAIL on timeout/failure
 */
esp_err_t wifi_prov_start(void);

/**
 * @brief Reset provisioned credentials
 *
 * Clears saved WiFi credentials from NVS.
 * Next boot will enter provisioning mode.
 */
void wifi_prov_reset(void);

/**
 * @brief Check if device is provisioned
 *
 * @return true if WiFi credentials exist in NVS
 */
bool wifi_prov_is_provisioned(void);

/**
 * @brief Check if currently connected to WiFi
 *
 * @return true if WiFi STA is connected with IP address
 */
bool wifi_prov_is_connected(void);

/**
 * @brief Get the WiFi event group
 *
 * Can be used to wait for connection events.
 *
 * @return Event group handle, or NULL if not initialized
 */
EventGroupHandle_t wifi_prov_get_event_group(void);

#endif // WIFI_PROV_H
