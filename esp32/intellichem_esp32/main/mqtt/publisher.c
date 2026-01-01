/**
 * @file publisher.c
 * @brief MQTT state publishing implementation
 */

#include "publisher.h"
#include "mqtt_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "publisher";

// ============================================================================
// Helper Macros
// ============================================================================

#define PUBLISH(topic, payload) do { \
    ESP_LOGI(TAG, "MQTT PUB: %s = %s", topic, payload); \
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, CONFIG_MQTT_QOS, 0); \
    if (msg_id < 0) { \
        ESP_LOGE(TAG, "Failed to publish to %s", topic); \
        return ESP_FAIL; \
    } \
} while(0)

#define PUBLISH_RETAIN(topic, payload) do { \
    ESP_LOGI(TAG, "MQTT PUB (retain): %s = %s", topic, payload); \
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, CONFIG_MQTT_QOS, 1); \
    if (msg_id < 0) { \
        ESP_LOGE(TAG, "Failed to publish to %s", topic); \
        return ESP_FAIL; \
    } \
} while(0)

// ============================================================================
// Topic Building
// ============================================================================

size_t publisher_build_topic(char *buf, size_t buf_size, const char *path)
{
    return snprintf(buf, buf_size, "%s/intellichem/%s", mqtt_task_get_topic_prefix(), path);
}

size_t publisher_build_command_topic(char *buf, size_t buf_size, const char *command)
{
    return snprintf(buf, buf_size, "%s/intellichem/set/%s", mqtt_task_get_topic_prefix(), command);
}

size_t publisher_get_availability_topic(char *buf, size_t buf_size)
{
    return snprintf(buf, buf_size, "%s/intellichem/availability", mqtt_task_get_topic_prefix());
}

// ============================================================================
// State Publishing
// ============================================================================

esp_err_t publisher_publish_state(esp_mqtt_client_handle_t client,
                                   const intellichem_state_t *state)
{
    if (client == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Publishing complete state");

    // Publish JSON state first
    esp_err_t ret = publisher_publish_json_state(client, state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to publish JSON state");
    }

    // Publish individual topics
    ret = publisher_publish_ph_state(client, &state->ph);
    if (ret != ESP_OK) return ret;

    ret = publisher_publish_orp_state(client, &state->orp);
    if (ret != ESP_OK) return ret;

    ret = publisher_publish_chemistry_state(client, state);
    if (ret != ESP_OK) return ret;

    ret = publisher_publish_alarms(client, &state->alarms);
    if (ret != ESP_OK) return ret;

    ret = publisher_publish_warnings(client, &state->warnings);
    if (ret != ESP_OK) return ret;

    ESP_LOGD(TAG, "State published successfully");
    return ESP_OK;
}

esp_err_t publisher_publish_ph_state(esp_mqtt_client_handle_t client,
                                      const chemical_state_t *ph)
{
    char topic[128];
    char payload[32];

    // pH level
    publisher_build_topic(topic, sizeof(topic), "ph/level");
    snprintf(payload, sizeof(payload), "%.2f", ph->level);
    PUBLISH(topic, payload);

    // pH setpoint
    publisher_build_topic(topic, sizeof(topic), "ph/setpoint");
    snprintf(payload, sizeof(payload), "%.2f", ph->setpoint);
    PUBLISH(topic, payload);

    // pH tank level (raw)
    publisher_build_topic(topic, sizeof(topic), "ph/tank_level");
    snprintf(payload, sizeof(payload), "%d", ph->tank_level);
    PUBLISH(topic, payload);

    // pH tank level percent
    publisher_build_topic(topic, sizeof(topic), "ph/tank_level_percent");
    snprintf(payload, sizeof(payload), "%.1f", tank_level_percent(ph->tank_level));
    PUBLISH(topic, payload);

    // pH dose time
    publisher_build_topic(topic, sizeof(topic), "ph/dose_time");
    snprintf(payload, sizeof(payload), "%d", ph->dose_time);
    PUBLISH(topic, payload);

    // pH dose volume
    publisher_build_topic(topic, sizeof(topic), "ph/dose_volume");
    snprintf(payload, sizeof(payload), "%d", ph->dose_volume);
    PUBLISH(topic, payload);

    // pH dosing status
    publisher_build_topic(topic, sizeof(topic), "ph/dosing_status");
    PUBLISH(topic, dosing_status_str(ph->dosing_status));

    // pH is dosing
    publisher_build_topic(topic, sizeof(topic), "ph/is_dosing");
    PUBLISH(topic, ph->is_dosing ? "true" : "false");

    // pH dosing enabled (tank_level > 0)
    publisher_build_topic(topic, sizeof(topic), "ph/dosing_enabled");
    PUBLISH(topic, ph->tank_level > 0 ? "true" : "false");

    return ESP_OK;
}

esp_err_t publisher_publish_orp_state(esp_mqtt_client_handle_t client,
                                       const chemical_state_t *orp)
{
    char topic[128];
    char payload[32];

    // ORP level
    publisher_build_topic(topic, sizeof(topic), "orp/level");
    snprintf(payload, sizeof(payload), "%.0f", orp->level);
    PUBLISH(topic, payload);

    // ORP setpoint
    publisher_build_topic(topic, sizeof(topic), "orp/setpoint");
    snprintf(payload, sizeof(payload), "%.0f", orp->setpoint);
    PUBLISH(topic, payload);

    // ORP tank level (raw)
    publisher_build_topic(topic, sizeof(topic), "orp/tank_level");
    snprintf(payload, sizeof(payload), "%d", orp->tank_level);
    PUBLISH(topic, payload);

    // ORP tank level percent
    publisher_build_topic(topic, sizeof(topic), "orp/tank_level_percent");
    snprintf(payload, sizeof(payload), "%.1f", tank_level_percent(orp->tank_level));
    PUBLISH(topic, payload);

    // ORP dose time
    publisher_build_topic(topic, sizeof(topic), "orp/dose_time");
    snprintf(payload, sizeof(payload), "%d", orp->dose_time);
    PUBLISH(topic, payload);

    // ORP dose volume
    publisher_build_topic(topic, sizeof(topic), "orp/dose_volume");
    snprintf(payload, sizeof(payload), "%d", orp->dose_volume);
    PUBLISH(topic, payload);

    // ORP dosing status
    publisher_build_topic(topic, sizeof(topic), "orp/dosing_status");
    PUBLISH(topic, dosing_status_str(orp->dosing_status));

    // ORP is dosing
    publisher_build_topic(topic, sizeof(topic), "orp/is_dosing");
    PUBLISH(topic, orp->is_dosing ? "true" : "false");

    // ORP dosing enabled (tank_level > 0)
    publisher_build_topic(topic, sizeof(topic), "orp/dosing_enabled");
    PUBLISH(topic, orp->tank_level > 0 ? "true" : "false");

    return ESP_OK;
}

esp_err_t publisher_publish_chemistry_state(esp_mqtt_client_handle_t client,
                                             const intellichem_state_t *state)
{
    char topic[128];
    char payload[32];

    // LSI
    publisher_build_topic(topic, sizeof(topic), "lsi");
    snprintf(payload, sizeof(payload), "%.2f", state->lsi);
    PUBLISH(topic, payload);

    // Calcium hardness
    publisher_build_topic(topic, sizeof(topic), "calcium_hardness");
    snprintf(payload, sizeof(payload), "%d", state->calcium_hardness);
    PUBLISH(topic, payload);

    // Cyanuric acid
    publisher_build_topic(topic, sizeof(topic), "cyanuric_acid");
    snprintf(payload, sizeof(payload), "%d", state->cyanuric_acid);
    PUBLISH(topic, payload);

    // Alkalinity
    publisher_build_topic(topic, sizeof(topic), "alkalinity");
    snprintf(payload, sizeof(payload), "%d", state->alkalinity);
    PUBLISH(topic, payload);

    // Salt level
    publisher_build_topic(topic, sizeof(topic), "salt_level");
    snprintf(payload, sizeof(payload), "%d", state->salt_level);
    PUBLISH(topic, payload);

    // Temperature
    publisher_build_topic(topic, sizeof(topic), "temperature");
    snprintf(payload, sizeof(payload), "%d", state->temperature);
    PUBLISH(topic, payload);

    // Firmware
    publisher_build_topic(topic, sizeof(topic), "firmware");
    PUBLISH(topic, state->firmware);

    // Flow detected
    publisher_build_topic(topic, sizeof(topic), "flow_detected");
    PUBLISH(topic, state->flow_detected ? "true" : "false");

    // Comms lost
    publisher_build_topic(topic, sizeof(topic), "comms_lost");
    PUBLISH(topic, state->comms_lost ? "true" : "false");

    return ESP_OK;
}

esp_err_t publisher_publish_alarms(esp_mqtt_client_handle_t client,
                                    const alarms_t *alarms)
{
    char topic[128];

    // Flow alarm
    publisher_build_topic(topic, sizeof(topic), "alarms/flow");
    PUBLISH(topic, alarms->flow ? "true" : "false");

    // pH tank empty
    publisher_build_topic(topic, sizeof(topic), "alarms/ph_tank_empty");
    PUBLISH(topic, alarms->ph_tank_empty ? "true" : "false");

    // ORP tank empty
    publisher_build_topic(topic, sizeof(topic), "alarms/orp_tank_empty");
    PUBLISH(topic, alarms->orp_tank_empty ? "true" : "false");

    // Probe fault
    publisher_build_topic(topic, sizeof(topic), "alarms/probe_fault");
    PUBLISH(topic, alarms->probe_fault ? "true" : "false");

    // Any active
    publisher_build_topic(topic, sizeof(topic), "alarms/any_active");
    PUBLISH(topic, alarms_any_active(alarms) ? "true" : "false");

    return ESP_OK;
}

esp_err_t publisher_publish_warnings(esp_mqtt_client_handle_t client,
                                      const warnings_t *warnings)
{
    char topic[128];

    // pH lockout
    publisher_build_topic(topic, sizeof(topic), "warnings/ph_lockout");
    PUBLISH(topic, warnings->ph_lockout ? "true" : "false");

    // pH daily limit
    publisher_build_topic(topic, sizeof(topic), "warnings/ph_daily_limit");
    PUBLISH(topic, warnings->ph_daily_limit ? "true" : "false");

    // ORP daily limit
    publisher_build_topic(topic, sizeof(topic), "warnings/orp_daily_limit");
    PUBLISH(topic, warnings->orp_daily_limit ? "true" : "false");

    // Invalid setup
    publisher_build_topic(topic, sizeof(topic), "warnings/invalid_setup");
    PUBLISH(topic, warnings->invalid_setup ? "true" : "false");

    // Chlorinator comm error
    publisher_build_topic(topic, sizeof(topic), "warnings/chlorinator_comm_error");
    PUBLISH(topic, warnings->chlorinator_comm_error ? "true" : "false");

    // Water chemistry status
    publisher_build_topic(topic, sizeof(topic), "warnings/water_chemistry");
    PUBLISH(topic, water_chemistry_str(warnings->water_chemistry));

    // Any active
    publisher_build_topic(topic, sizeof(topic), "warnings/any_active");
    PUBLISH(topic, warnings_any_active(warnings) ? "true" : "false");

    return ESP_OK;
}

esp_err_t publisher_publish_availability(esp_mqtt_client_handle_t client, bool online)
{
    char topic[128];
    publisher_get_availability_topic(topic, sizeof(topic));

    ESP_LOGI(TAG, "Publishing availability: %s", online ? "online" : "offline");
    PUBLISH_RETAIN(topic, online ? "online" : "offline");

    return ESP_OK;
}

esp_err_t publisher_publish_comms_error(esp_mqtt_client_handle_t client)
{
    ESP_LOGW(TAG, "Publishing communication error state");

    char topic[128];

    publisher_build_topic(topic, sizeof(topic), "comms_lost");
    PUBLISH(topic, "true");

    publisher_build_topic(topic, sizeof(topic), "alarms/comms");
    PUBLISH(topic, "true");

    return ESP_OK;
}

esp_err_t publisher_publish_comms_restored(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "Publishing communication restored");

    char topic[128];

    publisher_build_topic(topic, sizeof(topic), "comms_lost");
    PUBLISH(topic, "false");

    publisher_build_topic(topic, sizeof(topic), "alarms/comms");
    PUBLISH(topic, "false");

    return ESP_OK;
}

// ============================================================================
// JSON Publishing
// ============================================================================

esp_err_t publisher_publish_json_state(esp_mqtt_client_handle_t client,
                                        const intellichem_state_t *state)
{
    // Build JSON payload
    // Note: In a production system, you'd use cJSON or similar library
    // For now, we'll use snprintf for simplicity
    char json[1024];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"ph\":{\"level\":%.2f,\"setpoint\":%.2f,\"tank_level\":%d,\"tank_level_percent\":%.1f,"
        "\"dose_time\":%d,\"dose_volume\":%d,\"dosing_status\":\"%s\",\"is_dosing\":%s},"
        "\"orp\":{\"level\":%.0f,\"setpoint\":%.0f,\"tank_level\":%d,\"tank_level_percent\":%.1f,"
        "\"dose_time\":%d,\"dose_volume\":%d,\"dosing_status\":\"%s\",\"is_dosing\":%s},"
        "\"lsi\":%.2f,\"calcium_hardness\":%d,\"cyanuric_acid\":%d,\"alkalinity\":%d,"
        "\"salt_level\":%d,\"temperature\":%d,\"firmware\":\"%s\","
        "\"flow_detected\":%s,\"comms_lost\":%s,"
        "\"alarms\":{\"flow\":%s,\"ph_tank_empty\":%s,\"orp_tank_empty\":%s,\"probe_fault\":%s},"
        "\"warnings\":{\"ph_lockout\":%s,\"ph_daily_limit\":%s,\"orp_daily_limit\":%s,"
        "\"invalid_setup\":%s,\"chlorinator_comm_error\":%s,\"water_chemistry\":\"%s\"}"
        "}",
        state->ph.level, state->ph.setpoint, state->ph.tank_level,
        tank_level_percent(state->ph.tank_level),
        state->ph.dose_time, state->ph.dose_volume,
        dosing_status_str(state->ph.dosing_status),
        state->ph.is_dosing ? "true" : "false",
        state->orp.level, state->orp.setpoint, state->orp.tank_level,
        tank_level_percent(state->orp.tank_level),
        state->orp.dose_time, state->orp.dose_volume,
        dosing_status_str(state->orp.dosing_status),
        state->orp.is_dosing ? "true" : "false",
        state->lsi, state->calcium_hardness, state->cyanuric_acid, state->alkalinity,
        state->salt_level, state->temperature, state->firmware,
        state->flow_detected ? "true" : "false",
        state->comms_lost ? "true" : "false",
        state->alarms.flow ? "true" : "false",
        state->alarms.ph_tank_empty ? "true" : "false",
        state->alarms.orp_tank_empty ? "true" : "false",
        state->alarms.probe_fault ? "true" : "false",
        state->warnings.ph_lockout ? "true" : "false",
        state->warnings.ph_daily_limit ? "true" : "false",
        state->warnings.orp_daily_limit ? "true" : "false",
        state->warnings.invalid_setup ? "true" : "false",
        state->warnings.chlorinator_comm_error ? "true" : "false",
        water_chemistry_str(state->warnings.water_chemistry)
    );

    if (len >= (int)sizeof(json)) {
        ESP_LOGW(TAG, "JSON payload truncated");
    }

    char topic[128];
    publisher_build_topic(topic, sizeof(topic), "status");

    ESP_LOGI(TAG, "MQTT PUB: %s = <JSON %d bytes>", topic, len);
    int msg_id = esp_mqtt_client_publish(client, topic, json, len, CONFIG_MQTT_QOS, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish JSON state");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t publisher_publish_diagnostics(esp_mqtt_client_handle_t client,
                                         uint32_t polls_sent,
                                         uint32_t responses_received,
                                         uint32_t serial_errors,
                                         uint32_t states_published,
                                         uint32_t mqtt_reconnections)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get uptime in seconds
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)(uptime_us / 1000000);

    // Get free heap
    uint32_t free_heap = esp_get_free_heap_size();

    // Build JSON payload
    char json[256];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"polls_sent\":%lu,"
        "\"responses_received\":%lu,"
        "\"serial_errors\":%lu,"
        "\"states_published\":%lu,"
        "\"mqtt_reconnections\":%lu,"
        "\"uptime_sec\":%lu,"
        "\"free_heap\":%lu,"
        "\"response_rate\":%.1f"
        "}",
        (unsigned long)polls_sent,
        (unsigned long)responses_received,
        (unsigned long)serial_errors,
        (unsigned long)states_published,
        (unsigned long)mqtt_reconnections,
        (unsigned long)uptime_sec,
        (unsigned long)free_heap,
        polls_sent > 0 ? (100.0f * responses_received / polls_sent) : 0.0f
    );

    char topic[128];
    publisher_build_topic(topic, sizeof(topic), "diagnostics");

    int msg_id = esp_mqtt_client_publish(client, topic, json, len, 0, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish diagnostics");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published diagnostics: polls=%lu resp=%lu pub=%lu",
             (unsigned long)polls_sent, (unsigned long)responses_received,
             (unsigned long)states_published);
    return ESP_OK;
}
