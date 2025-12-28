/**
 * @file serial_task.c
 * @brief Serial task implementation for RS-485 IntelliChem communication
 */

#include "serial_task.h"
#include "rs485.h"
#include "../protocol/constants.h"
#include "../protocol/message.h"
#include "../protocol/buffer.h"
#include "../protocol/parser.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "serial";

// ============================================================================
// Configuration
// ============================================================================

#define SERIAL_TASK_STACK_SIZE  4096
#define SERIAL_TASK_PRIORITY    5
#define UART_RX_BUFFER_SIZE     256
#define UART_TX_BUFFER_SIZE     0       // No TX buffer needed
#define UART_QUEUE_SIZE         20

// ============================================================================
// State
// ============================================================================

static TaskHandle_t s_task_handle = NULL;
static QueueHandle_t s_state_queue = NULL;
static QueueHandle_t s_command_queue = NULL;
static QueueHandle_t s_uart_queue = NULL;
static SemaphoreHandle_t s_state_mutex = NULL;

static intellichem_state_t s_last_state;
static packet_buffer_t s_rx_buffer;
static bool s_running = false;

// Statistics
static uint32_t s_polls_sent = 0;
static uint32_t s_responses_received = 0;
static uint32_t s_errors = 0;

// Current settings (for building config commands)
static intellichem_state_t s_current_settings;

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief Initialize UART for RS-485 communication
 */
static esp_err_t uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = CONFIG_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(CONFIG_UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(CONFIG_UART_PORT_NUM,
                       CONFIG_UART_TX_PIN,
                       CONFIG_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,  // RTS
                       UART_PIN_NO_CHANGE); // CTS
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(CONFIG_UART_PORT_NUM,
                              UART_RX_BUFFER_SIZE,
                              UART_TX_BUFFER_SIZE,
                              UART_QUEUE_SIZE,
                              &s_uart_queue,
                              0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART%d initialized: %d baud, TX=%d RX=%d",
             CONFIG_UART_PORT_NUM, CONFIG_UART_BAUD_RATE,
             CONFIG_UART_TX_PIN, CONFIG_UART_RX_PIN);

    return ESP_OK;
}

/**
 * @brief Send a packet over RS-485
 */
static esp_err_t send_packet(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "TX [%zu bytes] to IntelliChem 0x%02X:",
             len, CONFIG_INTELLICHEM_ADDRESS);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    // Switch to TX mode
    rs485_set_tx_mode();

    // Small delay for transceiver to switch
    vTaskDelay(pdMS_TO_TICKS(1));

    // Send data
    int written = uart_write_bytes(CONFIG_UART_PORT_NUM, data, len);

    // Wait for TX complete
    esp_err_t ret = uart_wait_tx_done(CONFIG_UART_PORT_NUM, pdMS_TO_TICKS(100));

    // Small delay before switching back to RX
    vTaskDelay(pdMS_TO_TICKS(1));

    // Switch back to RX mode
    rs485_set_rx_mode();

    if (written != len) {
        ESP_LOGE(TAG, "UART write failed: wrote %d of %zu bytes", written, len);
        return ESP_FAIL;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART TX done timeout");
    }

    return ESP_OK;
}

/**
 * @brief Build and send status request
 */
static esp_err_t send_status_request(void)
{
    uint8_t buf[16];
    size_t len = message_build(buf, sizeof(buf),
                               CONFIG_INTELLICHEM_ADDRESS,
                               CONTROLLER_ADDRESS,
                               ACTION_STATUS_REQUEST,
                               NULL, 0);

    if (len == 0) {
        ESP_LOGE(TAG, "Failed to build status request");
        return ESP_FAIL;
    }

    s_polls_sent++;
    ESP_LOGI(TAG, "Sending status request #%lu to 0x%02X",
             (unsigned long)s_polls_sent, CONFIG_INTELLICHEM_ADDRESS);

    return send_packet(buf, len);
}

/**
 * @brief Process received UART data
 */
static void process_uart_data(void)
{
    uint8_t data[128];
    int len = uart_read_bytes(CONFIG_UART_PORT_NUM, data, sizeof(data),
                              pdMS_TO_TICKS(10));

    if (len > 0) {
        ESP_LOGD(TAG, "RX [%d bytes]:", len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

        buffer_add_bytes(&s_rx_buffer, data, len);

        // Try to extract complete packets
        uint8_t packet[MAX_PACKET_SIZE];
        size_t packet_len;

        while (buffer_get_packet(&s_rx_buffer, packet, sizeof(packet), &packet_len)) {
            ESP_LOGI(TAG, "Complete packet received (%zu bytes)", packet_len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, packet, packet_len, ESP_LOG_DEBUG);

            // Parse status response
            intellichem_state_t state;
            if (parser_parse_status(packet, packet_len, &state)) {
                s_responses_received++;

                // Update timestamp
                state.last_update_ms = esp_timer_get_time() / 1000;

                // Save state
                if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    memcpy(&s_last_state, &state, sizeof(state));
                    memcpy(&s_current_settings, &state, sizeof(state));
                    xSemaphoreGive(s_state_mutex);
                }

                // Send to state queue (non-blocking)
                if (s_state_queue != NULL) {
                    if (xQueueSend(s_state_queue, &state, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "State queue full, dropping update");
                    } else {
                        ESP_LOGI(TAG, "State sent to queue: pH=%.2f ORP=%.0fmV",
                                 state.ph.level, state.orp.level);
                    }
                }

                // Log full state
                parser_log_state(&state);
            } else {
                ESP_LOGW(TAG, "Failed to parse packet");
                s_errors++;
            }
        }
    }
}

/**
 * @brief Process command from command queue
 */
static void process_command(const serial_command_t *cmd)
{
    if (cmd == NULL) return;

    ESP_LOGI(TAG, "Processing command type %d", cmd->type);

    switch (cmd->type) {
        case CMD_TYPE_REQUEST_STATUS:
            send_status_request();
            break;

        case CMD_TYPE_SET_PH_SETPOINT:
            ESP_LOGI(TAG, "Set pH setpoint to %.2f", cmd->value.ph_setpoint);
            // TODO: Build and send config command in Phase 4
            break;

        case CMD_TYPE_SET_ORP_SETPOINT:
            ESP_LOGI(TAG, "Set ORP setpoint to %d mV", cmd->value.orp_setpoint);
            // TODO: Build and send config command in Phase 4
            break;

        case CMD_TYPE_SET_PH_DOSING_ENABLED:
            ESP_LOGI(TAG, "Set pH dosing enabled: %s",
                     cmd->value.dosing_enabled ? "true" : "false");
            // TODO: Build and send config command in Phase 4
            break;

        case CMD_TYPE_SET_ORP_DOSING_ENABLED:
            ESP_LOGI(TAG, "Set ORP dosing enabled: %s",
                     cmd->value.dosing_enabled ? "true" : "false");
            // TODO: Build and send config command in Phase 4
            break;

        default:
            ESP_LOGW(TAG, "Unknown command type: %d", cmd->type);
            break;
    }
}

/**
 * @brief Main serial task function
 */
static void serial_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Serial task started");

    // Initialize buffer
    buffer_init(&s_rx_buffer);

    // Initialize UART
    if (uart_init() != ESP_OK) {
        ESP_LOGE(TAG, "UART initialization failed, task exiting");
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Initialize RS-485 direction control
    rs485_init(CONFIG_RS485_DE_PIN);

    // Send initial status request
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for system to stabilize
    send_status_request();

    TickType_t last_poll_time = xTaskGetTickCount();
    const TickType_t poll_interval = pdMS_TO_TICKS(CONFIG_INTELLICHEM_POLL_INTERVAL * 1000);

    while (s_running) {
        // Check for UART events
        uart_event_t event;
        if (xQueueReceive(s_uart_queue, &event, pdMS_TO_TICKS(100))) {
            switch (event.type) {
                case UART_DATA:
                    process_uart_data();
                    break;

                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART FIFO overflow");
                    uart_flush_input(CONFIG_UART_PORT_NUM);
                    buffer_clear(&s_rx_buffer);
                    s_errors++;
                    break;

                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer full");
                    uart_flush_input(CONFIG_UART_PORT_NUM);
                    buffer_clear(&s_rx_buffer);
                    s_errors++;
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "UART parity error");
                    s_errors++;
                    break;

                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART frame error");
                    s_errors++;
                    break;

                default:
                    ESP_LOGD(TAG, "UART event: %d", event.type);
                    break;
            }
        }

        // Also check for any remaining data (no event)
        process_uart_data();

        // Check for commands
        if (s_command_queue != NULL) {
            serial_command_t cmd;
            while (xQueueReceive(s_command_queue, &cmd, 0) == pdTRUE) {
                process_command(&cmd);
            }
        }

        // Check if it's time to poll
        TickType_t now = xTaskGetTickCount();
        if ((now - last_poll_time) >= poll_interval) {
            send_status_request();
            last_poll_time = now;
        }
    }

    // Cleanup
    uart_driver_delete(CONFIG_UART_PORT_NUM);
    ESP_LOGI(TAG, "Serial task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t serial_task_start(QueueHandle_t state_queue, QueueHandle_t command_queue)
{
    if (s_running) {
        ESP_LOGW(TAG, "Serial task already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_state_queue = state_queue;
    s_command_queue = command_queue;

    // Create mutex for state access
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize state
    intellichem_state_init(&s_last_state);
    intellichem_state_init(&s_current_settings);

    // Reset stats
    s_polls_sent = 0;
    s_responses_received = 0;
    s_errors = 0;

    s_running = true;

    // Create task
    BaseType_t ret = xTaskCreate(serial_task,
                                 "serial_task",
                                 SERIAL_TASK_STACK_SIZE,
                                 NULL,
                                 SERIAL_TASK_PRIORITY,
                                 &s_task_handle);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial task");
        s_running = false;
        vSemaphoreDelete(s_state_mutex);
        s_state_mutex = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serial task started successfully");
    return ESP_OK;
}

void serial_task_stop(void)
{
    if (!s_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping serial task...");
    s_running = false;

    // Wait for task to exit
    vTaskDelay(pdMS_TO_TICKS(500));

    if (s_state_mutex != NULL) {
        vSemaphoreDelete(s_state_mutex);
        s_state_mutex = NULL;
    }

    s_task_handle = NULL;
}

bool serial_task_is_running(void)
{
    return s_running;
}

bool serial_task_get_last_state(intellichem_state_t *state)
{
    if (state == NULL || s_state_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(state, &s_last_state, sizeof(intellichem_state_t));
        xSemaphoreGive(s_state_mutex);
        return s_last_state.last_update_ms > 0;
    }

    return false;
}

void serial_task_get_stats(uint32_t *polls_sent, uint32_t *responses_received, uint32_t *errors)
{
    if (polls_sent) *polls_sent = s_polls_sent;
    if (responses_received) *responses_received = s_responses_received;
    if (errors) *errors = s_errors;
}

esp_err_t serial_task_force_poll(void)
{
    if (!s_running || s_command_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    serial_command_t cmd = {
        .type = CMD_TYPE_REQUEST_STATUS,
    };

    if (xQueueSend(s_command_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
