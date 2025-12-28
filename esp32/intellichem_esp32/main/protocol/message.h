/**
 * @file message.h
 * @brief Packet building and validation for IntelliChem protocol
 *
 * Direct port from Python intellichem2mqtt/protocol/message.py
 */

#ifndef PROTOCOL_MESSAGE_H
#define PROTOCOL_MESSAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// Packet Building
// ============================================================================

/**
 * @brief Build a complete packet with preamble, header, payload, and checksum
 *
 * Packet format:
 *   [FF 00 FF]                    - Preamble (3 bytes)
 *   [A5 00 DEST SRC ACTION LEN]   - Header (6 bytes)
 *   [...payload...]               - Payload (LEN bytes)
 *   [CHK_HI CHK_LO]               - Checksum (2 bytes)
 *
 * @param buf       Output buffer (must be at least 11 + payload_len bytes)
 * @param buf_size  Size of output buffer
 * @param dest      Destination address (144-158 for IntelliChem)
 * @param src       Source address (16 for controller)
 * @param action    Action code
 * @param payload   Payload bytes (can be NULL if payload_len is 0)
 * @param payload_len Length of payload
 * @return Size of built packet, or 0 on error
 */
size_t message_build(uint8_t *buf, size_t buf_size,
                     uint8_t dest, uint8_t src, uint8_t action,
                     const uint8_t *payload, uint8_t payload_len);

/**
 * @brief Calculate checksum of header + payload bytes
 *
 * Checksum is the 16-bit sum of all bytes from header start to end of payload.
 *
 * @param header_and_payload Pointer to header start (after preamble)
 * @param len Length of header + payload
 * @return 16-bit checksum
 */
uint16_t message_calculate_checksum(const uint8_t *header_and_payload, size_t len);

// ============================================================================
// Packet Validation
// ============================================================================

/**
 * @brief Validate a packet's checksum
 *
 * @param packet Complete packet including preamble and checksum
 * @param len Length of packet
 * @return true if checksum is valid
 */
bool message_validate_checksum(const uint8_t *packet, size_t len);

/**
 * @brief Check if packet has valid structure (preamble, header start byte)
 *
 * @param packet Packet bytes
 * @param len Length of packet
 * @return true if structure is valid
 */
bool message_validate_structure(const uint8_t *packet, size_t len);

// ============================================================================
// Packet Field Extraction
// ============================================================================

/**
 * @brief Extract action code from a packet
 *
 * @param packet Complete packet including preamble
 * @return Action code, or 0 if packet is too short
 */
uint8_t message_get_action(const uint8_t *packet);

/**
 * @brief Extract source address from a packet
 *
 * @param packet Complete packet including preamble
 * @return Source address, or 0 if packet is too short
 */
uint8_t message_get_source(const uint8_t *packet);

/**
 * @brief Extract destination address from a packet
 *
 * @param packet Complete packet including preamble
 * @return Destination address, or 0 if packet is too short
 */
uint8_t message_get_dest(const uint8_t *packet);

/**
 * @brief Extract payload length from a packet
 *
 * @param packet Complete packet including preamble
 * @return Payload length, or 0 if packet is too short
 */
uint8_t message_get_payload_len(const uint8_t *packet);

/**
 * @brief Get pointer to payload data in a packet
 *
 * @param packet Complete packet including preamble
 * @return Pointer to payload start, or NULL if packet is too short
 */
const uint8_t* message_get_payload(const uint8_t *packet);

/**
 * @brief Calculate total packet length from payload length
 *
 * @param payload_len Length of payload
 * @return Total packet length (preamble + header + payload + checksum)
 */
static inline size_t message_total_length(uint8_t payload_len) {
    return 3 + 6 + payload_len + 2;  // preamble + header + payload + checksum
}

// ============================================================================
// Helper Macros
// ============================================================================

// Byte offsets in complete packet (including preamble)
#define PKT_OFFSET_PREAMBLE     0
#define PKT_OFFSET_HEADER       3
#define PKT_OFFSET_START_BYTE   3   // 0xA5
#define PKT_OFFSET_SUB_BYTE     4   // 0x00
#define PKT_OFFSET_DEST         5
#define PKT_OFFSET_SRC          6
#define PKT_OFFSET_ACTION       7
#define PKT_OFFSET_LENGTH       8
#define PKT_OFFSET_PAYLOAD      9

// Extract big-endian 16-bit value from buffer
#define BE16(buf, offset) (((uint16_t)(buf)[(offset)] << 8) | (buf)[(offset) + 1])

#endif // PROTOCOL_MESSAGE_H
