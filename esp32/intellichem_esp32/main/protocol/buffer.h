/**
 * @file buffer.h
 * @brief Packet buffer for RS-485 byte stream assembly
 *
 * Direct port from Python intellichem2mqtt/serial/buffer.py
 */

#ifndef PROTOCOL_BUFFER_H
#define PROTOCOL_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// Buffer Configuration
// ============================================================================

#define PACKET_BUFFER_CAPACITY  512     // Maximum bytes to buffer
#define PACKET_BUFFER_OVERFLOW  4096    // Trigger overflow cleanup

// ============================================================================
// Buffer Statistics
// ============================================================================

/**
 * @brief Buffer statistics for monitoring
 */
typedef struct {
    uint32_t packets_received;
    uint32_t bytes_received;
    uint32_t invalid_checksums;
    uint32_t buffer_overflows;
    uint32_t preamble_syncs;
} buffer_stats_t;

// ============================================================================
// Packet Buffer
// ============================================================================

/**
 * @brief Ring buffer for assembling packets from byte stream
 */
typedef struct {
    uint8_t data[PACKET_BUFFER_CAPACITY];
    size_t head;            // Write position
    size_t tail;            // Read position
    size_t count;           // Bytes in buffer
    buffer_stats_t stats;
} packet_buffer_t;

// ============================================================================
// Buffer Operations
// ============================================================================

/**
 * @brief Initialize a packet buffer
 *
 * @param buf Pointer to buffer structure
 */
void buffer_init(packet_buffer_t *buf);

/**
 * @brief Add received bytes to the buffer
 *
 * @param buf Pointer to buffer structure
 * @param data Bytes received from serial port
 * @param len Number of bytes
 */
void buffer_add_bytes(packet_buffer_t *buf, const uint8_t *data, size_t len);

/**
 * @brief Try to extract a complete packet from the buffer
 *
 * Searches for preamble, validates structure and checksum,
 * and extracts complete packets.
 *
 * @param buf Pointer to buffer structure
 * @param packet_out Output buffer for extracted packet
 * @param packet_out_size Size of output buffer
 * @param packet_len_out Pointer to store extracted packet length
 * @return true if a valid packet was extracted
 */
bool buffer_get_packet(packet_buffer_t *buf,
                       uint8_t *packet_out, size_t packet_out_size,
                       size_t *packet_len_out);

/**
 * @brief Clear all data from the buffer
 *
 * @param buf Pointer to buffer structure
 */
void buffer_clear(packet_buffer_t *buf);

/**
 * @brief Get current number of bytes in buffer
 *
 * @param buf Pointer to buffer structure
 * @return Number of pending bytes
 */
size_t buffer_pending_bytes(const packet_buffer_t *buf);

/**
 * @brief Get buffer statistics
 *
 * @param buf Pointer to buffer structure
 * @return Pointer to statistics structure
 */
const buffer_stats_t* buffer_get_stats(const packet_buffer_t *buf);

/**
 * @brief Log buffer statistics
 *
 * @param buf Pointer to buffer structure
 */
void buffer_log_stats(const packet_buffer_t *buf);

#endif // PROTOCOL_BUFFER_H
