/**
 * @file serial_task.h
 * @brief Serial task for RS-485 IntelliChem communication
 */

#ifndef SERIAL_TASK_H
#define SERIAL_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "../models/state.h"

// ============================================================================
// Configuration
// ============================================================================

#ifndef CONFIG_UART_PORT_NUM
#define CONFIG_UART_PORT_NUM 1
#endif

#ifndef CONFIG_UART_TX_PIN
#define CONFIG_UART_TX_PIN 4
#endif

#ifndef CONFIG_UART_RX_PIN
#define CONFIG_UART_RX_PIN 5
#endif

#ifndef CONFIG_UART_BAUD_RATE
#define CONFIG_UART_BAUD_RATE 9600
#endif

#ifndef CONFIG_INTELLICHEM_ADDRESS
#define CONFIG_INTELLICHEM_ADDRESS 144
#endif

#ifndef CONFIG_INTELLICHEM_POLL_INTERVAL
#define CONFIG_INTELLICHEM_POLL_INTERVAL 30
#endif

#ifndef CONFIG_INTELLICHEM_TIMEOUT_MS
#define CONFIG_INTELLICHEM_TIMEOUT_MS 5000
#endif

// ============================================================================
// Command Message Types
// ============================================================================

typedef enum {
    CMD_TYPE_NONE = 0,
    CMD_TYPE_SET_PH_SETPOINT,
    CMD_TYPE_SET_ORP_SETPOINT,
    CMD_TYPE_SET_PH_DOSING_ENABLED,
    CMD_TYPE_SET_ORP_DOSING_ENABLED,
    CMD_TYPE_SET_CALCIUM_HARDNESS,
    CMD_TYPE_SET_CYANURIC_ACID,
    CMD_TYPE_SET_ALKALINITY,
    CMD_TYPE_REQUEST_STATUS,        // Force immediate status request
} command_type_t;

/**
 * @brief Command message for serial task queue
 */
typedef struct {
    command_type_t type;
    union {
        float ph_setpoint;          // 7.0 - 7.6
        uint16_t orp_setpoint;      // 400 - 800 mV
        bool dosing_enabled;        // true/false
        uint16_t calcium_hardness;  // 25 - 800 ppm
        uint8_t cyanuric_acid;      // 0 - 210 ppm
        uint16_t alkalinity;        // 25 - 800 ppm
    } value;
} serial_command_t;

// ============================================================================
// Task Interface
// ============================================================================

/**
 * @brief Initialize and start the serial task
 *
 * @param state_queue Queue to send parsed state updates (to MQTT task)
 * @param command_queue Queue to receive commands (from MQTT task)
 * @return ESP_OK on success
 */
esp_err_t serial_task_start(QueueHandle_t state_queue, QueueHandle_t command_queue);

/**
 * @brief Stop the serial task
 */
void serial_task_stop(void);

/**
 * @brief Check if serial task is running
 *
 * @return true if task is running
 */
bool serial_task_is_running(void);

/**
 * @brief Get last received state (may be stale)
 *
 * @param state Output state structure
 * @return true if state is available
 */
bool serial_task_get_last_state(intellichem_state_t *state);

/**
 * @brief Get serial task statistics
 *
 * @param polls_sent Number of poll requests sent
 * @param responses_received Number of valid responses received
 * @param errors Number of errors (timeouts, bad checksums, etc.)
 */
void serial_task_get_stats(uint32_t *polls_sent, uint32_t *responses_received, uint32_t *errors);

/**
 * @brief Force an immediate status poll
 *
 * @return ESP_OK if request queued successfully
 */
esp_err_t serial_task_force_poll(void);

#endif // SERIAL_TASK_H
