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

#define DNS_PORT            53
#define DNS_MAX_LEN         256

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

// ============================================================================
// HTML Pages
// ============================================================================

static const char SETUP_HTML[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>IntelliChem WiFi Setup</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}"
"h1{color:#333;margin-bottom:20px;}"
".box{background:white;padding:25px;border-radius:12px;max-width:380px;margin:20px auto;box-shadow:0 10px 40px rgba(0,0,0,0.2);}"
"input[type=text],input[type=password]{width:100%;padding:14px;margin:8px 0 16px 0;box-sizing:border-box;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;transition:border-color 0.2s;}"
"input[type=text]:focus,input[type=password]:focus{border-color:#667eea;outline:none;}"
"input[type=submit]{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;padding:16px;margin:8px 0;border:none;cursor:pointer;width:100%;border-radius:8px;font-size:18px;font-weight:600;transition:transform 0.1s,box-shadow 0.2s;}"
"input[type=submit]:hover{transform:translateY(-2px);box-shadow:0 5px 20px rgba(102,126,234,0.4);}"
"input[type=submit]:active{transform:translateY(0);}"
"label{font-weight:600;color:#333;display:block;margin-bottom:4px;}"
".info{color:#666;font-size:13px;margin-top:20px;padding-top:15px;border-top:1px solid #eee;}"
".logo{text-align:center;margin-bottom:15px;font-size:48px;}"
"</style></head><body>"
"<div class='box'>"
"<div class='logo'>&#x1F3CA;</div>"
"<h1>IntelliChem WiFi Setup</h1>"
"<form action='/save' method='post'>"
"<label>WiFi Network Name:</label>"
"<input type='text' name='ssid' maxlength='32' required placeholder='Enter your WiFi name'>"
"<label>WiFi Password:</label>"
"<input type='password' name='password' maxlength='64' placeholder='Enter your WiFi password'>"
"<input type='submit' value='Connect'>"
"</form>"
"<p class='info'>Your IntelliChem device will connect to your home WiFi and begin monitoring your pool chemistry.</p>"
"</div></body></html>";

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
    ESP_LOGI(TAG, "Serving setup page");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[200];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(buf) - 1) {
        remaining = sizeof(buf) - 1;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Received form data: %s", buf);

    // Parse form data: ssid=xxx&password=yyy
    char *ssid_start = strstr(buf, "ssid=");
    char *pass_start = strstr(buf, "password=");

    if (ssid_start) {
        ssid_start += 5;
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) *ssid_end = '\0';
        url_decode(s_target_ssid, ssid_start, sizeof(s_target_ssid));
        if (ssid_end) *ssid_end = '&';
    }

    if (pass_start) {
        pass_start += 9;
        char *pass_end = strchr(pass_start, '&');
        if (pass_end) *pass_end = '\0';
        url_decode(s_target_pass, pass_start, sizeof(s_target_pass));
    }

    ESP_LOGI(TAG, "Parsed SSID: '%s'", s_target_ssid);

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

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

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
    }

    // Try to load saved credentials
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    bool has_credentials = (load_wifi_credentials(saved_ssid, sizeof(saved_ssid),
                                                   saved_pass, sizeof(saved_pass)) == ESP_OK)
                           && (strlen(saved_ssid) > 0);

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
