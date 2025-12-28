/**
 * @file discovery.h
 * @brief Home Assistant MQTT Discovery configuration
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

// ============================================================================
// Entity Counts
// ============================================================================

#define DISCOVERY_SENSOR_COUNT          17
#define DISCOVERY_BINARY_SENSOR_COUNT   11
#define DISCOVERY_NUMBER_COUNT          5
#define DISCOVERY_SWITCH_COUNT          2

#define DISCOVERY_TOTAL_ENTITIES        (DISCOVERY_SENSOR_COUNT + \
                                         DISCOVERY_BINARY_SENSOR_COUNT + \
                                         DISCOVERY_NUMBER_COUNT + \
                                         DISCOVERY_SWITCH_COUNT)

// ============================================================================
// Discovery Interface
// ============================================================================

/**
 * @brief Publish all Home Assistant discovery configs
 *
 * Publishes discovery configs for all sensors, binary sensors,
 * and control entities (if enabled).
 *
 * @param client MQTT client handle
 * @param control_enabled Whether to publish control entities
 * @return ESP_OK on success
 */
esp_err_t discovery_publish_all(esp_mqtt_client_handle_t client, bool control_enabled);

/**
 * @brief Publish sensor discovery configs
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t discovery_publish_sensors(esp_mqtt_client_handle_t client);

/**
 * @brief Publish binary sensor discovery configs
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t discovery_publish_binary_sensors(esp_mqtt_client_handle_t client);

/**
 * @brief Publish number entity discovery configs (for setpoint control)
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t discovery_publish_number_entities(esp_mqtt_client_handle_t client);

/**
 * @brief Publish switch entity discovery configs (for dosing control)
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t discovery_publish_switch_entities(esp_mqtt_client_handle_t client);

/**
 * @brief Remove all discovery configs from Home Assistant
 *
 * Publishes empty payloads to remove all entities.
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t discovery_remove_all(esp_mqtt_client_handle_t client);

// ============================================================================
// Topic Helpers
// ============================================================================

/**
 * @brief Build a discovery topic
 *
 * Builds: {discovery_prefix}/{component}/intellichem/{entity_id}/config
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param component HA component type (sensor, binary_sensor, number, switch)
 * @param entity_id Unique entity identifier
 * @return Length of topic string
 */
size_t discovery_build_topic(char *buf, size_t buf_size,
                              const char *component, const char *entity_id);

/**
 * @brief Get device info JSON snippet
 *
 * Returns the device identification block for discovery configs.
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Length of JSON string
 */
size_t discovery_get_device_info(char *buf, size_t buf_size);

#endif // DISCOVERY_H
