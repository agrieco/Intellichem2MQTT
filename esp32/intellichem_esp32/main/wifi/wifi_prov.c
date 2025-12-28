/**
 * @file wifi_prov.c
 * @brief WiFi provisioning implementation using ESP-IDF provisioning manager
 */

#include "wifi_prov.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#ifdef CONFIG_PROV_SHOW_QR
#include "qrcode.h"
#endif

static const char *TAG = "wifi_prov";

// ============================================================================
// Configuration
// ============================================================================

#ifndef CONFIG_PROV_POP
#define CONFIG_PROV_POP "intellichem"
#endif

#ifndef CONFIG_PROV_RESET_GPIO
#define CONFIG_PROV_RESET_GPIO 9
#endif

#ifndef CONFIG_PROV_MAX_RETRY
#define CONFIG_PROV_MAX_RETRY 5
#endif

#define PROV_QR_VERSION     "v1"
#define PROV_TRANSPORT      "softap"
#define QRCODE_BASE_URL     "https://espressif.github.io/esp-jumpstart/qrcode.html"

// ============================================================================
// State
// ============================================================================

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_wifi_connected = false;
static int s_retry_count = 0;

// ============================================================================
// Event Handler
// ============================================================================

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started - connect to WiFi AP and use phone app");
                break;

            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received WiFi credentials - SSID: %s",
                         (const char *)wifi_sta_cfg->ssid);
                break;
            }

            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Authentication failed" : "AP not found");
#ifdef CONFIG_PROV_RESET_ON_FAILURE
                s_retry_count++;
                if (s_retry_count >= CONFIG_PROV_MAX_RETRY) {
                    ESP_LOGI(TAG, "Max retries reached, resetting provisioning");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    s_retry_count = 0;
                }
#endif
                break;
            }

            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                s_retry_count = 0;
                break;

            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                wifi_prov_mgr_deinit();
                break;

            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
                s_wifi_connected = false;
                esp_wifi_connect();
                break;

            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Phone connected to provisioning AP");
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Phone disconnected from provisioning AP");
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected - IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_PROV_CONNECTED_BIT);
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

static void get_device_service_name(char *service_name, size_t max_len)
{
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max_len, "PROV_%02X%02X%02X",
             eth_mac[3], eth_mac[4], eth_mac[5]);
}

static void print_qr_code(const char *service_name, const char *pop)
{
#ifdef CONFIG_PROV_SHOW_QR
    char payload[150];
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, service_name, pop, PROV_TRANSPORT);

    ESP_LOGI(TAG, "Scan this QR code from the ESP SoftAP Provisioning app:");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);

    ESP_LOGI(TAG, "Or open this URL in browser: %s?data=%s", QRCODE_BASE_URL, payload);
#else
    ESP_LOGI(TAG, "Provisioning AP: %s, Password: %s", service_name, pop);
#endif
}

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

    // Check if button is held LOW
    vTaskDelay(pdMS_TO_TICKS(100));  // Debounce
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
    ESP_LOGI(TAG, "Initializing WiFi provisioning...");

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

    // Create default WiFi station and AP interfaces
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    return ESP_OK;
}

esp_err_t wifi_prov_start(void)
{
    // Check if reset button is held
    if (check_reset_button()) {
        wifi_prov_reset();
    }

    // Initialize provisioning manager
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // Check if already provisioned
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(TAG, "Device not provisioned - starting provisioning AP");

        // Get unique device name
        char service_name[16];
        get_device_service_name(service_name, sizeof(service_name));

        // Security settings - using Security 1 (simple but secure)
        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        const char *pop = CONFIG_PROV_POP;

        // Start provisioning (no password on the SoftAP itself)
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            security, pop, service_name, NULL));

        // Print QR code for easy setup
        print_qr_code(service_name, pop);
    } else {
        ESP_LOGI(TAG, "Already provisioned - connecting to saved WiFi");

        // Release provisioning resources
        wifi_prov_mgr_deinit();

        // Start WiFi in station mode
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    // Wait for connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_PROV_CONNECTED_BIT | WIFI_PROV_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY  // Wait forever
    );

    if (bits & WIFI_PROV_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        return ESP_FAIL;
    }
}

void wifi_prov_reset(void)
{
    ESP_LOGW(TAG, "Resetting WiFi credentials");
    wifi_prov_mgr_reset_provisioning();
}

bool wifi_prov_is_provisioned(void)
{
    bool provisioned = false;
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };

    if (wifi_prov_mgr_init(config) == ESP_OK) {
        wifi_prov_mgr_is_provisioned(&provisioned);
        wifi_prov_mgr_deinit();
    }
    return provisioned;
}

bool wifi_prov_is_connected(void)
{
    return s_wifi_connected;
}

EventGroupHandle_t wifi_prov_get_event_group(void)
{
    return s_wifi_event_group;
}
