/**
 * @file wifi_prov.c
 * @brief Captive portal WiFi provisioning
 *
 * Creates an open AP with captive portal for WiFi configuration.
 * When you connect, your phone/computer automatically opens the setup page.
 */

#include "wifi_prov.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/gpio.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

static const char *TAG = "wifi_prov";

// ============================================================================
// Configuration
// ============================================================================

#define SETUP_AP_SSID       "IntelliChem-Setup"
#define SETUP_AP_CHANNEL    6
#define SETUP_AP_MAX_CONN   4

#define NVS_NAMESPACE       "wifi_creds"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASS        "password"

#define NVS_MQTT_NAMESPACE  "mqtt_config"
#define NVS_MQTT_BROKER     "broker_uri"
#define NVS_MQTT_USER       "username"
#define NVS_MQTT_PASS       "password"
#define NVS_MQTT_PREFIX     "topic_prefix"

#define DNS_PORT            53
#define DNS_MAX_LEN         256

#define MAX_SCAN_RESULTS    20

#ifndef CONFIG_PROV_RESET_GPIO
#define CONFIG_PROV_RESET_GPIO 9
#endif

// ============================================================================
// State
// ============================================================================

static EventGroupHandle_t s_wifi_event_group = NULL;
static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_dns_task = NULL;
static bool s_wifi_connected = false;
static bool s_credentials_received = false;
static bool s_dns_running = false;
static char s_target_ssid[33] = {0};
static char s_target_pass[65] = {0};

// MQTT config from web form
static mqtt_config_t s_mqtt_config = {0};
static bool s_mqtt_config_loaded = false;

// WiFi scan results
static wifi_ap_record_t s_scan_results[MAX_SCAN_RESULTS];
static uint16_t s_scan_count = 0;

// ============================================================================
// HTML Templates
// ============================================================================

// iOS-friendly input attributes to disable autocorrect/autocapitalize/password suggestions
#define INPUT_ATTRS "autocomplete='off' autocorrect='off' autocapitalize='off' spellcheck='false'"

static const char SETUP_HTML_HEAD[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>IntelliChem Setup</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}"
"h1{color:#333;margin-bottom:20px;}"
"h2{color:#667eea;font-size:16px;margin:20px 0 10px 0;padding-top:15px;border-top:1px solid #eee;}"
".box{background:white;padding:25px;border-radius:12px;max-width:380px;margin:20px auto;box-shadow:0 10px 40px rgba(0,0,0,0.2);}"
"input[type=text],input[type=password],select{width:100%%;padding:14px;margin:8px 0 16px 0;box-sizing:border-box;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;transition:border-color 0.2s;background:white;}"
"input[type=text]:focus,input[type=password]:focus,select:focus{border-color:#667eea;outline:none;}"
"input[type=submit]{background:linear-gradient(135deg,#667eea 0%%,#764ba2 100%%);color:white;padding:16px;margin:8px 0;border:none;cursor:pointer;width:100%%;border-radius:8px;font-size:18px;font-weight:600;transition:transform 0.1s,box-shadow 0.2s;}"
"input[type=submit]:hover{transform:translateY(-2px);box-shadow:0 5px 20px rgba(102,126,234,0.4);}"
"input[type=submit]:active{transform:translateY(0);}"
"label{font-weight:600;color:#333;display:block;margin-bottom:4px;}"
".opt{font-weight:400;color:#888;font-size:12px;}"
".info{color:#666;font-size:13px;margin-top:20px;padding-top:15px;border-top:1px solid #eee;}"
".logo{text-align:center;margin-bottom:15px;font-size:48px;}"
".signal{color:#888;font-size:12px;}"
"</style></head><body>"
"<div class='box'>"
"<div class='logo'>&#x1F3CA;</div>"
"<h1>IntelliChem Setup</h1>"
"<form action='/save' method='post'>"
"<label>WiFi Network:</label>";

// Network options are inserted dynamically here

static const char SETUP_HTML_MIDDLE[] =
"<label>WiFi Password:</label>"
"<input type='password' name='password' maxlength='64' " INPUT_ATTRS " placeholder='Enter WiFi password'>"
"<h2>MQTT Settings</h2>"
"<label>MQTT Broker: <span class='opt'>(required)</span></label>"
"<input type='text' name='mqtt_broker' maxlength='128' required " INPUT_ATTRS " value='mqtt://192.168.1.100:1883'>"
"<label>MQTT Username: <span class='opt'>(optional)</span></label>"
"<input type='text' name='mqtt_user' maxlength='64' " INPUT_ATTRS " placeholder='Leave blank if no auth'>"
"<label>MQTT Password: <span class='opt'>(optional)</span></label>"
"<input type='password' name='mqtt_pass' maxlength='64' " INPUT_ATTRS " placeholder='Leave blank if no auth'>"
"<label>Topic Prefix: <span class='opt'>(optional)</span></label>"
"<input type='text' name='mqtt_prefix' maxlength='64' " INPUT_ATTRS " value='intellichem2mqtt'>"
"<input type='submit' value='Save &amp; Connect'>"
"</form>"
"<p class='info'>Your IntelliChem device will connect to your home WiFi and publish pool data to MQTT.</p>"
"</div></body></html>";

// Buffer for dynamically generated HTML
static char s_html_buffer[4096];

// Signal strength to bars
static const char* rssi_to_signal(int8_t rssi)
{
    if (rssi >= -50) return "&#9679;&#9679;&#9679;&#9679;";  // Excellent
    if (rssi >= -60) return "&#9679;&#9679;&#9679;&#9675;";  // Good
    if (rssi >= -70) return "&#9679;&#9679;&#9675;&#9675;";  // Fair
    return "&#9679;&#9675;&#9675;&#9675;";                    // Weak
}

// Generate HTML with WiFi network dropdown
static void generate_setup_html(void)
{
    int offset = 0;

    // Copy header
    offset += snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset, "%s", SETUP_HTML_HEAD);

    // Generate network dropdown
    if (s_scan_count > 0) {
        offset += snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset,
            "<select name='ssid' required>");
        offset += snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset,
            "<option value=''>-- Select Network --</option>");

        for (int i = 0; i < s_scan_count && offset < (int)sizeof(s_html_buffer) - 200; i++) {
            const char *ssid = (const char *)s_scan_results[i].ssid;
            if (strlen(ssid) == 0) continue;  // Skip hidden networks

            offset += snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset,
                "<option value='%s'>%s <span class='signal'>%s</span></option>",
                ssid, ssid, rssi_to_signal(s_scan_results[i].rssi));
        }

        offset += snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset,
            "</select>");
    } else {
        // Fallback to text input if no networks found
        offset += snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset,
            "<input type='text' name='ssid' maxlength='32' required " INPUT_ATTRS " placeholder='Enter WiFi network name'>");
    }

    // Copy rest of form
    snprintf(s_html_buffer + offset, sizeof(s_html_buffer) - offset, "%s", SETUP_HTML_MIDDLE);
}

static const char SAVED_HTML[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Connected!</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#11998e 0%%,#38ef7d 100%%);min-height:100vh;}"
".box{background:white;padding:30px;border-radius:12px;max-width:380px;margin:20px auto;box-shadow:0 10px 40px rgba(0,0,0,0.2);text-align:center;}"
"h1{color:#11998e;margin-bottom:15px;}"
".check{font-size:64px;margin-bottom:10px;color:#11998e;}"
"p{color:#666;line-height:1.6;}"
"strong{color:#333;}"
"</style></head><body>"
"<div class='box'>"
"<div class='check'>OK</div>"
"<h1>WiFi Saved!</h1>"
"<p>Connecting to <strong>%s</strong>...</p>"
"<p>You can close this page. The device will connect automatically.</p>"
"</div></body></html>";

// ============================================================================
// DNS Server (Captive Portal)
// ============================================================================

// DNS header structure
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS response (points to 192.168.4.1)
static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[DNS_MAX_LEN];
    uint8_t tx_buffer[DNS_MAX_LEN];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS: Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS: Failed to bind socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
    s_dns_running = true;

    // AP IP address: 192.168.4.1
    uint8_t ap_ip[4] = {192, 168, 4, 1};

    while (s_dns_running) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&client_addr, &client_len);

        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        // Build response
        memcpy(tx_buffer, rx_buffer, len);
        dns_header_t *resp_header = (dns_header_t *)tx_buffer;

        // Set response flags: QR=1 (response), AA=1 (authoritative), RD=1, RA=1
        resp_header->flags = htons(0x8580);
        resp_header->an_count = htons(1);  // One answer

        // Find end of question section
        int offset = sizeof(dns_header_t);
        while (offset < len && rx_buffer[offset] != 0) {
            offset += rx_buffer[offset] + 1;
        }
        offset += 5;  // Skip null byte + qtype (2) + qclass (2)

        // Append answer
        int ans_offset = offset;

        // Name pointer (points to name in question)
        tx_buffer[ans_offset++] = 0xc0;
        tx_buffer[ans_offset++] = 0x0c;

        // Type A (1)
        tx_buffer[ans_offset++] = 0x00;
        tx_buffer[ans_offset++] = 0x01;

        // Class IN (1)
        tx_buffer[ans_offset++] = 0x00;
        tx_buffer[ans_offset++] = 0x01;

        // TTL (60 seconds)
        tx_buffer[ans_offset++] = 0x00;
        tx_buffer[ans_offset++] = 0x00;
        tx_buffer[ans_offset++] = 0x00;
        tx_buffer[ans_offset++] = 0x3c;

        // Data length (4 bytes for IPv4)
        tx_buffer[ans_offset++] = 0x00;
        tx_buffer[ans_offset++] = 0x04;

        // IP address: 192.168.4.1
        tx_buffer[ans_offset++] = ap_ip[0];
        tx_buffer[ans_offset++] = ap_ip[1];
        tx_buffer[ans_offset++] = ap_ip[2];
        tx_buffer[ans_offset++] = ap_ip[3];

        sendto(sock, tx_buffer, ans_offset, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    close(sock);
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

static void start_dns_server(void)
{
    if (s_dns_task == NULL) {
        xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task);
    }
}

static void stop_dns_server(void)
{
    s_dns_running = false;
    if (s_dns_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_dns_task = NULL;
    }
}

// ============================================================================
// URL Decode Helper
// ============================================================================

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    char a, b;
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && ((a = src[1]) && (b = src[2])) &&
            isxdigit((unsigned char)a) && isxdigit((unsigned char)b)) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            dst[i++] = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// ============================================================================
// HTTP Handlers
// ============================================================================

static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving setup page (%d networks available)", s_scan_count);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_html_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Helper to extract and decode a form field value
static bool parse_form_field(const char *buf, const char *field, char *out, size_t out_size)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "%s=", field);

    char *start = strstr(buf, pattern);
    if (!start) {
        out[0] = '\0';
        return false;
    }

    start += strlen(pattern);
    const char *end = strchr(start, '&');

    // Temporarily null-terminate
    char saved = 0;
    if (end) {
        saved = *end;
        *(char*)end = '\0';
    }

    url_decode(out, start, out_size);

    // Restore
    if (end) {
        *(char*)end = saved;
    }

    return strlen(out) > 0;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    // Larger buffer for WiFi + MQTT fields
    char buf[512];
    int ret, remaining = req->content_len;

    if (remaining > (int)sizeof(buf) - 1) {
        remaining = sizeof(buf) - 1;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Received form data (%d bytes)", ret);

    // Parse WiFi fields
    parse_form_field(buf, "ssid", s_target_ssid, sizeof(s_target_ssid));
    parse_form_field(buf, "password", s_target_pass, sizeof(s_target_pass));

    // Parse MQTT fields
    parse_form_field(buf, "mqtt_broker", s_mqtt_config.broker_uri, sizeof(s_mqtt_config.broker_uri));
    parse_form_field(buf, "mqtt_user", s_mqtt_config.username, sizeof(s_mqtt_config.username));
    parse_form_field(buf, "mqtt_pass", s_mqtt_config.password, sizeof(s_mqtt_config.password));
    parse_form_field(buf, "mqtt_prefix", s_mqtt_config.topic_prefix, sizeof(s_mqtt_config.topic_prefix));

    // Set default topic prefix if not provided
    if (strlen(s_mqtt_config.topic_prefix) == 0) {
        strncpy(s_mqtt_config.topic_prefix, "intellichem2mqtt", sizeof(s_mqtt_config.topic_prefix) - 1);
    }

    ESP_LOGI(TAG, "Parsed - SSID: '%s', MQTT: '%s'", s_target_ssid, s_mqtt_config.broker_uri);

    // Send response
    char response[900];
    snprintf(response, sizeof(response), SAVED_HTML, s_target_ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    // Signal that credentials were received
    s_credentials_received = true;

    return ESP_OK;
}

// Handle common captive portal detection URLs
static esp_err_t captive_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    // Android/Chrome connectivity check
    if (strstr(uri, "generate_204") || strstr(uri, "gen_204")) {
        // Return 302 redirect to trigger captive portal
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Apple captive portal detection
    if (strstr(uri, "hotspot-detect") || strstr(uri, "captive.apple.com")) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Windows NCSI
    if (strstr(uri, "ncsi.txt") || strstr(uri, "connecttest")) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Default: redirect to setup page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ============================================================================
// HTTP Server
// ============================================================================

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;

    // Increase socket capacity for captive portal traffic
    // Mobile devices make many rapid connection attempts
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;  // Recycle idle sockets when limit reached

    ESP_LOGI(TAG, "Starting HTTP server on port %d (max_sockets=%d)", config.server_port, config.max_open_sockets);

    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    // Root page (must be registered first for priority)
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(s_httpd, &root);

    // Save credentials
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
    };
    httpd_register_uri_handler(s_httpd, &save);

    // Captive portal detection (wildcard catches all other requests)
    httpd_uri_t captive = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_handler,
    };
    httpd_register_uri_handler(s_httpd, &captive);

    return s_httpd;
}

static void stop_webserver(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

// ============================================================================
// NVS Functions
// ============================================================================

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    return err;
}

static esp_err_t load_wifi_credentials(char *ssid, size_t ssid_len,
                                        char *password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_PASS, password, &pass_len);
    if (err != ESP_OK) {
        // Password might be empty, that's OK
        password[0] = '\0';
    }

    nvs_close(handle);
    return ESP_OK;
}

static void clear_wifi_credentials(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGW(TAG, "WiFi credentials cleared from NVS");
    }
}

// MQTT config NVS functions
static esp_err_t save_mqtt_config(const mqtt_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open MQTT NVS: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, NVS_MQTT_BROKER, config->broker_uri);
    nvs_set_str(handle, NVS_MQTT_USER, config->username);
    nvs_set_str(handle, NVS_MQTT_PASS, config->password);
    nvs_set_str(handle, NVS_MQTT_PREFIX, config->topic_prefix);

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "MQTT config saved to NVS (broker: %s)", config->broker_uri);
    return err;
}

static esp_err_t load_mqtt_config(mqtt_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_MQTT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t len;

    len = sizeof(config->broker_uri);
    if (nvs_get_str(handle, NVS_MQTT_BROKER, config->broker_uri, &len) != ESP_OK) {
        config->broker_uri[0] = '\0';
    }

    len = sizeof(config->username);
    if (nvs_get_str(handle, NVS_MQTT_USER, config->username, &len) != ESP_OK) {
        config->username[0] = '\0';
    }

    len = sizeof(config->password);
    if (nvs_get_str(handle, NVS_MQTT_PASS, config->password, &len) != ESP_OK) {
        config->password[0] = '\0';
    }

    len = sizeof(config->topic_prefix);
    if (nvs_get_str(handle, NVS_MQTT_PREFIX, config->topic_prefix, &len) != ESP_OK) {
        config->topic_prefix[0] = '\0';
    }

    nvs_close(handle);

    // Check if we have at least a broker URI
    if (strlen(config->broker_uri) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

static void clear_mqtt_config(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGW(TAG, "MQTT config cleared from NVS");
    }
}

// ============================================================================
// Event Handler
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event =
                    (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "WiFi disconnected, reason: %d", event->reason);
                s_wifi_connected = false;

                // Retry connection
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_wifi_connect();
                break;
            }

            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Device connected to setup AP - captive portal should appear");
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Device disconnected from setup AP");
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_PROV_CONNECTED_BIT);
    }
}

// ============================================================================
// Reset Button Check
// ============================================================================

static bool check_reset_button(void)
{
#if CONFIG_PROV_RESET_GPIO >= 0
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_PROV_RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    vTaskDelay(pdMS_TO_TICKS(100));
    if (gpio_get_level(CONFIG_PROV_RESET_GPIO) == 0) {
        ESP_LOGW(TAG, "Reset button held - clearing WiFi credentials");
        return true;
    }
#endif
    return false;
}

// ============================================================================
// WiFi Scanning
// ============================================================================

static void scan_wifi_networks(void)
{
    ESP_LOGI(TAG, "Scanning for WiFi networks...");

    // Start WiFi in STA mode for scanning
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Configure scan - active scan for better results
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    // Perform blocking scan
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        esp_wifi_stop();
        return;
    }

    // Get scan results
    s_scan_count = MAX_SCAN_RESULTS;
    err = esp_wifi_scan_get_ap_records(&s_scan_count, s_scan_results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get scan results: %s", esp_err_to_name(err));
        s_scan_count = 0;
    }

    // Stop WiFi (will restart in AP mode)
    esp_wifi_stop();

    // Log results
    ESP_LOGI(TAG, "Found %d WiFi networks:", s_scan_count);
    for (int i = 0; i < s_scan_count && i < 10; i++) {
        ESP_LOGI(TAG, "  %d. %s (RSSI: %d)", i + 1,
                 s_scan_results[i].ssid, s_scan_results[i].rssi);
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t wifi_prov_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS partition");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Initialize TCP/IP and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create network interfaces
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    return ESP_OK;
}

esp_err_t wifi_prov_start(void)
{
    // Check reset button
    if (check_reset_button()) {
        clear_wifi_credentials();
        clear_mqtt_config();
    }

    // Try to load saved credentials
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    bool has_credentials = (load_wifi_credentials(saved_ssid, sizeof(saved_ssid),
                                                   saved_pass, sizeof(saved_pass)) == ESP_OK)
                           && (strlen(saved_ssid) > 0);

    // Try to load MQTT config from NVS
    if (load_mqtt_config(&s_mqtt_config) == ESP_OK) {
        s_mqtt_config_loaded = true;
        ESP_LOGI(TAG, "Loaded MQTT config from NVS: %s", s_mqtt_config.broker_uri);
    }

    if (has_credentials) {
        // Connect to saved network
        ESP_LOGI(TAG, "Found saved credentials, connecting to '%s'...", saved_ssid);

        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, saved_ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, saved_pass, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

    } else {
        // Scan for available networks first
        scan_wifi_networks();

        // Generate setup page HTML with network list
        generate_setup_html();

        // Start captive portal setup
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║     IntelliChem WiFi Setup Mode          ║");
        ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║  1. Connect to: %s      ║", SETUP_AP_SSID);
        ESP_LOGI(TAG, "║     (No password required)               ║");
        ESP_LOGI(TAG, "║                                          ║");
        ESP_LOGI(TAG, "║  2. Setup page opens automatically       ║");
        ESP_LOGI(TAG, "║     Or go to: http://192.168.4.1         ║");
        ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
        ESP_LOGI(TAG, "");

        // Configure open AP (no password)
        wifi_config_t ap_config = {
            .ap = {
                .ssid = SETUP_AP_SSID,
                .ssid_len = strlen(SETUP_AP_SSID),
                .channel = SETUP_AP_CHANNEL,
                .password = "",
                .max_connection = SETUP_AP_MAX_CONN,
                .authmode = WIFI_AUTH_OPEN,
            },
        };

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        // Start DNS server (redirects all domains to 192.168.4.1)
        start_dns_server();

        // Start web server
        start_webserver();

        // Wait for credentials to be entered
        ESP_LOGI(TAG, "Waiting for WiFi configuration...");
        while (!s_credentials_received) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Stop servers
        stop_dns_server();
        stop_webserver();
        esp_wifi_stop();

        // Save credentials
        save_wifi_credentials(s_target_ssid, s_target_pass);
        save_mqtt_config(&s_mqtt_config);
        s_mqtt_config_loaded = true;

        // Give time for the response to be sent
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Connect to the configured network
        ESP_LOGI(TAG, "Connecting to '%s'...", s_target_ssid);

        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, s_target_ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, s_target_pass, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    // Wait for connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_PROV_CONNECTED_BIT | WIFI_PROV_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_PROV_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        return ESP_FAIL;
    }
}

void wifi_prov_reset(void)
{
    clear_wifi_credentials();
    clear_mqtt_config();
}

bool wifi_prov_is_provisioned(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    return (load_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK)
           && (strlen(ssid) > 0);
}

bool wifi_prov_is_connected(void)
{
    return s_wifi_connected;
}

EventGroupHandle_t wifi_prov_get_event_group(void)
{
    return s_wifi_event_group;
}

bool wifi_prov_get_mqtt_config(mqtt_config_t *config)
{
    if (!s_mqtt_config_loaded || !config) {
        return false;
    }

    memcpy(config, &s_mqtt_config, sizeof(mqtt_config_t));
    return true;
}
