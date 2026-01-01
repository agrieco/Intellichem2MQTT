/**
 * @file wifi_prov.h
 * @brief WiFi provisioning with captive portal
 *
 * Uses SoftAP with embedded web server for WiFi configuration.
 * Credentials are stored in NVS and survive reboots.
 */

#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

// Event bits for WiFi connection status
#define WIFI_PROV_CONNECTED_BIT  BIT0
#define WIFI_PROV_FAIL_BIT       BIT1

/**
 * @brief MQTT configuration from web provisioning
 */
typedef struct {
    char broker_uri[128];    // e.g., "mqtt://192.168.1.100:1883"
    char username[64];       // MQTT username (optional)
    char password[64];       // MQTT password (optional)
    char topic_prefix[64];   // e.g., "intellichem2mqtt"
} mqtt_config_t;

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
 * If not provisioned, starts open SoftAP "IntelliChem-Setup"
 * with captive portal web interface for credential entry.
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

/**
 * @brief Get MQTT configuration from web provisioning
 *
 * Returns MQTT settings entered via the captive portal.
 * If no web config exists, returns false and caller should
 * use Kconfig defaults.
 *
 * @param config Output structure to fill
 * @return true if web config exists, false to use defaults
 */
bool wifi_prov_get_mqtt_config(mqtt_config_t *config);

/**
 * @brief Start HTTP server for debug endpoints (in STA mode)
 *
 * Call this after WiFi is connected to start an HTTP server
 * that can be used for debug endpoints. This is separate from
 * the captive portal server used during provisioning.
 *
 * @return httpd_handle_t on success, NULL on failure
 */
httpd_handle_t wifi_prov_start_debug_server(void);

/**
 * @brief Get the debug HTTP server handle
 *
 * @return httpd_handle_t or NULL if not running
 */
httpd_handle_t wifi_prov_get_debug_server(void);

/**
 * @brief Stop the debug HTTP server
 */
void wifi_prov_stop_debug_server(void);

#endif // WIFI_PROV_H
