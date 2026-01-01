/**
 * @file publisher.h
 * @brief MQTT state publishing helpers
 */

#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"
#include "../models/state.h"

// ============================================================================
// Topic Building
// ============================================================================

/**
 * @brief Build a state topic path
 *
 * Builds: {prefix}/intellichem/{path}
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param path Topic path (e.g., "ph/level")
 * @return Length of topic string
 */
size_t publisher_build_topic(char *buf, size_t buf_size, const char *path);

/**
 * @brief Build a command topic path
 *
 * Builds: {prefix}/intellichem/set/{command}
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param command Command name (e.g., "ph_setpoint")
 * @return Length of topic string
 */
size_t publisher_build_command_topic(char *buf, size_t buf_size, const char *command);

/**
 * @brief Get availability topic
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Length of topic string
 */
size_t publisher_get_availability_topic(char *buf, size_t buf_size);

// ============================================================================
// State Publishing
// ============================================================================

/**
 * @brief Publish complete IntelliChem state
 *
 * Publishes all state values to individual MQTT topics.
 *
 * @param client MQTT client handle
 * @param state Current IntelliChem state
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_state(esp_mqtt_client_handle_t client,
                                   const intellichem_state_t *state);

/**
 * @brief Publish pH state
 *
 * @param client MQTT client handle
 * @param state Chemical state
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_ph_state(esp_mqtt_client_handle_t client,
                                      const chemical_state_t *ph);

/**
 * @brief Publish ORP state
 *
 * @param client MQTT client handle
 * @param state Chemical state
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_orp_state(esp_mqtt_client_handle_t client,
                                       const chemical_state_t *orp);

/**
 * @brief Publish water chemistry state
 *
 * @param client MQTT client handle
 * @param state Full IntelliChem state
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_chemistry_state(esp_mqtt_client_handle_t client,
                                             const intellichem_state_t *state);

/**
 * @brief Publish alarm states
 *
 * @param client MQTT client handle
 * @param alarms Alarm flags
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_alarms(esp_mqtt_client_handle_t client,
                                    const alarms_t *alarms);

/**
 * @brief Publish warning states
 *
 * @param client MQTT client handle
 * @param warnings Warning flags
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_warnings(esp_mqtt_client_handle_t client,
                                      const warnings_t *warnings);

/**
 * @brief Publish availability status
 *
 * @param client MQTT client handle
 * @param online True for online, false for offline
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_availability(esp_mqtt_client_handle_t client, bool online);

/**
 * @brief Publish communication error state
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_comms_error(esp_mqtt_client_handle_t client);

/**
 * @brief Publish communication restored state
 *
 * @param client MQTT client handle
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_comms_restored(esp_mqtt_client_handle_t client);

// ============================================================================
// JSON Helpers
// ============================================================================

/**
 * @brief Publish a JSON object as complete state
 *
 * Publishes to {prefix}/intellichem/status
 *
 * @param client MQTT client handle
 * @param state Full IntelliChem state
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_json_state(esp_mqtt_client_handle_t client,
                                        const intellichem_state_t *state);

/**
 * @brief Publish diagnostic information for remote debugging
 *
 * Publishes to {prefix}/intellichem/diagnostics
 * Includes: polls_sent, responses_received, errors, states_published, uptime, free_heap
 *
 * @param client MQTT client handle
 * @param polls_sent Number of status polls sent
 * @param responses_received Number of valid responses received
 * @param serial_errors Number of serial errors
 * @param states_published Number of states published to MQTT
 * @param mqtt_reconnections Number of MQTT reconnections
 * @return ESP_OK on success
 */
esp_err_t publisher_publish_diagnostics(esp_mqtt_client_handle_t client,
                                         uint32_t polls_sent,
                                         uint32_t responses_received,
                                         uint32_t serial_errors,
                                         uint32_t states_published,
                                         uint32_t mqtt_reconnections);

#endif // PUBLISHER_H
