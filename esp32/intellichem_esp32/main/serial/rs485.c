/**
 * @file rs485.c
 * @brief RS-485 direction control implementation
 */

#include "rs485.h"
#include "esp_log.h"

static const char *TAG = "rs485";

// ============================================================================
// State
// ============================================================================

static int s_de_pin = -1;
static bool s_tx_mode = false;

// ============================================================================
// Implementation
// ============================================================================

esp_err_t rs485_init(int de_pin)
{
    s_de_pin = de_pin;

    if (de_pin < 0) {
        ESP_LOGI(TAG, "RS-485 direction control disabled (auto-direction transceiver)");
        return ESP_OK;
    }

    // Configure GPIO as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << de_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DE/RE GPIO %d: %s", de_pin, esp_err_to_name(ret));
        return ret;
    }

    // Start in receive mode
    rs485_set_rx_mode();

    ESP_LOGI(TAG, "RS-485 direction control initialized on GPIO %d", de_pin);
    return ESP_OK;
}

void rs485_set_tx_mode(void)
{
    if (s_de_pin >= 0) {
        gpio_set_level(s_de_pin, 1);
        s_tx_mode = true;
        ESP_LOGD(TAG, "TX mode enabled");
    }
}

void rs485_set_rx_mode(void)
{
    if (s_de_pin >= 0) {
        gpio_set_level(s_de_pin, 0);
        s_tx_mode = false;
        ESP_LOGD(TAG, "RX mode enabled");
    }
}

bool rs485_is_enabled(void)
{
    return s_de_pin >= 0;
}

bool rs485_is_tx_mode(void)
{
    return s_tx_mode;
}
