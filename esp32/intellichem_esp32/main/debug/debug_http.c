/**
 * @file debug_http.c
 * @brief HTTP debug endpoint implementation
 */

#include "debug_http.h"
#include "debug_log.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

// Import stats functions from other modules
#include "serial/serial_task.h"
#include "mqtt/mqtt_task.h"

static const char *TAG = "debug_http";

// ============================================================================
// Configuration
// ============================================================================

#ifndef CONFIG_DEBUG_HTTP_PORT
#define CONFIG_DEBUG_HTTP_PORT 80
#endif

#ifndef CONFIG_DEBUG_LOG_HTTP_BUF_SIZE
#define CONFIG_DEBUG_LOG_HTTP_BUF_SIZE 8192
#endif

// ============================================================================
// State
// ============================================================================

static httpd_handle_t s_server = NULL;
static bool s_owns_server = false;  // True if we created the server

// ============================================================================
// HTTP Handlers
// ============================================================================

/**
 * @brief GET /debug/stats - Return system statistics as JSON
 */
static esp_err_t stats_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /debug/stats");

    // Gather statistics
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)(uptime_us / 1000000);

    // WiFi info
    int rssi = 0;
    char ssid[33] = "N/A";
    uint8_t channel = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
        strncpy(ssid, (const char *)ap_info.ssid, sizeof(ssid) - 1);
        channel = ap_info.primary;
    }

    // Serial task stats
    uint32_t polls = 0, responses = 0, errors = 0;
    serial_task_get_stats(&polls, &responses, &errors);

    // MQTT task stats
    uint32_t states_published = 0;
    bool discovery_sent = false;
    uint32_t reconnections = 0;
    mqtt_task_get_stats(&states_published, &discovery_sent, &reconnections);
    const char *mqtt_status = mqtt_task_status_str(mqtt_task_get_status());

    // Log buffer stats
    debug_log_stats_t log_stats = {0};
    debug_log_get_stats(&log_stats);

    // Build JSON response
    char json[768];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"uptime_sec\":%lu,"
        "\"free_heap\":%lu,"
        "\"min_free_heap\":%lu,"
        "\"wifi\":{"
            "\"rssi\":%d,"
            "\"ssid\":\"%s\","
            "\"channel\":%u"
        "},"
        "\"serial\":{"
            "\"polls_sent\":%lu,"
            "\"responses_received\":%lu,"
            "\"errors\":%lu"
        "},"
        "\"mqtt\":{"
            "\"status\":\"%s\","
            "\"states_published\":%lu,"
            "\"discovery_sent\":%s,"
            "\"reconnections\":%lu"
        "},"
        "\"log_buffer\":{"
            "\"capacity\":%u,"
            "\"count\":%u,"
            "\"dropped\":%lu,"
            "\"total_captured\":%lu"
        "}"
        "}",
        (unsigned long)uptime_sec,
        (unsigned long)free_heap,
        (unsigned long)min_free_heap,
        rssi,
        ssid,
        (unsigned)channel,
        (unsigned long)polls,
        (unsigned long)responses,
        (unsigned long)errors,
        mqtt_status,
        (unsigned long)states_published,
        discovery_sent ? "true" : "false",
        (unsigned long)reconnections,
        (unsigned)log_stats.capacity,
        (unsigned)log_stats.count,
        (unsigned long)log_stats.dropped_count,
        (unsigned long)log_stats.total_captured
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, len);

    return ESP_OK;
}

/**
 * @brief GET /debug/logs - Return captured logs
 *
 * Query params:
 *   format=json  - Return as JSON array (default: plain text)
 */
static esp_err_t logs_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /debug/logs");

    // Check for format query parameter
    char query[32] = "";
    char format[16] = "plain";

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "format", format, sizeof(format));
    }

    bool use_json = (strcmp(format, "json") == 0);

    // Allocate response buffer
    char *buf = malloc(CONFIG_DEBUG_LOG_HTTP_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Read logs in requested format
    size_t len;
    if (use_json) {
        len = debug_log_read_json(buf, CONFIG_DEBUG_LOG_HTTP_BUF_SIZE);
        httpd_resp_set_type(req, "application/json");
    } else {
        len = debug_log_read_plain(buf, CONFIG_DEBUG_LOG_HTTP_BUF_SIZE);
        httpd_resp_set_type(req, "text/plain");
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, len);

    free(buf);
    return ESP_OK;
}

/**
 * @brief POST /debug/logs/clear - Clear the log buffer
 */
static esp_err_t logs_clear_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /debug/logs/clear");

    debug_log_clear();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"status\":\"cleared\"}", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/**
 * @brief POST /reboot - Trigger system reboot
 */
static esp_err_t reboot_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "POST /reboot - System reboot requested via HTTP");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}", HTTPD_RESP_USE_STRLEN);

    // Delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_restart();

    return ESP_OK;  // Never reached
}

/**
 * @brief GET /debug/heap - Detailed heap info
 */
static esp_err_t heap_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /debug/heap");

    char json[256];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"free_heap\":%lu,"
        "\"min_free_heap\":%lu,"
        "\"largest_free_block\":%lu"
        "}",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, len);

    return ESP_OK;
}

// ============================================================================
// URI Handler Definitions
// ============================================================================

static const httpd_uri_t uri_stats = {
    .uri = "/debug/stats",
    .method = HTTP_GET,
    .handler = stats_get_handler,
};

static const httpd_uri_t uri_logs = {
    .uri = "/debug/logs",
    .method = HTTP_GET,
    .handler = logs_get_handler,
};

static const httpd_uri_t uri_logs_clear = {
    .uri = "/debug/logs/clear",
    .method = HTTP_POST,
    .handler = logs_clear_handler,
};

static const httpd_uri_t uri_heap = {
    .uri = "/debug/heap",
    .method = HTTP_GET,
    .handler = heap_get_handler,
};

static const httpd_uri_t uri_reboot = {
    .uri = "/reboot",
    .method = HTTP_POST,
    .handler = reboot_handler,
};

// ============================================================================
// Public API
// ============================================================================

esp_err_t debug_http_start(httpd_handle_t existing_server)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "Debug HTTP server already running");
        return ESP_OK;
    }

    if (existing_server != NULL) {
        // Use existing server, just register our handlers
        s_server = existing_server;
        s_owns_server = false;
        ESP_LOGI(TAG, "Registering debug handlers on existing HTTP server");
    } else {
        // Create our own server
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = CONFIG_DEBUG_HTTP_PORT;
        config.max_uri_handlers = 8;
        config.max_open_sockets = 3;
        config.lru_purge_enable = true;

        esp_err_t ret = httpd_start(&s_server, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
            return ret;
        }

        s_owns_server = true;
        ESP_LOGI(TAG, "Started debug HTTP server on port %d", config.server_port);
    }

    // Register handlers
    esp_err_t ret;

    ret = httpd_register_uri_handler(s_server, &uri_stats);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register /debug/stats: %s", esp_err_to_name(ret));
    }

    ret = httpd_register_uri_handler(s_server, &uri_logs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register /debug/logs: %s", esp_err_to_name(ret));
    }

    ret = httpd_register_uri_handler(s_server, &uri_logs_clear);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register /debug/logs/clear: %s", esp_err_to_name(ret));
    }

    ret = httpd_register_uri_handler(s_server, &uri_heap);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register /debug/heap: %s", esp_err_to_name(ret));
    }

    ret = httpd_register_uri_handler(s_server, &uri_reboot);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register /reboot: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Debug HTTP endpoints registered:");
    ESP_LOGI(TAG, "  GET  /debug/stats      - System statistics");
    ESP_LOGI(TAG, "  GET  /debug/logs       - Captured logs (?format=json)");
    ESP_LOGI(TAG, "  POST /debug/logs/clear - Clear log buffer");
    ESP_LOGI(TAG, "  GET  /debug/heap       - Heap info");
    ESP_LOGI(TAG, "  POST /reboot           - Reboot device");

    return ESP_OK;
}

void debug_http_stop(void)
{
    if (s_server == NULL) {
        return;
    }

    // Unregister our handlers
    httpd_unregister_uri(s_server, "/debug/stats");
    httpd_unregister_uri(s_server, "/debug/logs");
    httpd_unregister_uri(s_server, "/debug/logs/clear");
    httpd_unregister_uri(s_server, "/debug/heap");
    httpd_unregister_uri(s_server, "/reboot");

    // Only stop server if we created it
    if (s_owns_server) {
        httpd_stop(s_server);
        ESP_LOGI(TAG, "Debug HTTP server stopped");
    }

    s_server = NULL;
    s_owns_server = false;
}

bool debug_http_is_running(void)
{
    return s_server != NULL;
}

httpd_handle_t debug_http_get_handle(void)
{
    return s_server;
}
