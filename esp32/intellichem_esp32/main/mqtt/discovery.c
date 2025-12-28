/**
 * @file discovery.c
 * @brief Home Assistant MQTT Discovery implementation
 */

#include "discovery.h"
#include "mqtt_task.h"
#include "publisher.h"
#include "../protocol/constants.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "discovery";

// ============================================================================
// Configuration Constants
// ============================================================================

// Setpoint limits (from protocol/constants.h)
#ifndef PH_SETPOINT_MIN
#define PH_SETPOINT_MIN     7.0f
#endif
#ifndef PH_SETPOINT_MAX
#define PH_SETPOINT_MAX     7.6f
#endif
#ifndef ORP_SETPOINT_MIN
#define ORP_SETPOINT_MIN    400
#endif
#ifndef ORP_SETPOINT_MAX
#define ORP_SETPOINT_MAX    800
#endif
#ifndef CALCIUM_HARDNESS_MIN
#define CALCIUM_HARDNESS_MIN    25
#endif
#ifndef CALCIUM_HARDNESS_MAX
#define CALCIUM_HARDNESS_MAX    800
#endif
#ifndef CYANURIC_ACID_MIN
#define CYANURIC_ACID_MIN   0
#endif
#ifndef CYANURIC_ACID_MAX
#define CYANURIC_ACID_MAX   210
#endif
#ifndef ALKALINITY_MIN
#define ALKALINITY_MIN      25
#endif
#ifndef ALKALINITY_MAX
#define ALKALINITY_MAX      800
#endif

// ============================================================================
// Topic Helpers
// ============================================================================

size_t discovery_build_topic(char *buf, size_t buf_size,
                              const char *component, const char *entity_id)
{
    return snprintf(buf, buf_size, "%s/%s/intellichem/%s/config",
                    CONFIG_MQTT_DISCOVERY_PREFIX, component, entity_id);
}

size_t discovery_get_device_info(char *buf, size_t buf_size)
{
    return snprintf(buf, buf_size,
        "\"device\":{"
        "\"identifiers\":[\"intellichem_%d\"],"
        "\"name\":\"IntelliChem\","
        "\"manufacturer\":\"Pentair\","
        "\"model\":\"IntelliChem\","
        "\"suggested_area\":\"Pool\""
        "}",
        CONFIG_INTELLICHEM_ADDRESS);
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * @brief Publish a single discovery config
 */
static esp_err_t publish_discovery(esp_mqtt_client_handle_t client,
                                   const char *topic, const char *payload)
{
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, CONFIG_MQTT_QOS, 1);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish discovery to %s", topic);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Published discovery: %s", topic);
    return ESP_OK;
}

/**
 * @brief Build base config common to all entities
 */
static int build_base_config(char *buf, size_t buf_size,
                              const char *name, const char *entity_id)
{
    char device_info[256];
    discovery_get_device_info(device_info, sizeof(device_info));

    char availability_topic[128];
    publisher_get_availability_topic(availability_topic, sizeof(availability_topic));

    return snprintf(buf, buf_size,
        "\"name\":\"%s\","
        "\"unique_id\":\"intellichem_%d_%s\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "%s",
        name,
        CONFIG_INTELLICHEM_ADDRESS,
        entity_id,
        availability_topic,
        device_info);
}

// ============================================================================
// Sensor Discovery
// ============================================================================

typedef struct {
    const char *name;
    const char *entity_id;
    const char *topic_path;
    const char *unit;
    const char *device_class;
    const char *state_class;
    const char *icon;
} sensor_config_t;

static const sensor_config_t SENSORS[] = {
    // pH sensors
    {"pH Level", "ph_level", "ph/level", "pH", NULL, "measurement", "mdi:ph"},
    {"pH Setpoint", "ph_setpoint", "ph/setpoint", "pH", NULL, NULL, "mdi:target"},
    {"pH Tank Level", "ph_tank_level", "ph/tank_level_percent", "%", NULL, NULL, "mdi:car-coolant-level"},
    {"pH Dose Time", "ph_dose_time", "ph/dose_time", "s", "duration", NULL, "mdi:timer"},
    {"pH Dose Volume", "ph_dose_volume", "ph/dose_volume", "mL", NULL, NULL, "mdi:beaker"},

    // ORP sensors
    {"ORP Level", "orp_level", "orp/level", "mV", "voltage", "measurement", "mdi:flash"},
    {"ORP Setpoint", "orp_setpoint", "orp/setpoint", "mV", "voltage", NULL, "mdi:target"},
    {"ORP Tank Level", "orp_tank_level", "orp/tank_level_percent", "%", NULL, NULL, "mdi:car-coolant-level"},
    {"ORP Dose Time", "orp_dose_time", "orp/dose_time", "s", "duration", NULL, "mdi:timer"},
    {"ORP Dose Volume", "orp_dose_volume", "orp/dose_volume", "mL", NULL, NULL, "mdi:beaker"},

    // Water chemistry
    {"Temperature", "temperature", "temperature", "°F", "temperature", "measurement", NULL},
    {"Saturation Index (LSI)", "lsi", "lsi", NULL, NULL, "measurement", "mdi:water-percent"},
    {"Calcium Hardness", "calcium_hardness", "calcium_hardness", "ppm", NULL, "measurement", "mdi:flask"},
    {"Cyanuric Acid", "cyanuric_acid", "cyanuric_acid", "ppm", NULL, "measurement", "mdi:flask"},
    {"Alkalinity", "alkalinity", "alkalinity", "ppm", NULL, "measurement", "mdi:flask"},
    {"Salt Level", "salt_level", "salt_level", "ppm", NULL, "measurement", "mdi:shaker"},
    {"Firmware", "firmware", "firmware", NULL, NULL, NULL, "mdi:chip"},
};

static const sensor_config_t TEXT_SENSORS[] = {
    {"pH Dosing Status", "ph_dosing_status", "ph/dosing_status", NULL, NULL, NULL, "mdi:information"},
    {"ORP Dosing Status", "orp_dosing_status", "orp/dosing_status", NULL, NULL, NULL, "mdi:information"},
    {"Water Chemistry", "water_chemistry", "warnings/water_chemistry", NULL, NULL, NULL, "mdi:water-alert"},
};

esp_err_t discovery_publish_sensors(esp_mqtt_client_handle_t client)
{
    char topic[256];
    char payload[768];
    char state_topic[128];
    char base_config[512];
    esp_err_t ret;

    ESP_LOGI(TAG, "Publishing %d sensor discovery configs",
             (int)(sizeof(SENSORS) / sizeof(SENSORS[0]) + sizeof(TEXT_SENSORS) / sizeof(TEXT_SENSORS[0])));

    // Regular sensors
    for (size_t i = 0; i < sizeof(SENSORS) / sizeof(SENSORS[0]); i++) {
        const sensor_config_t *s = &SENSORS[i];

        discovery_build_topic(topic, sizeof(topic), "sensor", s->entity_id);
        publisher_build_topic(state_topic, sizeof(state_topic), s->topic_path);
        build_base_config(base_config, sizeof(base_config), s->name, s->entity_id);

        int len = snprintf(payload, sizeof(payload),
            "{%s,\"state_topic\":\"%s\"",
            base_config, state_topic);

        if (s->unit) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"unit_of_measurement\":\"%s\"", s->unit);
        }
        if (s->device_class) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"device_class\":\"%s\"", s->device_class);
        }
        if (s->state_class) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"state_class\":\"%s\"", s->state_class);
        }
        if (s->icon) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"icon\":\"%s\"", s->icon);
        }

        snprintf(payload + len, sizeof(payload) - len, "}");

        ret = publish_discovery(client, topic, payload);
        if (ret != ESP_OK) return ret;
    }

    // Text sensors
    for (size_t i = 0; i < sizeof(TEXT_SENSORS) / sizeof(TEXT_SENSORS[0]); i++) {
        const sensor_config_t *s = &TEXT_SENSORS[i];

        discovery_build_topic(topic, sizeof(topic), "sensor", s->entity_id);
        publisher_build_topic(state_topic, sizeof(state_topic), s->topic_path);
        build_base_config(base_config, sizeof(base_config), s->name, s->entity_id);

        int len = snprintf(payload, sizeof(payload),
            "{%s,\"state_topic\":\"%s\"",
            base_config, state_topic);

        if (s->icon) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"icon\":\"%s\"", s->icon);
        }

        snprintf(payload + len, sizeof(payload) - len, "}");

        ret = publish_discovery(client, topic, payload);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

// ============================================================================
// Binary Sensor Discovery
// ============================================================================

typedef struct {
    const char *name;
    const char *entity_id;
    const char *topic_path;
    const char *device_class;
    const char *icon;
} binary_sensor_config_t;

static const binary_sensor_config_t BINARY_SENSORS[] = {
    {"Flow Detected", "flow_detected", "flow_detected", "running", "mdi:water"},
    {"Flow Alarm", "flow_alarm", "alarms/flow", "problem", NULL},
    {"pH Tank Empty", "ph_tank_empty", "alarms/ph_tank_empty", "problem", "mdi:car-coolant-level"},
    {"ORP Tank Empty", "orp_tank_empty", "alarms/orp_tank_empty", "problem", "mdi:car-coolant-level"},
    {"Probe Fault", "probe_fault", "alarms/probe_fault", "problem", NULL},
    {"Communication Lost", "comms_lost", "comms_lost", "connectivity", NULL},
    {"pH Lockout", "ph_lockout", "warnings/ph_lockout", "problem", NULL},
    {"pH Daily Limit", "ph_daily_limit", "warnings/ph_daily_limit", "problem", NULL},
    {"ORP Daily Limit", "orp_daily_limit", "warnings/orp_daily_limit", "problem", NULL},
    {"pH Dosing", "ph_dosing", "ph/is_dosing", "running", "mdi:water-pump"},
    {"ORP Dosing", "orp_dosing", "orp/is_dosing", "running", "mdi:water-pump"},
};

esp_err_t discovery_publish_binary_sensors(esp_mqtt_client_handle_t client)
{
    char topic[256];
    char payload[768];
    char state_topic[128];
    char base_config[512];
    esp_err_t ret;

    ESP_LOGI(TAG, "Publishing %d binary sensor discovery configs",
             (int)(sizeof(BINARY_SENSORS) / sizeof(BINARY_SENSORS[0])));

    for (size_t i = 0; i < sizeof(BINARY_SENSORS) / sizeof(BINARY_SENSORS[0]); i++) {
        const binary_sensor_config_t *s = &BINARY_SENSORS[i];

        discovery_build_topic(topic, sizeof(topic), "binary_sensor", s->entity_id);
        publisher_build_topic(state_topic, sizeof(state_topic), s->topic_path);
        build_base_config(base_config, sizeof(base_config), s->name, s->entity_id);

        int len = snprintf(payload, sizeof(payload),
            "{%s,\"state_topic\":\"%s\","
            "\"payload_on\":\"true\",\"payload_off\":\"false\"",
            base_config, state_topic);

        if (s->device_class) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"device_class\":\"%s\"", s->device_class);
        }
        if (s->icon) {
            len += snprintf(payload + len, sizeof(payload) - len,
                ",\"icon\":\"%s\"", s->icon);
        }

        snprintf(payload + len, sizeof(payload) - len, "}");

        ret = publish_discovery(client, topic, payload);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

// ============================================================================
// Number Entity Discovery (Control)
// ============================================================================

typedef struct {
    const char *name;
    const char *entity_id;
    const char *state_path;
    const char *command_name;
    float min;
    float max;
    float step;
    const char *unit;
    const char *icon;
    const char *mode;  // "slider" or "box"
} number_config_t;

static const number_config_t NUMBERS[] = {
    {"pH Setpoint Control", "ph_setpoint_control", "ph/setpoint", "ph_setpoint",
     PH_SETPOINT_MIN, PH_SETPOINT_MAX, 0.1f, "pH", "mdi:target", "slider"},
    {"ORP Setpoint Control", "orp_setpoint_control", "orp/setpoint", "orp_setpoint",
     ORP_SETPOINT_MIN, ORP_SETPOINT_MAX, 10.0f, "mV", "mdi:target", "slider"},
    {"Calcium Hardness Setting", "calcium_hardness_control", "calcium_hardness", "calcium_hardness",
     CALCIUM_HARDNESS_MIN, CALCIUM_HARDNESS_MAX, 25.0f, "ppm", "mdi:flask", "box"},
    {"Cyanuric Acid Setting", "cyanuric_acid_control", "cyanuric_acid", "cyanuric_acid",
     CYANURIC_ACID_MIN, CYANURIC_ACID_MAX, 10.0f, "ppm", "mdi:flask", "box"},
    {"Alkalinity Setting", "alkalinity_control", "alkalinity", "alkalinity",
     ALKALINITY_MIN, ALKALINITY_MAX, 10.0f, "ppm", "mdi:flask", "box"},
};

esp_err_t discovery_publish_number_entities(esp_mqtt_client_handle_t client)
{
    char topic[256];
    char payload[1024];
    char state_topic[128];
    char command_topic[128];
    char base_config[512];
    esp_err_t ret;

    ESP_LOGI(TAG, "Publishing %d number entity discovery configs",
             (int)(sizeof(NUMBERS) / sizeof(NUMBERS[0])));

    for (size_t i = 0; i < sizeof(NUMBERS) / sizeof(NUMBERS[0]); i++) {
        const number_config_t *n = &NUMBERS[i];

        discovery_build_topic(topic, sizeof(topic), "number", n->entity_id);
        publisher_build_topic(state_topic, sizeof(state_topic), n->state_path);
        publisher_build_command_topic(command_topic, sizeof(command_topic), n->command_name);
        build_base_config(base_config, sizeof(base_config), n->name, n->entity_id);

        snprintf(payload, sizeof(payload),
            "{%s,"
            "\"state_topic\":\"%s\","
            "\"command_topic\":\"%s\","
            "\"min\":%.1f,\"max\":%.1f,\"step\":%.1f,"
            "\"unit_of_measurement\":\"%s\","
            "\"icon\":\"%s\","
            "\"mode\":\"%s\""
            "}",
            base_config,
            state_topic, command_topic,
            n->min, n->max, n->step,
            n->unit, n->icon, n->mode);

        ret = publish_discovery(client, topic, payload);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

// ============================================================================
// Switch Entity Discovery (Control)
// ============================================================================

typedef struct {
    const char *name;
    const char *entity_id;
    const char *state_path;
    const char *command_name;
    const char *icon;
} switch_config_t;

static const switch_config_t SWITCHES[] = {
    {"pH Dosing Enable", "ph_dosing_enable", "ph/dosing_enabled", "ph_dosing", "mdi:flask-outline"},
    {"ORP Dosing Enable", "orp_dosing_enable", "orp/dosing_enabled", "orp_dosing", "mdi:flask-outline"},
};

esp_err_t discovery_publish_switch_entities(esp_mqtt_client_handle_t client)
{
    char topic[256];
    char payload[1024];
    char state_topic[128];
    char command_topic[128];
    char base_config[512];
    esp_err_t ret;

    ESP_LOGI(TAG, "Publishing %d switch entity discovery configs",
             (int)(sizeof(SWITCHES) / sizeof(SWITCHES[0])));

    for (size_t i = 0; i < sizeof(SWITCHES) / sizeof(SWITCHES[0]); i++) {
        const switch_config_t *s = &SWITCHES[i];

        discovery_build_topic(topic, sizeof(topic), "switch", s->entity_id);
        publisher_build_topic(state_topic, sizeof(state_topic), s->state_path);
        publisher_build_command_topic(command_topic, sizeof(command_topic), s->command_name);
        build_base_config(base_config, sizeof(base_config), s->name, s->entity_id);

        snprintf(payload, sizeof(payload),
            "{%s,"
            "\"state_topic\":\"%s\","
            "\"command_topic\":\"%s\","
            "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
            "\"state_on\":\"true\",\"state_off\":\"false\","
            "\"icon\":\"%s\""
            "}",
            base_config,
            state_topic, command_topic,
            s->icon);

        ret = publish_discovery(client, topic, payload);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

// ============================================================================
// Main Discovery Functions
// ============================================================================

esp_err_t discovery_publish_all(esp_mqtt_client_handle_t client, bool control_enabled)
{
    ESP_LOGI(TAG, "Publishing Home Assistant discovery configs (control=%s)",
             control_enabled ? "enabled" : "disabled");

    esp_err_t ret;

    // Sensors
    ret = discovery_publish_sensors(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish sensor discovery");
        return ret;
    }

    // Binary sensors
    ret = discovery_publish_binary_sensors(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish binary sensor discovery");
        return ret;
    }

    // Control entities (if enabled)
    if (control_enabled) {
        ret = discovery_publish_number_entities(client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to publish number entity discovery");
            return ret;
        }

        ret = discovery_publish_switch_entities(client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to publish switch entity discovery");
            return ret;
        }

        ESP_LOGI(TAG, "Control entities published");
    }

    ESP_LOGI(TAG, "All discovery configs published successfully");
    return ESP_OK;
}

esp_err_t discovery_remove_all(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "Removing all discovery configs");

    char topic[256];

    // Remove sensors
    for (size_t i = 0; i < sizeof(SENSORS) / sizeof(SENSORS[0]); i++) {
        discovery_build_topic(topic, sizeof(topic), "sensor", SENSORS[i].entity_id);
        esp_mqtt_client_publish(client, topic, "", 0, CONFIG_MQTT_QOS, 1);
    }
    for (size_t i = 0; i < sizeof(TEXT_SENSORS) / sizeof(TEXT_SENSORS[0]); i++) {
        discovery_build_topic(topic, sizeof(topic), "sensor", TEXT_SENSORS[i].entity_id);
        esp_mqtt_client_publish(client, topic, "", 0, CONFIG_MQTT_QOS, 1);
    }

    // Remove binary sensors
    for (size_t i = 0; i < sizeof(BINARY_SENSORS) / sizeof(BINARY_SENSORS[0]); i++) {
        discovery_build_topic(topic, sizeof(topic), "binary_sensor", BINARY_SENSORS[i].entity_id);
        esp_mqtt_client_publish(client, topic, "", 0, CONFIG_MQTT_QOS, 1);
    }

    // Remove number entities
    for (size_t i = 0; i < sizeof(NUMBERS) / sizeof(NUMBERS[0]); i++) {
        discovery_build_topic(topic, sizeof(topic), "number", NUMBERS[i].entity_id);
        esp_mqtt_client_publish(client, topic, "", 0, CONFIG_MQTT_QOS, 1);
    }

    // Remove switch entities
    for (size_t i = 0; i < sizeof(SWITCHES) / sizeof(SWITCHES[0]); i++) {
        discovery_build_topic(topic, sizeof(topic), "switch", SWITCHES[i].entity_id);
        esp_mqtt_client_publish(client, topic, "", 0, CONFIG_MQTT_QOS, 1);
    }

    ESP_LOGI(TAG, "All discovery configs removed");
    return ESP_OK;
}
