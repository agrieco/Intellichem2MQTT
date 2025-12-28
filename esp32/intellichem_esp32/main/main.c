/**
 * @file main.c
 * @brief IntelliChem2MQTT ESP32 Entry Point
 *
 * Phase 2: Serial task testing
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"

#include "protocol/constants.h"
#include "protocol/message.h"
#include "protocol/buffer.h"
#include "protocol/parser.h"
#include "models/state.h"
#include "serial/serial_task.h"
#include "mqtt/mqtt_task.h"

static const char *TAG = "main";

// ============================================================================
// Queue Handles
// ============================================================================

static QueueHandle_t s_state_queue = NULL;
static QueueHandle_t s_command_queue = NULL;

// ============================================================================
// Test Functions (Phase 1 - keep for regression)
// ============================================================================

static const uint8_t test_status_packet[] = {
    0xFF, 0x00, 0xFF,
    0xA5, 0x00, 0x10, 0x90, 0x12, 0x29,
    0x02, 0xD4, 0x02, 0xBC, 0x02, 0xD0, 0x02,
    0x8A, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x64, 0x00, 0x32, 0x05, 0x04, 0x00,
    0x01, 0x2C, 0x00, 0x32, 0x00, 0x50, 0x3C, 0x00, 0x52, 0x00, 0x00, 0x10, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x06, 0xBA
};

static void fix_test_packet_checksum(uint8_t *packet, size_t len) {
    uint16_t checksum = 0;
    for (size_t i = PREAMBLE_LENGTH; i < len - CHECKSUM_LENGTH; i++) {
        checksum += packet[i];
    }
    packet[len - 2] = (checksum >> 8) & 0xFF;
    packet[len - 1] = checksum & 0xFF;
}

static bool run_protocol_tests(void) {
    ESP_LOGI(TAG, "=== Running Protocol Layer Tests ===");

    bool all_passed = true;

    // Test 1: Message build
    uint8_t buf[64];
    size_t len = message_build(buf, sizeof(buf),
                               DEFAULT_INTELLICHEM_ADDRESS,
                               CONTROLLER_ADDRESS,
                               ACTION_STATUS_REQUEST,
                               NULL, 0);
    if (len != 11) {
        ESP_LOGE(TAG, "Message build test FAILED: expected 11 bytes, got %zu", len);
        all_passed = false;
    } else {
        ESP_LOGI(TAG, "Message build: PASS");
    }

    // Test 2: Checksum validation
    uint8_t packet[sizeof(test_status_packet)];
    memcpy(packet, test_status_packet, sizeof(packet));
    fix_test_packet_checksum(packet, sizeof(packet));

    if (!message_validate_checksum(packet, sizeof(packet))) {
        ESP_LOGE(TAG, "Checksum validation test FAILED");
        all_passed = false;
    } else {
        ESP_LOGI(TAG, "Checksum validation: PASS");
    }

    // Test 3: Status parsing
    intellichem_state_t state;
    if (!parser_parse_status(packet, sizeof(packet), &state)) {
        ESP_LOGE(TAG, "Status parsing test FAILED");
        all_passed = false;
    } else if (state.ph.level < 7.23f || state.ph.level > 7.25f) {
        ESP_LOGE(TAG, "Status parsing test FAILED: pH=%.2f", state.ph.level);
        all_passed = false;
    } else {
        ESP_LOGI(TAG, "Status parsing: PASS (pH=%.2f, ORP=%.0f, temp=%d)",
                 state.ph.level, state.orp.level, state.temperature);
    }

    ESP_LOGI(TAG, "Protocol tests: %s", all_passed ? "ALL PASSED" : "SOME FAILED");
    return all_passed;
}


// ============================================================================
// Main Entry Point
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "IntelliChem2MQTT ESP32");
    ESP_LOGI(TAG, "========================================");

    // Print chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip: %s, %d cores, WiFi%s%s",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    ESP_LOGI(TAG, "");

    // Run protocol layer tests first
    if (!run_protocol_tests()) {
        ESP_LOGE(TAG, "Protocol tests failed, aborting");
        return;
    }
    ESP_LOGI(TAG, "");

    // Create queues
    ESP_LOGI(TAG, "Creating FreeRTOS queues...");
    s_state_queue = xQueueCreate(2, sizeof(intellichem_state_t));
    s_command_queue = xQueueCreate(4, sizeof(serial_command_t));

    if (s_state_queue == NULL || s_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }
    ESP_LOGI(TAG, "Queues created: state=%p command=%p",
             (void*)s_state_queue, (void*)s_command_queue);

    // Start serial task
    ESP_LOGI(TAG, "Starting serial task...");
    esp_err_t ret = serial_task_start(s_state_queue, s_command_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start serial task: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Serial task started successfully");

    // Start MQTT task
    ESP_LOGI(TAG, "Starting MQTT task...");
    ret = mqtt_task_start(s_state_queue, s_command_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT task: %s", esp_err_to_name(ret));
        // Continue without MQTT - serial task will still work
        ESP_LOGW(TAG, "Running in serial-only mode (no MQTT)");
    } else {
        ESP_LOGI(TAG, "MQTT task started successfully");
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System initialized, waiting for data...");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  IntelliChem address: 0x%02X (%d)",
             CONFIG_INTELLICHEM_ADDRESS, CONFIG_INTELLICHEM_ADDRESS);
    ESP_LOGI(TAG, "  Poll interval: %d seconds", CONFIG_INTELLICHEM_POLL_INTERVAL);
    ESP_LOGI(TAG, "  UART port: %d (TX=%d, RX=%d)",
             CONFIG_UART_PORT_NUM, CONFIG_UART_TX_PIN, CONFIG_UART_RX_PIN);
    ESP_LOGI(TAG, "  RS-485 DE pin: %d", CONFIG_RS485_DE_PIN);
    ESP_LOGI(TAG, "  WiFi SSID: %s", CONFIG_WIFI_SSID);
    ESP_LOGI(TAG, "  MQTT broker: %s", CONFIG_MQTT_BROKER_URI);
    ESP_LOGI(TAG, "  MQTT topic prefix: %s", CONFIG_MQTT_TOPIC_PREFIX);
    ESP_LOGI(TAG, "  Control enabled: %s", CONFIG_CONTROL_ENABLED ? "yes" : "no");
    ESP_LOGI(TAG, "");

    // Main loop - just keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // Wake up every minute

        // Log periodic status
        uint32_t polls, responses, errors;
        serial_task_get_stats(&polls, &responses, &errors);

        uint32_t states_published;
        bool discovery_sent;
        uint32_t reconnections;
        mqtt_task_get_stats(&states_published, &discovery_sent, &reconnections);

        ESP_LOGI(TAG, "Heartbeat: serial[polls=%lu resp=%lu err=%lu] mqtt[pub=%lu disc=%s reconn=%lu status=%s]",
                 (unsigned long)polls, (unsigned long)responses, (unsigned long)errors,
                 (unsigned long)states_published,
                 discovery_sent ? "yes" : "no",
                 (unsigned long)reconnections,
                 mqtt_task_status_str(mqtt_task_get_status()));
    }
}
