/**
 * @file rs485.h
 * @brief RS-485 direction control for half-duplex communication
 */

#ifndef SERIAL_RS485_H
#define SERIAL_RS485_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// ============================================================================
// Configuration
// ============================================================================

// GPIO for RS-485 DE/RE pin (-1 to disable for auto-direction transceivers)
#ifndef CONFIG_RS485_DE_PIN
#define CONFIG_RS485_DE_PIN 19
#endif

// ============================================================================
// RS-485 Direction Control
// ============================================================================

/**
 * @brief Initialize RS-485 direction control GPIO
 *
 * @param de_pin GPIO pin for DE/RE control (-1 to disable)
 * @return ESP_OK on success
 */
esp_err_t rs485_init(int de_pin);

/**
 * @brief Set RS-485 transceiver to transmit mode
 *
 * Sets DE/RE high to enable transmitter
 */
void rs485_set_tx_mode(void);

/**
 * @brief Set RS-485 transceiver to receive mode
 *
 * Sets DE/RE low to enable receiver
 */
void rs485_set_rx_mode(void);

/**
 * @brief Check if RS-485 direction control is enabled
 *
 * @return true if DE/RE pin is configured
 */
bool rs485_is_enabled(void);

/**
 * @brief Get current DE/RE pin state
 *
 * @return true if in TX mode, false if in RX mode
 */
bool rs485_is_tx_mode(void);

#endif // SERIAL_RS485_H
