/**
 * @file commands.h
 * @brief Command message builders for IntelliChem control operations
 *
 * These functions build configuration commands to IntelliChem controllers
 * via Action 146 (21-byte payload).
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../models/state.h"

// ============================================================================
// Configuration Command Limits
// ============================================================================

#define PH_SETPOINT_MIN         7.0f
#define PH_SETPOINT_MAX         7.6f
#define ORP_SETPOINT_MIN        400
#define ORP_SETPOINT_MAX        800
#define CALCIUM_HARDNESS_MIN    25
#define CALCIUM_HARDNESS_MAX    800
#define CYANURIC_ACID_MIN       0
#define CYANURIC_ACID_MAX       210
#define ALKALINITY_MIN          25
#define ALKALINITY_MAX          800
#define TANK_LEVEL_MIN          0
#define TANK_LEVEL_MAX          7

// ============================================================================
// Configuration Settings Structure
// ============================================================================

/**
 * @brief Current IntelliChem settings for building commands
 *
 * Used to preserve existing values when making partial updates.
 */
typedef struct {
    float ph_setpoint;          // 7.0-7.6
    uint16_t orp_setpoint;      // 400-800 mV
    uint8_t ph_tank_level;      // 0-7 (0=disabled)
    uint8_t orp_tank_level;     // 0-7 (0=disabled)
    uint16_t calcium_hardness;  // 25-800 ppm
    uint8_t cyanuric_acid;      // 0-210 ppm
    uint16_t alkalinity;        // 25-800 ppm
} intellichem_settings_t;

// ============================================================================
// Command Building Functions
// ============================================================================

/**
 * @brief Build a configuration command payload
 *
 * Builds the 21-byte configuration payload (Action 146) for IntelliChem.
 *
 * Payload format:
 *   Byte  0-1: pH setpoint (value × 100, big-endian)
 *   Byte  2-3: ORP setpoint (big-endian)
 *   Byte  4: pH tank level (0-7)
 *   Byte  5: ORP tank level (0-7)
 *   Byte  6-7: Calcium hardness (big-endian)
 *   Byte  8: Reserved
 *   Byte  9: Cyanuric acid
 *   Byte 10: Alkalinity high byte
 *   Byte 11: Reserved
 *   Byte 12: Alkalinity low byte
 *   Byte 13-20: Reserved
 *
 * @param buf Output buffer (must be at least CONFIG_PAYLOAD_LENGTH bytes)
 * @param settings Configuration settings
 * @return Size of payload (21 bytes) on success, 0 on error
 */
size_t command_build_config_payload(uint8_t *buf, const intellichem_settings_t *settings);

/**
 * @brief Build a complete configuration command message
 *
 * Builds the full RS-485 packet with preamble, header, payload, and checksum.
 *
 * @param buf Output buffer (should be at least 35 bytes)
 * @param buf_size Size of output buffer
 * @param intellichem_addr Target IntelliChem address
 * @param settings Configuration settings
 * @return Size of complete message, 0 on error
 */
size_t command_build_config(uint8_t *buf, size_t buf_size,
                            uint8_t intellichem_addr,
                            const intellichem_settings_t *settings);

/**
 * @brief Initialize settings from current IntelliChem state
 *
 * Copies current state values to settings structure for building commands.
 *
 * @param settings Output settings structure
 * @param state Current IntelliChem state
 */
void command_settings_from_state(intellichem_settings_t *settings,
                                  const intellichem_state_t *state);

/**
 * @brief Initialize settings with default values
 *
 * @param settings Output settings structure
 */
void command_settings_init(intellichem_settings_t *settings);

// ============================================================================
// Validation Functions
// ============================================================================

/**
 * @brief Validate pH setpoint
 *
 * @param value pH setpoint value
 * @return true if valid (7.0-7.6)
 */
bool command_validate_ph_setpoint(float value);

/**
 * @brief Validate ORP setpoint
 *
 * @param value ORP setpoint value
 * @return true if valid (400-800 mV)
 */
bool command_validate_orp_setpoint(uint16_t value);

/**
 * @brief Validate calcium hardness
 *
 * @param value Calcium hardness in ppm
 * @return true if valid (25-800 ppm)
 */
bool command_validate_calcium_hardness(uint16_t value);

/**
 * @brief Validate cyanuric acid
 *
 * @param value Cyanuric acid in ppm
 * @return true if valid (0-210 ppm)
 */
bool command_validate_cyanuric_acid(uint8_t value);

/**
 * @brief Validate alkalinity
 *
 * @param value Alkalinity in ppm
 * @return true if valid (25-800 ppm)
 */
bool command_validate_alkalinity(uint16_t value);

/**
 * @brief Validate tank level
 *
 * @param value Tank level (0-7)
 * @return true if valid
 */
bool command_validate_tank_level(uint8_t value);

/**
 * @brief Validate all settings
 *
 * @param settings Settings to validate
 * @return true if all values are valid
 */
bool command_validate_settings(const intellichem_settings_t *settings);

// ============================================================================
// Logging
// ============================================================================

/**
 * @brief Log command settings
 *
 * @param settings Settings to log
 */
void command_log_settings(const intellichem_settings_t *settings);

#endif // COMMANDS_H
