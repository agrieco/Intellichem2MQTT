/**
 * @file mqtt_task.c
 * @brief MQTT task implementation for MQTT client management
 *
 * WiFi provisioning is handled by wifi_prov module.
 */

#include "mqtt_task.h"
#include "publisher.h"
#include "discovery.h"
#include "../serial/serial_task.h"
#include "../protocol/commands.h"
#include "../wifi/wifi_prov.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "mqtt";

// ============================================================================
// Forward Declarations
// ============================================================================

static bool parse_mqtt_command(const char *topic, int topic_len,
                               const char *data, int data_len,
                               serial_command_t *cmd);

// ============================================================================
// Configuration
// ============================================================================

#define MQTT_TASK_STACK_SIZE    8192
#define MQTT_TASK_PRIORITY      4
#define STATE_QUEUE_TIMEOUT_MS  1000

// ============================================================================
// State
// ============================================================================

static TaskHandle_t s_task_handle = NULL;
static QueueHandle_t s_state_queue = NULL;
static QueueHandle_t s_command_queue = NULL;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static volatile bool s_running = false;
static volatile mqtt_connection_status_t s_status = MQTT_CONN_DISCONNECTED;
static volatile bool s_mqtt_connected = false;
static volatile bool s_discovery_sent = false;

// Statistics
static uint32_t s_states_published = 0;
static uint32_t s_reconnections = 0;

// Runtime MQTT config (from web provisioning or Kconfig defaults)
static char s_broker_uri[128] = {0};
static char s_username[64] = {0};
static char s_password[64] = {0};
static char s_topic_prefix[64] = {0};

// ============================================================================
// Command Parsing
// ============================================================================

/**
 * @brief Parse MQTT command message into serial_command_t
 *
 * Topic format: <prefix>/intellichem/set/<command>
 * Commands: ph_setpoint, orp_setpoint, ph_dosing_enabled, orp_dosing_enabled,
 *           calcium_hardness, cyanuric_acid, alkalinity
 *
 * @param topic MQTT topic (not null-terminated)
 * @param topic_len Length of topic
 * @param data MQTT payload (not null-terminated)
 * @param data_len Length of payload
 * @param cmd Output command structure
 * @return true if command parsed successfully
 */
static bool parse_mqtt_command(const char *topic, int topic_len,
                               const char *data, int data_len,
                               serial_command_t *cmd)
{
    if (!topic || !data || !cmd || topic_len <= 0 || data_len <= 0) {
        ESP_LOGW(TAG, "Invalid command parameters");
        return false;
    }

    // Null-terminate the topic for parsing
    char topic_buf[128];
    if (topic_len >= (int)sizeof(topic_buf)) {
        ESP_LOGW(TAG, "Topic too long: %d", topic_len);
        return false;
    }
    memcpy(topic_buf, topic, topic_len);
    topic_buf[topic_len] = '\0';

    // Null-terminate the data for parsing
    char data_buf[32];
    if (data_len >= (int)sizeof(data_buf)) {
        ESP_LOGW(TAG, "Data too long: %d", data_len);
        return false;
    }
    memcpy(data_buf, data, data_len);
    data_buf[data_len] = '\0';

    ESP_LOGD(TAG, "Parsing command: topic='%s' data='%s'", topic_buf, data_buf);

    // Find the command name (after last '/')
    const char *cmd_name = strrchr(topic_buf, '/');
    if (!cmd_name) {
        ESP_LOGW(TAG, "No command name in topic");
        return false;
    }
    cmd_name++;  // Skip the '/'

    ESP_LOGI(TAG, "Command: %s = %s", cmd_name, data_buf);

    // Parse command type and value
    if (strcmp(cmd_name, "ph_setpoint") == 0) {
        cmd->type = CMD_TYPE_SET_PH_SETPOINT;
        cmd->value.ph_setpoint = strtof(data_buf, NULL);
        if (!command_validate_ph_setpoint(cmd->value.ph_setpoint)) {
            ESP_LOGW(TAG, "Invalid pH setpoint: %.2f (valid: 7.0-7.6)",
                     cmd->value.ph_setpoint);
            return false;
        }
        ESP_LOGI(TAG, "Parsed pH setpoint command: %.2f", cmd->value.ph_setpoint);
        return true;
    }
    else if (strcmp(cmd_name, "orp_setpoint") == 0) {
        cmd->type = CMD_TYPE_SET_ORP_SETPOINT;
        cmd->value.orp_setpoint = (uint16_t)atoi(data_buf);
        if (!command_validate_orp_setpoint(cmd->value.orp_setpoint)) {
            ESP_LOGW(TAG, "Invalid ORP setpoint: %d (valid: 400-800 mV)",
                     cmd->value.orp_setpoint);
            return false;
        }
        ESP_LOGI(TAG, "Parsed ORP setpoint command: %d mV", cmd->value.orp_setpoint);
        return true;
    }
    else if (strcmp(cmd_name, "ph_dosing_enabled") == 0) {
        cmd->type = CMD_TYPE_SET_PH_DOSING_ENABLED;
        cmd->value.dosing_enabled = (strcasecmp(data_buf, "ON") == 0 ||
                                     strcasecmp(data_buf, "1") == 0 ||
                                     strcasecmp(data_buf, "true") == 0);
        ESP_LOGI(TAG, "Parsed pH dosing command: %s",
                 cmd->value.dosing_enabled ? "enabled" : "disabled");
        return true;
    }
    else if (strcmp(cmd_name, "orp_dosing_enabled") == 0) {
        cmd->type = CMD_TYPE_SET_ORP_DOSING_ENABLED;
        cmd->value.dosing_enabled = (strcasecmp(data_buf, "ON") == 0 ||
                                     strcasecmp(data_buf, "1") == 0 ||
                                     strcasecmp(data_buf, "true") == 0);
        ESP_LOGI(TAG, "Parsed ORP dosing command: %s",
                 cmd->value.dosing_enabled ? "enabled" : "disabled");
        return true;
    }
    else if (strcmp(cmd_name, "calcium_hardness") == 0) {
        cmd->type = CMD_TYPE_SET_CALCIUM_HARDNESS;
        cmd->value.calcium_hardness = (uint16_t)atoi(data_buf);
        if (!command_validate_calcium_hardness(cmd->value.calcium_hardness)) {
            ESP_LOGW(TAG, "Invalid calcium hardness: %d (valid: 25-800 ppm)",
                     cmd->value.calcium_hardness);
            return false;
        }
        ESP_LOGI(TAG, "Parsed calcium hardness command: %d ppm",
                 cmd->value.calcium_hardness);
        return true;
    }
    else if (strcmp(cmd_name, "cyanuric_acid") == 0) {
        cmd->type = CMD_TYPE_SET_CYANURIC_ACID;
        cmd->value.cyanuric_acid = (uint8_t)atoi(data_buf);
        if (!command_validate_cyanuric_acid(cmd->value.cyanuric_acid)) {
            ESP_LOGW(TAG, "Invalid cyanuric acid: %d (valid: 0-210 ppm)",
                     cmd->value.cyanuric_acid);
            return false;
        }
        ESP_LOGI(TAG, "Parsed cyanuric acid command: %d ppm",
                 cmd->value.cyanuric_acid);
        return true;
    }
    else if (strcmp(cmd_name, "alkalinity") == 0) {
        cmd->type = CMD_TYPE_SET_ALKALINITY;
        cmd->value.alkalinity = (uint16_t)atoi(data_buf);
        if (!command_validate_alkalinity(cmd->value.alkalinity)) {
            ESP_LOGW(TAG, "Invalid alkalinity: %d (valid: 25-800 ppm)",
                     cmd->value.alkalinity);
            return false;
        }
        ESP_LOGI(TAG, "Parsed alkalinity command: %d ppm", cmd->value.alkalinity);
        return true;
    }
    else {
        ESP_LOGW(TAG, "Unknown command: %s", cmd_name);
        return false;
    }
}

// ============================================================================
// MQTT Event Handler
// ============================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            s_mqtt_connected = true;
            s_status = MQTT_CONN_MQTT_CONNECTED;

            // Publish availability
            publisher_publish_availability(s_mqtt_client, true);

            // Publish discovery configs if not sent yet
            if (!s_discovery_sent) {
                ESP_LOGI(TAG, "Publishing Home Assistant discovery configs...");
                discovery_publish_all(s_mqtt_client, CONFIG_CONTROL_ENABLED);
                s_discovery_sent = true;
                ESP_LOGI(TAG, "Discovery configs published");
            }

            // Subscribe to command topics if control enabled
            if (CONFIG_CONTROL_ENABLED) {
                char topic[128];
                snprintf(topic, sizeof(topic), "%s/intellichem/set/#", CONFIG_MQTT_TOPIC_PREFIX);
                esp_mqtt_client_subscribe(s_mqtt_client, topic, CONFIG_MQTT_QOS);
                ESP_LOGI(TAG, "Subscribed to command topics: %s", topic);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            s_status = MQTT_CONN_WIFI_CONNECTED;
            s_reconnections++;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received on topic: %.*s",
                     event->topic_len, event->topic);
            ESP_LOGD(TAG, "Data: %.*s", event->data_len, event->data);

            // Parse and queue command to serial task
            if (CONFIG_CONTROL_ENABLED && s_command_queue != NULL) {
                serial_command_t cmd;
                memset(&cmd, 0, sizeof(cmd));

                if (parse_mqtt_command(event->topic, event->topic_len,
                                        event->data, event->data_len, &cmd)) {
                    // Queue command to serial task
                    if (xQueueSend(s_command_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
                        ESP_LOGI(TAG, "Command queued to serial task (type=%d)", cmd.type);
                    } else {
                        ESP_LOGW(TAG, "Command queue full, dropping command");
                    }
                }
            } else {
                ESP_LOGD(TAG, "Control disabled or no command queue");
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Transport error: %s",
                         strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGD(TAG, "MQTT event: %d", (int)event_id);
            break;
    }
}

// ============================================================================
// MQTT Initialization
// ============================================================================

static esp_err_t mqtt_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT client...");

    // Try to get MQTT config from web provisioning
    mqtt_config_t web_config;
    if (wifi_prov_get_mqtt_config(&web_config)) {
        // Use web provisioning config
        strncpy(s_broker_uri, web_config.broker_uri, sizeof(s_broker_uri) - 1);
        strncpy(s_username, web_config.username, sizeof(s_username) - 1);
        strncpy(s_password, web_config.password, sizeof(s_password) - 1);
        strncpy(s_topic_prefix, web_config.topic_prefix, sizeof(s_topic_prefix) - 1);
        ESP_LOGI(TAG, "Using MQTT config from web provisioning");
    } else {
        // Fall back to Kconfig defaults
        strncpy(s_broker_uri, CONFIG_MQTT_BROKER_URI, sizeof(s_broker_uri) - 1);
        strncpy(s_username, CONFIG_MQTT_USERNAME, sizeof(s_username) - 1);
        strncpy(s_password, CONFIG_MQTT_PASSWORD, sizeof(s_password) - 1);
        strncpy(s_topic_prefix, CONFIG_MQTT_TOPIC_PREFIX, sizeof(s_topic_prefix) - 1);
        ESP_LOGI(TAG, "Using MQTT config from Kconfig defaults");
    }

    // Build LWT topic
    char lwt_topic[128];
    publisher_get_availability_topic(lwt_topic, sizeof(lwt_topic));

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_broker_uri,
        .credentials.username = s_username,
        .credentials.authentication.password = s_password,
        .session.keepalive = CONFIG_MQTT_KEEPALIVE,
        .session.last_will = {
            .topic = lwt_topic,
            .msg = "offline",
            .msg_len = 7,
            .qos = CONFIG_MQTT_QOS,
            .retain = true,
        },
        .network.reconnect_timeout_ms = 5000,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_err_t ret = esp_mqtt_client_register_event(
        s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }

    // Start MQTT client
    s_status = MQTT_CONN_MQTT_CONNECTING;
    ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client");
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client started, broker: %s", s_broker_uri);
    return ESP_OK;
}

// ============================================================================
// Main Task Function
// ============================================================================

static void mqtt_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT task started");

    // Initialize WiFi provisioning
    esp_err_t ret = wifi_prov_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi provisioning init failed: %s", esp_err_to_name(ret));
        s_status = MQTT_CONN_ERROR;
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Start WiFi provisioning (blocks until connected)
    s_status = MQTT_CONN_WIFI_CONNECTING;
    ret = wifi_prov_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed: %s", esp_err_to_name(ret));
        s_status = MQTT_CONN_ERROR;
        s_running = false;
        vTaskDelete(NULL);
        return;
    }
    s_status = MQTT_CONN_WIFI_CONNECTED;
    ESP_LOGI(TAG, "WiFi connected, starting MQTT...");

    // Initialize MQTT
    ret = mqtt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT initialization failed: %s", esp_err_to_name(ret));
        s_status = MQTT_CONN_ERROR;
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Main loop - receive state updates and publish
    intellichem_state_t state;
    TickType_t last_publish_time = 0;
    TickType_t last_diagnostics_time = 0;
    const TickType_t diagnostics_interval = pdMS_TO_TICKS(60000);  // Every 60 seconds

    while (s_running) {
        // Wait for state update from serial task
        if (xQueueReceive(s_state_queue, &state, pdMS_TO_TICKS(STATE_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            if (s_mqtt_connected) {
                ESP_LOGI(TAG, "Publishing state: pH=%.2f ORP=%.0fmV temp=%d°F",
                         state.ph.level, state.orp.level, state.temperature);

                ret = publisher_publish_state(s_mqtt_client, &state);
                if (ret == ESP_OK) {
                    s_states_published++;
                    last_publish_time = xTaskGetTickCount();
                } else {
                    ESP_LOGE(TAG, "Failed to publish state: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGW(TAG, "State received but MQTT not connected (connected=%d)",
                         s_mqtt_connected);
            }
        }

        // Publish diagnostics periodically for remote debugging
        if (s_mqtt_connected) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_diagnostics_time) >= diagnostics_interval) {
                uint32_t polls, responses, errors;
                serial_task_get_stats(&polls, &responses, &errors);

                publisher_publish_diagnostics(s_mqtt_client,
                                               polls, responses, errors,
                                               s_states_published, s_reconnections);
                last_diagnostics_time = now;
            }

            // Check for stale connection
            if (last_publish_time > 0 &&
                (now - last_publish_time) > pdMS_TO_TICKS(300000)) {  // 5 minutes
                ESP_LOGW(TAG, "No state published for 5 minutes, checking connection...");
                // The MQTT client handles reconnection automatically
            }
        }
    }

    // Cleanup
    if (s_mqtt_client) {
        publisher_publish_availability(s_mqtt_client, false);
        vTaskDelay(pdMS_TO_TICKS(100));  // Allow message to be sent
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }

    // Note: WiFi cleanup is handled by wifi_prov module

    ESP_LOGI(TAG, "MQTT task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t mqtt_task_start(QueueHandle_t state_queue, QueueHandle_t command_queue)
{
    if (s_running) {
        ESP_LOGW(TAG, "MQTT task already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_state_queue = state_queue;
    s_command_queue = command_queue;

    // Reset state
    s_status = MQTT_CONN_DISCONNECTED;
    s_mqtt_connected = false;
    s_discovery_sent = false;
    s_states_published = 0;
    s_reconnections = 0;

    s_running = true;

    // Create task
    BaseType_t ret = xTaskCreate(mqtt_task,
                                 "mqtt_task",
                                 MQTT_TASK_STACK_SIZE,
                                 NULL,
                                 MQTT_TASK_PRIORITY,
                                 &s_task_handle);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MQTT task started successfully");
    return ESP_OK;
}

void mqtt_task_stop(void)
{
    if (!s_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping MQTT task...");
    s_running = false;

    // Wait for task to exit
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Note: WiFi event group is managed by wifi_prov module

    s_task_handle = NULL;
}

bool mqtt_task_is_running(void)
{
    return s_running;
}

mqtt_connection_status_t mqtt_task_get_status(void)
{
    return s_status;
}

const char* mqtt_task_status_str(mqtt_connection_status_t status)
{
    switch (status) {
        case MQTT_CONN_DISCONNECTED:     return "Disconnected";
        case MQTT_CONN_WIFI_CONNECTING:  return "WiFi Connecting";
        case MQTT_CONN_WIFI_CONNECTED:   return "WiFi Connected";
        case MQTT_CONN_MQTT_CONNECTING:  return "MQTT Connecting";
        case MQTT_CONN_MQTT_CONNECTED:   return "MQTT Connected";
        case MQTT_CONN_ERROR:            return "Error";
        default:                         return "Unknown";
    }
}

bool mqtt_task_is_connected(void)
{
    return s_mqtt_connected;
}

void mqtt_task_get_stats(uint32_t *states_published, bool *discovery_sent, uint32_t *reconnections)
{
    if (states_published) *states_published = s_states_published;
    if (discovery_sent) *discovery_sent = s_discovery_sent;
    if (reconnections) *reconnections = s_reconnections;
}

const char* mqtt_task_get_topic_prefix(void)
{
    // Return the runtime topic prefix (from web config or Kconfig default)
    // If not yet initialized, return Kconfig default
    if (s_topic_prefix[0] == '\0') {
        return CONFIG_MQTT_TOPIC_PREFIX;
    }
    return s_topic_prefix;
}

esp_err_t mqtt_task_republish_discovery(void)
{
    if (!s_mqtt_connected || !s_mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Republishing Home Assistant discovery configs...");
    esp_err_t ret = discovery_publish_all(s_mqtt_client, CONFIG_CONTROL_ENABLED);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Discovery configs republished");
    }
    return ret;
}
