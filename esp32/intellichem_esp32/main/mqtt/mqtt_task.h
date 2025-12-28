/**
 * @file mqtt_task.h
 * @brief MQTT task for WiFi and MQTT client management
 */

#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "../models/state.h"

// ============================================================================
// Configuration (from Kconfig)
// ============================================================================

#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID "your_ssid"
#endif

#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD "your_password"
#endif

#ifndef CONFIG_MQTT_BROKER_URI
#define CONFIG_MQTT_BROKER_URI "mqtt://192.168.1.1:1883"
#endif

#ifndef CONFIG_MQTT_USERNAME
#define CONFIG_MQTT_USERNAME ""
#endif

#ifndef CONFIG_MQTT_PASSWORD
#define CONFIG_MQTT_PASSWORD ""
#endif

#ifndef CONFIG_MQTT_TOPIC_PREFIX
#define CONFIG_MQTT_TOPIC_PREFIX "intellichem2mqtt"
#endif

#ifndef CONFIG_MQTT_DISCOVERY_PREFIX
#define CONFIG_MQTT_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef CONFIG_MQTT_KEEPALIVE
#define CONFIG_MQTT_KEEPALIVE 60
#endif

#ifndef CONFIG_MQTT_QOS
#define CONFIG_MQTT_QOS 1
#endif

#ifndef CONFIG_CONTROL_ENABLED
#define CONFIG_CONTROL_ENABLED 1
#endif

// ============================================================================
// Connection Status
// ============================================================================

typedef enum {
    MQTT_CONN_DISCONNECTED = 0,
    MQTT_CONN_WIFI_CONNECTING,
    MQTT_CONN_WIFI_CONNECTED,
    MQTT_CONN_MQTT_CONNECTING,
    MQTT_CONN_MQTT_CONNECTED,
    MQTT_CONN_ERROR,
} mqtt_connection_status_t;

// ============================================================================
// Task Interface
// ============================================================================

/**
 * @brief Initialize and start the MQTT task
 *
 * @param state_queue Queue to receive state updates (from serial task)
 * @param command_queue Queue to send commands (to serial task)
 * @return ESP_OK on success
 */
esp_err_t mqtt_task_start(QueueHandle_t state_queue, QueueHandle_t command_queue);

/**
 * @brief Stop the MQTT task
 */
void mqtt_task_stop(void);

/**
 * @brief Check if MQTT task is running
 *
 * @return true if task is running
 */
bool mqtt_task_is_running(void);

/**
 * @brief Get current connection status
 *
 * @return Current connection status enum
 */
mqtt_connection_status_t mqtt_task_get_status(void);

/**
 * @brief Get connection status as string
 *
 * @return Status string for logging
 */
const char* mqtt_task_status_str(mqtt_connection_status_t status);

/**
 * @brief Check if MQTT is connected
 *
 * @return true if connected to MQTT broker
 */
bool mqtt_task_is_connected(void);

/**
 * @brief Get MQTT task statistics
 *
 * @param states_published Number of state updates published
 * @param discovery_sent Whether discovery has been sent
 * @param reconnections Number of reconnections
 */
void mqtt_task_get_stats(uint32_t *states_published, bool *discovery_sent, uint32_t *reconnections);

/**
 * @brief Force republish of Home Assistant discovery
 *
 * @return ESP_OK if request queued successfully
 */
esp_err_t mqtt_task_republish_discovery(void);

#endif // MQTT_TASK_H
