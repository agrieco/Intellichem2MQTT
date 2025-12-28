/**
 * @file parser.c
 * @brief Status response parser implementation
 */

#include "parser.h"
#include "constants.h"
#include "message.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "parser";

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Extract big-endian 16-bit value from payload
 */
static inline uint16_t be16(const uint8_t *payload, size_t offset) {
    return ((uint16_t)payload[offset] << 8) | payload[offset + 1];
}

/**
 * @brief Parse signed LSI value from byte
 *
 * Bit 7 set means negative value.
 */
static float parse_lsi(uint8_t lsi_byte) {
    if (lsi_byte & 0x80) {
        // High bit set = negative
        return (256 - lsi_byte) / -100.0f;
    }
    return lsi_byte / 100.0f;
}

/**
 * @brief Parse tank level (1-7 from protocol, convert to 0-6)
 */
static uint8_t parse_tank_level(uint8_t raw) {
    if (raw == 0) return 0;
    return (raw > 1) ? (raw - 1) : 0;
}

/**
 * @brief Parse dosing status from status byte
 */
static dosing_status_t parse_dosing_status(uint8_t raw) {
    // Clamp to valid range
    if (raw > 2) raw = 2;
    return (dosing_status_t)raw;
}

// ============================================================================
// Parser Implementation
// ============================================================================

bool parser_parse_status(const uint8_t *packet, size_t len,
                         intellichem_state_t *state) {
    if (packet == NULL || state == NULL) {
        ESP_LOGE(TAG, "NULL argument");
        return false;
    }

    // Validate minimum packet size
    if (len < MIN_PACKET_SIZE) {
        ESP_LOGW(TAG, "Packet too short: %zu bytes", len);
        return false;
    }

    // Validate checksum
    if (!message_validate_checksum(packet, len)) {
        ESP_LOGW(TAG, "Invalid checksum in status response");
        return false;
    }

    // Check action code
    uint8_t action = message_get_action(packet);
    if (action != ACTION_STATUS_RESPONSE) {
        ESP_LOGD(TAG, "Not a status response (action=%d)", action);
        return false;
    }

    // Validate source is IntelliChem
    uint8_t source = message_get_source(packet);
    if (source < INTELLICHEM_ADDRESS_MIN || source > INTELLICHEM_ADDRESS_MAX) {
        ESP_LOGW(TAG, "Invalid source address: %d", source);
        return false;
    }

    // Get payload
    uint8_t payload_len = message_get_payload_len(packet);
    if (payload_len < STATUS_PAYLOAD_LENGTH) {
        ESP_LOGW(TAG, "Payload too short: %d < %d", payload_len, STATUS_PAYLOAD_LENGTH);
        return false;
    }

    const uint8_t *payload = message_get_payload(packet);
    if (payload == NULL) {
        ESP_LOGE(TAG, "Failed to get payload");
        return false;
    }

    return parser_parse_payload(payload, payload_len, source, state);
}

bool parser_parse_payload(const uint8_t *payload, size_t payload_len,
                          uint8_t address, intellichem_state_t *state) {
    if (payload == NULL || state == NULL) {
        return false;
    }

    if (payload_len < STATUS_PAYLOAD_LENGTH) {
        ESP_LOGW(TAG, "Payload too short: %zu < %d", payload_len, STATUS_PAYLOAD_LENGTH);
        return false;
    }

    // Initialize state
    intellichem_state_init(state);
    state->address = address;

    // ========================================================================
    // Parse pH data
    // ========================================================================
    state->ph.level = be16(payload, PAYLOAD_PH_LEVEL) / 100.0f;
    state->ph.setpoint = be16(payload, PAYLOAD_PH_SETPOINT) / 100.0f;
    state->ph.dose_time = be16(payload, PAYLOAD_PH_DOSE_TIME);
    state->ph.dose_volume = be16(payload, PAYLOAD_PH_DOSE_VOLUME);
    state->ph.tank_level = parse_tank_level(payload[PAYLOAD_PH_TANK_LEVEL]);

    ESP_LOGD(TAG, "pH: level=%.2f setpoint=%.2f dose_time=%d dose_vol=%d tank=%d",
             state->ph.level, state->ph.setpoint, state->ph.dose_time,
             state->ph.dose_volume, state->ph.tank_level);

    // ========================================================================
    // Parse ORP data
    // ========================================================================
    state->orp.level = (float)be16(payload, PAYLOAD_ORP_LEVEL);
    state->orp.setpoint = (float)be16(payload, PAYLOAD_ORP_SETPOINT);
    state->orp.dose_time = be16(payload, PAYLOAD_ORP_DOSE_TIME);
    state->orp.dose_volume = be16(payload, PAYLOAD_ORP_DOSE_VOLUME);
    state->orp.tank_level = parse_tank_level(payload[PAYLOAD_ORP_TANK_LEVEL]);

    ESP_LOGD(TAG, "ORP: level=%.0f setpoint=%.0f dose_time=%d dose_vol=%d tank=%d",
             state->orp.level, state->orp.setpoint, state->orp.dose_time,
             state->orp.dose_volume, state->orp.tank_level);

    // ========================================================================
    // Parse dosing status (byte 34)
    // ========================================================================
    uint8_t dosing_byte = payload[PAYLOAD_DOSING_STATUS];
    uint8_t ph_doser_type = dosing_byte & DOSING_PH_TYPE_MASK;
    uint8_t orp_doser_type = (dosing_byte & DOSING_ORP_TYPE_MASK) >> 2;
    uint8_t ph_dosing_raw = (dosing_byte & DOSING_PH_STATUS_MASK) >> 4;
    uint8_t orp_dosing_raw = (dosing_byte & DOSING_ORP_STATUS_MASK) >> 6;

    state->ph.dosing_status = parse_dosing_status(ph_dosing_raw);
    state->orp.dosing_status = parse_dosing_status(orp_dosing_raw);

    // Dosing active if status is DOSING and doser type is configured
    state->ph.is_dosing = (state->ph.dosing_status == DOSING_STATUS_DOSING && ph_doser_type != 0);
    state->orp.is_dosing = (state->orp.dosing_status == DOSING_STATUS_DOSING && orp_doser_type != 0);

    ESP_LOGD(TAG, "Dosing: ph_status=%s orp_status=%s ph_active=%d orp_active=%d",
             dosing_status_str(state->ph.dosing_status),
             dosing_status_str(state->orp.dosing_status),
             state->ph.is_dosing, state->orp.is_dosing);

    // ========================================================================
    // Parse water chemistry values
    // ========================================================================
    state->lsi = parse_lsi(payload[PAYLOAD_LSI]);
    state->calcium_hardness = be16(payload, PAYLOAD_CALCIUM);
    state->cyanuric_acid = payload[PAYLOAD_CYA];
    state->alkalinity = be16(payload, PAYLOAD_ALKALINITY);
    state->salt_level = payload[PAYLOAD_SALT] * 50;
    state->temperature = payload[PAYLOAD_TEMPERATURE];

    ESP_LOGD(TAG, "Chemistry: LSI=%.2f Ca=%d CYA=%d Alk=%d Salt=%d Temp=%d",
             state->lsi, state->calcium_hardness, state->cyanuric_acid,
             state->alkalinity, state->salt_level, state->temperature);

    // ========================================================================
    // Parse alarms (byte 32)
    // ========================================================================
    uint8_t alarm_byte = payload[PAYLOAD_ALARMS];
    state->alarms.flow = (alarm_byte & ALARM_FLOW) != 0;
    state->alarms.ph_tank_empty = (alarm_byte & ALARM_PH_TANK_EMPTY) != 0;
    state->alarms.orp_tank_empty = (alarm_byte & ALARM_ORP_TANK_EMPTY) != 0;
    state->alarms.probe_fault = (alarm_byte & ALARM_PROBE_FAULT) != 0;

    ESP_LOGD(TAG, "Alarms: flow=%d ph_empty=%d orp_empty=%d probe=%d",
             state->alarms.flow, state->alarms.ph_tank_empty,
             state->alarms.orp_tank_empty, state->alarms.probe_fault);

    // ========================================================================
    // Parse warnings (byte 33)
    // ========================================================================
    uint8_t warning_byte = payload[PAYLOAD_WARNINGS];
    state->warnings.ph_lockout = (warning_byte & WARNING_PH_LOCKOUT) != 0;
    state->warnings.ph_daily_limit = (warning_byte & WARNING_PH_DAILY_LIMIT) != 0;
    state->warnings.orp_daily_limit = (warning_byte & WARNING_ORP_DAILY_LIMIT) != 0;
    state->warnings.invalid_setup = (warning_byte & WARNING_INVALID_SETUP) != 0;
    state->warnings.chlorinator_comm_error = (warning_byte & WARNING_CHLORINATOR_COMM) != 0;

    // Water chemistry warning (byte 38)
    uint8_t water_chem_byte = payload[PAYLOAD_WATER_CHEMISTRY];
    if (water_chem_byte > 2) water_chem_byte = 2;
    state->warnings.water_chemistry = (water_chemistry_t)water_chem_byte;

    ESP_LOGD(TAG, "Warnings: ph_lock=%d ph_limit=%d orp_limit=%d invalid=%d chlor=%d water=%s",
             state->warnings.ph_lockout, state->warnings.ph_daily_limit,
             state->warnings.orp_daily_limit, state->warnings.invalid_setup,
             state->warnings.chlorinator_comm_error,
             water_chemistry_str(state->warnings.water_chemistry));

    // ========================================================================
    // Parse firmware version (bytes 36-37)
    // ========================================================================
    snprintf(state->firmware, sizeof(state->firmware), "%d.%03d",
             payload[PAYLOAD_FIRMWARE_MAJOR], payload[PAYLOAD_FIRMWARE_MINOR]);

    ESP_LOGD(TAG, "Firmware: %s", state->firmware);

    // ========================================================================
    // Parse status flags (byte 35)
    // ========================================================================
    uint8_t status_byte = payload[PAYLOAD_STATUS_FLAGS];
    state->comms_lost = (status_byte & STATUS_COMMS_LOST) != 0;

    // Flow detected is inverse of flow alarm
    state->flow_detected = !state->alarms.flow;

    ESP_LOGI(TAG, "Status parsed: pH=%.2f ORP=%.0fmV temp=%d°F fw=%s",
             state->ph.level, state->orp.level, state->temperature, state->firmware);

    return true;
}

void parser_log_state(const intellichem_state_t *state) {
    if (state == NULL) return;

    ESP_LOGI(TAG, "=== IntelliChem State (addr=0x%02X) ===", state->address);
    ESP_LOGI(TAG, "pH:  level=%.2f setpoint=%.2f tank=%d%% %s %s",
             state->ph.level, state->ph.setpoint,
             (int)tank_level_percent(state->ph.tank_level),
             dosing_status_str(state->ph.dosing_status),
             state->ph.is_dosing ? "[DOSING]" : "");
    ESP_LOGI(TAG, "ORP: level=%.0fmV setpoint=%.0fmV tank=%d%% %s %s",
             state->orp.level, state->orp.setpoint,
             (int)tank_level_percent(state->orp.tank_level),
             dosing_status_str(state->orp.dosing_status),
             state->orp.is_dosing ? "[DOSING]" : "");
    ESP_LOGI(TAG, "Chemistry: LSI=%.2f Ca=%dppm CYA=%dppm Alk=%dppm Salt=%dppm",
             state->lsi, state->calcium_hardness, state->cyanuric_acid,
             state->alkalinity, state->salt_level);
    ESP_LOGI(TAG, "Temperature: %d°F  Firmware: %s  Flow: %s",
             state->temperature, state->firmware,
             state->flow_detected ? "OK" : "ALARM");

    if (alarms_any_active(&state->alarms)) {
        ESP_LOGW(TAG, "ALARMS: %s%s%s%s",
                 state->alarms.flow ? "FLOW " : "",
                 state->alarms.ph_tank_empty ? "PH_EMPTY " : "",
                 state->alarms.orp_tank_empty ? "ORP_EMPTY " : "",
                 state->alarms.probe_fault ? "PROBE " : "");
    }

    if (warnings_any_active(&state->warnings)) {
        ESP_LOGW(TAG, "WARNINGS: %s%s%s%s%s%s",
                 state->warnings.ph_lockout ? "PH_LOCK " : "",
                 state->warnings.ph_daily_limit ? "PH_LIMIT " : "",
                 state->warnings.orp_daily_limit ? "ORP_LIMIT " : "",
                 state->warnings.invalid_setup ? "INVALID " : "",
                 state->warnings.chlorinator_comm_error ? "CHLOR_COMM " : "",
                 state->warnings.water_chemistry != WATER_CHEMISTRY_OK ?
                     water_chemistry_str(state->warnings.water_chemistry) : "");
    }
}
