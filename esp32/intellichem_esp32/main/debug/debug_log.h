/**
 * @file debug_log.h
 * @brief Ring buffer log capture for remote debugging
 *
 * Captures ESP_LOG output into a ring buffer that can be
 * retrieved via HTTP endpoints for remote debugging.
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "esp_err.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log entry structure stored in ring buffer
 */
typedef struct {
    uint32_t timestamp_ms;      /**< Milliseconds since boot */
    uint8_t level;              /**< ESP_LOG_* level */
    char tag[16];               /**< Log tag (truncated if necessary) */
    char message[112];          /**< Log message (truncated if necessary) */
} debug_log_entry_t;            // ~132 bytes per entry

/**
 * @brief Ring buffer statistics
 */
typedef struct {
    uint16_t capacity;              /**< Maximum number of entries */
    uint16_t count;                 /**< Current number of entries */
    uint32_t oldest_timestamp_ms;   /**< Timestamp of oldest entry */
    uint32_t newest_timestamp_ms;   /**< Timestamp of newest entry */
    uint32_t dropped_count;         /**< Entries dropped (buffer full, couldn't acquire lock) */
    uint32_t total_captured;        /**< Total entries ever captured */
} debug_log_stats_t;

/**
 * @brief Initialize debug log capture
 *
 * Allocates ring buffer and installs ESP_LOG hook.
 * Safe to call multiple times (subsequent calls are no-op).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t debug_log_init(void);

/**
 * @brief Deinitialize and free resources
 *
 * Removes log hook and frees ring buffer.
 */
void debug_log_deinit(void);

/**
 * @brief Clear the ring buffer
 *
 * Removes all captured log entries.
 */
void debug_log_clear(void);

/**
 * @brief Get buffer statistics
 *
 * @param[out] stats Pointer to stats structure to fill
 */
void debug_log_get_stats(debug_log_stats_t *stats);

/**
 * @brief Read logs into buffer as plain text
 *
 * Formats log entries as human-readable text:
 * "[timestamp] LEVEL TAG: message\n"
 *
 * @param[out] buf Output buffer
 * @param buf_size Size of output buffer
 * @return Number of bytes written (excluding null terminator)
 */
size_t debug_log_read_plain(char *buf, size_t buf_size);

/**
 * @brief Read logs into buffer as JSON array
 *
 * Formats log entries as JSON:
 * [{"ts":123,"level":"I","tag":"xxx","msg":"yyy"}, ...]
 *
 * @param[out] buf Output buffer
 * @param buf_size Size of output buffer
 * @return Number of bytes written (excluding null terminator)
 */
size_t debug_log_read_json(char *buf, size_t buf_size);

/**
 * @brief Check if debug logging is initialized
 *
 * @return true if initialized and capturing logs
 */
bool debug_log_is_initialized(void);

/**
 * @brief Get level character for ESP_LOG level
 *
 * @param level ESP_LOG_* level value
 * @return Single character: E, W, I, D, V, or ?
 */
char debug_log_level_char(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_LOG_H
