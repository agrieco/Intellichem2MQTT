/**
 * @file message.c
 * @brief Packet building and validation implementation
 */

#include "message.h"
#include "constants.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "message";

// ============================================================================
// Packet Building
// ============================================================================

size_t message_build(uint8_t *buf, size_t buf_size,
                     uint8_t dest, uint8_t src, uint8_t action,
                     const uint8_t *payload, uint8_t payload_len)
{
    // Calculate required size
    size_t required = PREAMBLE_LENGTH + HEADER_LENGTH + payload_len + CHECKSUM_LENGTH;

    if (buf == NULL || buf_size < required) {
        ESP_LOGE(TAG, "Buffer too small: need %zu, have %zu", required, buf_size);
        return 0;
    }

    size_t offset = 0;

    // Preamble
    buf[offset++] = PREAMBLE_BYTE_1;
    buf[offset++] = PREAMBLE_BYTE_2;
    buf[offset++] = PREAMBLE_BYTE_3;

    // Header
    buf[offset++] = HEADER_START_BYTE;
    buf[offset++] = HEADER_SUB_BYTE;
    buf[offset++] = dest;
    buf[offset++] = src;
    buf[offset++] = action;
    buf[offset++] = payload_len;

    // Payload
    if (payload_len > 0 && payload != NULL) {
        memcpy(&buf[offset], payload, payload_len);
        offset += payload_len;
    }

    // Calculate checksum (sum of header + payload)
    uint16_t checksum = message_calculate_checksum(&buf[PREAMBLE_LENGTH],
                                                    HEADER_LENGTH + payload_len);

    // Checksum (big-endian)
    buf[offset++] = (checksum >> 8) & 0xFF;
    buf[offset++] = checksum & 0xFF;

    ESP_LOGD(TAG, "Built packet: dest=0x%02X src=0x%02X action=%d len=%d checksum=0x%04X",
             dest, src, action, payload_len, checksum);

    return offset;
}

uint16_t message_calculate_checksum(const uint8_t *header_and_payload, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += header_and_payload[i];
    }
    return sum;
}

// ============================================================================
// Packet Validation
// ============================================================================

bool message_validate_checksum(const uint8_t *packet, size_t len)
{
    if (packet == NULL || len < MIN_PACKET_SIZE) {
        ESP_LOGW(TAG, "Packet too short for checksum validation: %zu bytes", len);
        return false;
    }

    // Get payload length from header
    uint8_t payload_len = packet[PKT_OFFSET_LENGTH];
    size_t expected_len = message_total_length(payload_len);

    if (len < expected_len) {
        ESP_LOGW(TAG, "Packet length mismatch: expected %zu, got %zu", expected_len, len);
        return false;
    }

    // Calculate checksum of header + payload (skip preamble, exclude checksum bytes)
    size_t data_len = HEADER_LENGTH + payload_len;
    uint16_t calculated = message_calculate_checksum(&packet[PREAMBLE_LENGTH], data_len);

    // Extract received checksum (last 2 bytes, big-endian)
    uint16_t received = BE16(packet, len - 2);

    if (calculated != received) {
        ESP_LOGW(TAG, "Checksum mismatch: calculated=0x%04X received=0x%04X",
                 calculated, received);
        return false;
    }

    ESP_LOGD(TAG, "Checksum valid: 0x%04X", calculated);
    return true;
}

bool message_validate_structure(const uint8_t *packet, size_t len)
{
    if (packet == NULL || len < MIN_PACKET_SIZE) {
        return false;
    }

    // Check preamble
    if (packet[0] != PREAMBLE_BYTE_1 ||
        packet[1] != PREAMBLE_BYTE_2 ||
        packet[2] != PREAMBLE_BYTE_3) {
        ESP_LOGD(TAG, "Invalid preamble: %02X %02X %02X",
                 packet[0], packet[1], packet[2]);
        return false;
    }

    // Check header start byte
    if (packet[PKT_OFFSET_START_BYTE] != HEADER_START_BYTE) {
        ESP_LOGD(TAG, "Invalid header start byte: 0x%02X",
                 packet[PKT_OFFSET_START_BYTE]);
        return false;
    }

    return true;
}

// ============================================================================
// Packet Field Extraction
// ============================================================================

uint8_t message_get_action(const uint8_t *packet)
{
    if (packet == NULL) return 0;
    return packet[PKT_OFFSET_ACTION];
}

uint8_t message_get_source(const uint8_t *packet)
{
    if (packet == NULL) return 0;
    return packet[PKT_OFFSET_SRC];
}

uint8_t message_get_dest(const uint8_t *packet)
{
    if (packet == NULL) return 0;
    return packet[PKT_OFFSET_DEST];
}

uint8_t message_get_payload_len(const uint8_t *packet)
{
    if (packet == NULL) return 0;
    return packet[PKT_OFFSET_LENGTH];
}

const uint8_t* message_get_payload(const uint8_t *packet)
{
    if (packet == NULL) return NULL;
    return &packet[PKT_OFFSET_PAYLOAD];
}
