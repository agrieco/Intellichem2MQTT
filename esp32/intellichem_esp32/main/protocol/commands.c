/**
 * @file commands.c
 * @brief Command message builders for IntelliChem control operations
 */

#include "commands.h"
#include "constants.h"
#include "message.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "commands";

// ============================================================================
// Validation Functions
// ============================================================================

bool command_validate_ph_setpoint(float value)
{
    return (value >= PH_SETPOINT_MIN && value <= PH_SETPOINT_MAX);
}

bool command_validate_orp_setpoint(uint16_t value)
{
    return (value >= ORP_SETPOINT_MIN && value <= ORP_SETPOINT_MAX);
}

bool command_validate_calcium_hardness(uint16_t value)
{
    return (value >= CALCIUM_HARDNESS_MIN && value <= CALCIUM_HARDNESS_MAX);
}

bool command_validate_cyanuric_acid(uint8_t value)
{
    return (value <= CYANURIC_ACID_MAX);  // Min is 0
}

bool command_validate_alkalinity(uint16_t value)
{
    return (value >= ALKALINITY_MIN && value <= ALKALINITY_MAX);
}

bool command_validate_tank_level(uint8_t value)
{
    return (value <= TANK_LEVEL_MAX);  // Min is 0
}

bool command_validate_settings(const intellichem_settings_t *settings)
{
    if (settings == NULL) return false;

    if (!command_validate_ph_setpoint(settings->ph_setpoint)) {
        ESP_LOGW(TAG, "Invalid pH setpoint: %.2f", settings->ph_setpoint);
        return false;
    }

    if (!command_validate_orp_setpoint(settings->orp_setpoint)) {
        ESP_LOGW(TAG, "Invalid ORP setpoint: %d", settings->orp_setpoint);
        return false;
    }

    if (!command_validate_tank_level(settings->ph_tank_level)) {
        ESP_LOGW(TAG, "Invalid pH tank level: %d", settings->ph_tank_level);
        return false;
    }

    if (!command_validate_tank_level(settings->orp_tank_level)) {
        ESP_LOGW(TAG, "Invalid ORP tank level: %d", settings->orp_tank_level);
        return false;
    }

    if (!command_validate_calcium_hardness(settings->calcium_hardness)) {
        ESP_LOGW(TAG, "Invalid calcium hardness: %d", settings->calcium_hardness);
        return false;
    }

    if (!command_validate_cyanuric_acid(settings->cyanuric_acid)) {
        ESP_LOGW(TAG, "Invalid cyanuric acid: %d", settings->cyanuric_acid);
        return false;
    }

    if (!command_validate_alkalinity(settings->alkalinity)) {
        ESP_LOGW(TAG, "Invalid alkalinity: %d", settings->alkalinity);
        return false;
    }

    return true;
}

// ============================================================================
// Settings Initialization
// ============================================================================

void command_settings_init(intellichem_settings_t *settings)
{
    if (settings == NULL) return;

    settings->ph_setpoint = 7.2f;
    settings->orp_setpoint = 650;
    settings->ph_tank_level = 7;
    settings->orp_tank_level = 7;
    settings->calcium_hardness = 300;
    settings->cyanuric_acid = 30;
    settings->alkalinity = 80;
}

void command_settings_from_state(intellichem_settings_t *settings,
                                  const intellichem_state_t *state)
{
    if (settings == NULL || state == NULL) return;

    settings->ph_setpoint = state->ph.setpoint;
    settings->orp_setpoint = (uint16_t)state->orp.setpoint;
    settings->ph_tank_level = state->ph.tank_level;
    settings->orp_tank_level = state->orp.tank_level;
    settings->calcium_hardness = state->calcium_hardness;
    settings->cyanuric_acid = state->cyanuric_acid;
    settings->alkalinity = state->alkalinity;
}

// ============================================================================
// Command Building
// ============================================================================

size_t command_build_config_payload(uint8_t *buf, const intellichem_settings_t *settings)
{
    if (buf == NULL || settings == NULL) {
        return 0;
    }

    if (!command_validate_settings(settings)) {
        ESP_LOGE(TAG, "Invalid settings, cannot build payload");
        return 0;
    }

    // Clear the payload buffer
    memset(buf, 0, CONFIG_PAYLOAD_LENGTH);

    // pH setpoint (bytes 0-1): value × 100 as 16-bit big-endian
    uint16_t ph_value = (uint16_t)(settings->ph_setpoint * 100);
    buf[0] = (ph_value >> 8) & 0xFF;
    buf[1] = ph_value & 0xFF;

    // ORP setpoint (bytes 2-3): value as 16-bit big-endian
    buf[2] = (settings->orp_setpoint >> 8) & 0xFF;
    buf[3] = settings->orp_setpoint & 0xFF;

    // Tank levels (bytes 4-5)
    buf[4] = settings->ph_tank_level;
    buf[5] = settings->orp_tank_level;

    // Calcium hardness (bytes 6-7): 16-bit big-endian
    buf[6] = (settings->calcium_hardness >> 8) & 0xFF;
    buf[7] = settings->calcium_hardness & 0xFF;

    // Byte 8: Reserved (already zeroed)

    // Cyanuric acid (byte 9)
    buf[9] = settings->cyanuric_acid;

    // Alkalinity (bytes 10-12): high byte, reserved, low byte
    buf[10] = (settings->alkalinity >> 8) & 0xFF;
    buf[11] = 0;  // Reserved
    buf[12] = settings->alkalinity & 0xFF;

    // Bytes 13-20: Reserved (already zeroed)

    ESP_LOGD(TAG, "Built config payload: pH=%.2f ORP=%d ph_tank=%d orp_tank=%d",
             settings->ph_setpoint, settings->orp_setpoint,
             settings->ph_tank_level, settings->orp_tank_level);

    return CONFIG_PAYLOAD_LENGTH;
}

size_t command_build_config(uint8_t *buf, size_t buf_size,
                            uint8_t intellichem_addr,
                            const intellichem_settings_t *settings)
{
    if (buf == NULL || settings == NULL) {
        return 0;
    }

    // Build the payload first
    uint8_t payload[CONFIG_PAYLOAD_LENGTH];
    size_t payload_len = command_build_config_payload(payload, settings);
    if (payload_len == 0) {
        return 0;
    }

    // Build the complete message
    size_t msg_len = message_build(buf, buf_size,
                                   intellichem_addr,
                                   CONTROLLER_ADDRESS,
                                   ACTION_CONFIG_COMMAND,
                                   payload, (uint8_t)payload_len);

    if (msg_len > 0) {
        ESP_LOGI(TAG, "Built config command [%zu bytes] to 0x%02X",
                 msg_len, intellichem_addr);
    }

    return msg_len;
}

// ============================================================================
// Logging
// ============================================================================

void command_log_settings(const intellichem_settings_t *settings)
{
    if (settings == NULL) return;

    ESP_LOGI(TAG, "=== Configuration Settings ===");
    ESP_LOGI(TAG, "pH setpoint: %.2f (dosing %s)",
             settings->ph_setpoint,
             settings->ph_tank_level > 0 ? "enabled" : "disabled");
    ESP_LOGI(TAG, "ORP setpoint: %d mV (dosing %s)",
             settings->orp_setpoint,
             settings->orp_tank_level > 0 ? "enabled" : "disabled");
    ESP_LOGI(TAG, "pH tank level: %d", settings->ph_tank_level);
    ESP_LOGI(TAG, "ORP tank level: %d", settings->orp_tank_level);
    ESP_LOGI(TAG, "Calcium hardness: %d ppm", settings->calcium_hardness);
    ESP_LOGI(TAG, "Cyanuric acid: %d ppm", settings->cyanuric_acid);
    ESP_LOGI(TAG, "Alkalinity: %d ppm", settings->alkalinity);
}
