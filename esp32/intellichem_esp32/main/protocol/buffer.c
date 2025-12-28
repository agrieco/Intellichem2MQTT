/**
 * @file buffer.c
 * @brief Packet buffer implementation
 */

#include "buffer.h"
#include "constants.h"
#include "message.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "buffer";

// ============================================================================
// Internal Helpers
// ============================================================================

/**
 * @brief Read byte at offset from ring buffer (non-destructive)
 */
static inline uint8_t buffer_peek_at(const packet_buffer_t *buf, size_t offset) {
    size_t idx = (buf->tail + offset) % PACKET_BUFFER_CAPACITY;
    return buf->data[idx];
}

/**
 * @brief Copy bytes from ring buffer to linear buffer
 */
static void buffer_copy_out(const packet_buffer_t *buf, uint8_t *dest,
                            size_t offset, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dest[i] = buffer_peek_at(buf, offset + i);
    }
}

/**
 * @brief Discard bytes from the front of the buffer
 */
static void buffer_discard(packet_buffer_t *buf, size_t count) {
    if (count > buf->count) {
        count = buf->count;
    }
    buf->tail = (buf->tail + count) % PACKET_BUFFER_CAPACITY;
    buf->count -= count;
}

/**
 * @brief Find preamble in buffer starting from offset
 * @return Offset of preamble start, or -1 if not found
 */
static int buffer_find_preamble(const packet_buffer_t *buf, size_t start_offset) {
    if (buf->count < start_offset + PREAMBLE_LENGTH) {
        return -1;
    }

    for (size_t i = start_offset; i <= buf->count - PREAMBLE_LENGTH; i++) {
        if (buffer_peek_at(buf, i) == PREAMBLE_BYTE_1 &&
            buffer_peek_at(buf, i + 1) == PREAMBLE_BYTE_2 &&
            buffer_peek_at(buf, i + 2) == PREAMBLE_BYTE_3) {
            return (int)i;
        }
    }
    return -1;
}

// ============================================================================
// Buffer Operations
// ============================================================================

void buffer_init(packet_buffer_t *buf) {
    if (buf == NULL) return;

    memset(buf->data, 0, PACKET_BUFFER_CAPACITY);
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;

    memset(&buf->stats, 0, sizeof(buffer_stats_t));

    ESP_LOGI(TAG, "Buffer initialized, capacity=%d bytes", PACKET_BUFFER_CAPACITY);
}

void buffer_add_bytes(packet_buffer_t *buf, const uint8_t *data, size_t len) {
    if (buf == NULL || data == NULL || len == 0) return;

    buf->stats.bytes_received += len;

    // Check for overflow
    if (buf->count + len > PACKET_BUFFER_CAPACITY) {
        ESP_LOGW(TAG, "Buffer overflow, clearing old data. Had %zu, adding %zu",
                 buf->count, len);
        buf->stats.buffer_overflows++;

        // Keep only the last 64 bytes before adding new data
        if (buf->count > 64) {
            buffer_discard(buf, buf->count - 64);
        }
    }

    // Add bytes to ring buffer
    for (size_t i = 0; i < len; i++) {
        buf->data[buf->head] = data[i];
        buf->head = (buf->head + 1) % PACKET_BUFFER_CAPACITY;
        if (buf->count < PACKET_BUFFER_CAPACITY) {
            buf->count++;
        } else {
            // Overwrite oldest byte
            buf->tail = (buf->tail + 1) % PACKET_BUFFER_CAPACITY;
        }
    }

    ESP_LOGV(TAG, "Added %zu bytes, buffer now has %zu bytes", len, buf->count);
}

bool buffer_get_packet(packet_buffer_t *buf,
                       uint8_t *packet_out, size_t packet_out_size,
                       size_t *packet_len_out) {
    if (buf == NULL || packet_out == NULL || packet_len_out == NULL) {
        return false;
    }

    while (buf->count >= MIN_PACKET_SIZE) {
        // Find preamble
        int preamble_idx = buffer_find_preamble(buf, 0);

        if (preamble_idx < 0) {
            // No preamble found, keep only last 2 bytes (might be partial preamble)
            if (buf->count > 2) {
                size_t discard = buf->count - 2;
                ESP_LOGD(TAG, "No preamble found, discarding %zu bytes", discard);
                buffer_discard(buf, discard);
            }
            return false;
        }

        // Discard bytes before preamble
        if (preamble_idx > 0) {
            ESP_LOGD(TAG, "Discarding %d bytes before preamble", preamble_idx);
            buffer_discard(buf, preamble_idx);
            buf->stats.preamble_syncs++;
        }

        // Need at least MIN_PACKET_SIZE bytes
        if (buf->count < MIN_PACKET_SIZE) {
            return false;
        }

        // Validate header start byte
        if (buffer_peek_at(buf, PKT_OFFSET_START_BYTE) != HEADER_START_BYTE) {
            ESP_LOGD(TAG, "Invalid header start byte 0x%02X, skipping preamble",
                     buffer_peek_at(buf, PKT_OFFSET_START_BYTE));
            buffer_discard(buf, 1);
            continue;
        }

        // Get payload length from header
        uint8_t payload_len = buffer_peek_at(buf, PKT_OFFSET_LENGTH);
        size_t packet_len = message_total_length(payload_len);

        // Sanity check on packet length
        if (packet_len > MAX_PACKET_SIZE) {
            ESP_LOGW(TAG, "Packet length %zu exceeds max %d, skipping",
                     packet_len, MAX_PACKET_SIZE);
            buffer_discard(buf, 1);
            continue;
        }

        // Wait for complete packet
        if (buf->count < packet_len) {
            ESP_LOGV(TAG, "Waiting for complete packet: have %zu, need %zu",
                     buf->count, packet_len);
            return false;
        }

        // Check output buffer size
        if (packet_out_size < packet_len) {
            ESP_LOGE(TAG, "Output buffer too small: need %zu, have %zu",
                     packet_len, packet_out_size);
            return false;
        }

        // Copy packet to output buffer
        buffer_copy_out(buf, packet_out, 0, packet_len);

        // Validate checksum
        if (message_validate_checksum(packet_out, packet_len)) {
            // Valid packet - remove from buffer and return
            buffer_discard(buf, packet_len);
            buf->stats.packets_received++;
            *packet_len_out = packet_len;

            ESP_LOGI(TAG, "Valid packet received: action=%d len=%zu",
                     message_get_action(packet_out), packet_len);
            ESP_LOG_BUFFER_HEXDUMP(TAG, packet_out, packet_len, ESP_LOG_DEBUG);

            return true;
        } else {
            // Invalid checksum - skip this preamble and try again
            ESP_LOGD(TAG, "Invalid checksum, skipping preamble");
            buf->stats.invalid_checksums++;
            buffer_discard(buf, 1);
            continue;
        }
    }

    return false;
}

void buffer_clear(packet_buffer_t *buf) {
    if (buf == NULL) return;

    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;

    ESP_LOGD(TAG, "Buffer cleared");
}

size_t buffer_pending_bytes(const packet_buffer_t *buf) {
    if (buf == NULL) return 0;
    return buf->count;
}

const buffer_stats_t* buffer_get_stats(const packet_buffer_t *buf) {
    if (buf == NULL) return NULL;
    return &buf->stats;
}

void buffer_log_stats(const packet_buffer_t *buf) {
    if (buf == NULL) return;

    ESP_LOGI(TAG, "Buffer stats: packets=%lu bytes=%lu invalid_chk=%lu overflows=%lu syncs=%lu",
             (unsigned long)buf->stats.packets_received,
             (unsigned long)buf->stats.bytes_received,
             (unsigned long)buf->stats.invalid_checksums,
             (unsigned long)buf->stats.buffer_overflows,
             (unsigned long)buf->stats.preamble_syncs);
}
