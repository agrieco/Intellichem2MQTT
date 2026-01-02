/**
 * @file debug_log.c
 * @brief Ring buffer log capture implementation
 */

#include "debug_log.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "sdkconfig.h"

// ============================================================================
// Configuration
// ============================================================================

#ifndef CONFIG_DEBUG_LOG_BUFFER_SIZE
#define CONFIG_DEBUG_LOG_BUFFER_SIZE 4096
#endif

// Calculate number of entries that fit in buffer
#define LOG_ENTRY_SIZE sizeof(debug_log_entry_t)
#define LOG_BUFFER_ENTRIES (CONFIG_DEBUG_LOG_BUFFER_SIZE / LOG_ENTRY_SIZE)

// Mutex timeout for log capture (short to avoid blocking logging)
#define LOG_MUTEX_TIMEOUT_MS 5

// ============================================================================
// State
// ============================================================================

static debug_log_entry_t *s_entries = NULL;
static uint16_t s_capacity = 0;
static uint16_t s_head = 0;         // Next write position
static uint16_t s_count = 0;        // Current number of entries
static uint32_t s_dropped = 0;      // Entries dropped
static uint32_t s_total = 0;        // Total entries captured
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

// Original vprintf function (to chain calls)
static vprintf_like_t s_original_vprintf = NULL;

// ============================================================================
// Log Hook
// ============================================================================

/**
 * @brief Parse ESP_LOG format string to extract components
 *
 * ESP_LOG format: "X (%u) %s: %s\n" or similar
 * Where X is level char, %u is timestamp, first %s is tag, second %s is message
 */
static void parse_log_line(const char *str, debug_log_entry_t *entry)
{
    // Initialize entry
    entry->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    entry->level = ESP_LOG_INFO;
    entry->tag[0] = '\0';
    entry->message[0] = '\0';

    if (!str || strlen(str) < 3) {
        strncpy(entry->message, str ? str : "", sizeof(entry->message) - 1);
        return;
    }

    // Parse level character
    char level_char = str[0];
    switch (level_char) {
        case 'E': entry->level = ESP_LOG_ERROR; break;
        case 'W': entry->level = ESP_LOG_WARN; break;
        case 'I': entry->level = ESP_LOG_INFO; break;
        case 'D': entry->level = ESP_LOG_DEBUG; break;
        case 'V': entry->level = ESP_LOG_VERBOSE; break;
        default:
            // Not a standard ESP_LOG format, store whole line
            strncpy(entry->message, str, sizeof(entry->message) - 1);
            entry->message[sizeof(entry->message) - 1] = '\0';
            return;
    }

    // Skip "X (" to find timestamp
    const char *p = str + 2;
    if (*p != '(') {
        strncpy(entry->message, str, sizeof(entry->message) - 1);
        return;
    }
    p++; // Skip '('

    // Skip timestamp digits and ") "
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == ')') p++;
    if (*p == ' ') p++;

    // Now p points to tag, find ": "
    const char *tag_start = p;
    const char *colon = strstr(p, ": ");
    if (colon) {
        size_t tag_len = colon - tag_start;
        if (tag_len >= sizeof(entry->tag)) {
            tag_len = sizeof(entry->tag) - 1;
        }
        strncpy(entry->tag, tag_start, tag_len);
        entry->tag[tag_len] = '\0';

        // Message starts after ": "
        const char *msg_start = colon + 2;
        strncpy(entry->message, msg_start, sizeof(entry->message) - 1);
        entry->message[sizeof(entry->message) - 1] = '\0';

        // Remove trailing newline
        size_t msg_len = strlen(entry->message);
        if (msg_len > 0 && entry->message[msg_len - 1] == '\n') {
            entry->message[msg_len - 1] = '\0';
        }
    } else {
        // No colon found, store rest as message
        strncpy(entry->message, p, sizeof(entry->message) - 1);
        entry->message[sizeof(entry->message) - 1] = '\0';
    }
}

/**
 * @brief Custom vprintf that captures logs to ring buffer
 */
static int debug_log_vprintf(const char *fmt, va_list args)
{
    // Always call original first to maintain normal logging
    int ret = 0;
    if (s_original_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = s_original_vprintf(fmt, args_copy);
        va_end(args_copy);
    }

    // Try to capture to ring buffer (non-blocking)
    if (s_initialized && s_mutex) {
        // Format the log message
        char line[256];
        vsnprintf(line, sizeof(line), fmt, args);

        // Try to acquire mutex with short timeout
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(LOG_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            // Parse and store entry
            debug_log_entry_t *entry = &s_entries[s_head];
            parse_log_line(line, entry);

            // Advance head
            s_head = (s_head + 1) % s_capacity;
            if (s_count < s_capacity) {
                s_count++;
            }
            s_total++;

            xSemaphoreGive(s_mutex);
        } else {
            // Couldn't acquire mutex, drop this entry
            s_dropped++;
        }
    }

    return ret;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t debug_log_init(void)
{
    if (s_initialized) {
        return ESP_OK;  // Already initialized
    }

    // Allocate entry array
    s_capacity = LOG_BUFFER_ENTRIES;
    s_entries = calloc(s_capacity, sizeof(debug_log_entry_t));
    if (!s_entries) {
        return ESP_ERR_NO_MEM;
    }

    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        free(s_entries);
        s_entries = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Reset counters
    s_head = 0;
    s_count = 0;
    s_dropped = 0;
    s_total = 0;

    // Install log hook
    s_original_vprintf = esp_log_set_vprintf(debug_log_vprintf);

    s_initialized = true;

    // Log that we're initialized (this will be captured!)
    ESP_LOGI("debug_log", "Initialized with %u entry buffer (%u bytes)",
             s_capacity, CONFIG_DEBUG_LOG_BUFFER_SIZE);

    return ESP_OK;
}

void debug_log_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    // Restore original vprintf
    if (s_original_vprintf) {
        esp_log_set_vprintf(s_original_vprintf);
        s_original_vprintf = NULL;
    }

    s_initialized = false;

    // Free resources
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    if (s_entries) {
        free(s_entries);
        s_entries = NULL;
    }
}

void debug_log_clear(void)
{
    if (!s_initialized || !s_mutex) {
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_head = 0;
        s_count = 0;
        // Note: don't reset s_dropped or s_total - keep for stats
        xSemaphoreGive(s_mutex);
    }
}

void debug_log_get_stats(debug_log_stats_t *stats)
{
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(*stats));

    if (!s_initialized || !s_mutex) {
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        stats->capacity = s_capacity;
        stats->count = s_count;
        stats->dropped_count = s_dropped;
        stats->total_captured = s_total;

        if (s_count > 0) {
            // Oldest entry is at (head - count) mod capacity
            uint16_t oldest_idx = (s_head + s_capacity - s_count) % s_capacity;
            uint16_t newest_idx = (s_head + s_capacity - 1) % s_capacity;
            stats->oldest_timestamp_ms = s_entries[oldest_idx].timestamp_ms;
            stats->newest_timestamp_ms = s_entries[newest_idx].timestamp_ms;
        }

        xSemaphoreGive(s_mutex);
    }
}

size_t debug_log_read_plain(char *buf, size_t buf_size)
{
    static const char level_chars[] = "?EWID V";  // Index by esp_log_level_t

    if (!buf || buf_size == 0) {
        return 0;
    }

    buf[0] = '\0';

    if (!s_initialized || !s_mutex || s_count == 0) {
        return 0;
    }

    size_t written = 0;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Iterate from oldest to newest
        uint16_t start_idx = (s_head + s_capacity - s_count) % s_capacity;

        for (uint16_t i = 0; i < s_count && written < buf_size - 100; i++) {
            uint16_t idx = (start_idx + i) % s_capacity;
            debug_log_entry_t *entry = &s_entries[idx];

            // Convert ms to HH:MM:SS.mmm format
            uint32_t ms = entry->timestamp_ms;
            uint32_t total_sec = ms / 1000;
            uint32_t hours = total_sec / 3600;
            uint32_t mins = (total_sec % 3600) / 60;
            uint32_t secs = total_sec % 60;
            uint32_t millis = ms % 1000;

            // Get level char safely
            char level_char = (entry->level < sizeof(level_chars)) ?
                              level_chars[entry->level] : '?';

            int n = snprintf(buf + written, buf_size - written,
                             "[%02lu:%02lu:%02lu.%03lu] %c %-12s: %s\n",
                             (unsigned long)hours,
                             (unsigned long)mins,
                             (unsigned long)secs,
                             (unsigned long)millis,
                             level_char,
                             entry->tag,
                             entry->message);

            if (n > 0) {
                written += n;
            }
        }

        xSemaphoreGive(s_mutex);
    }

    return written;
}

size_t debug_log_read_json(char *buf, size_t buf_size)
{
    static const char level_chars[] = "?EWID V";  // Index by esp_log_level_t

    if (!buf || buf_size == 0) {
        return 0;
    }

    buf[0] = '\0';

    if (!s_initialized || !s_mutex) {
        snprintf(buf, buf_size, "[]");
        return 2;
    }

    size_t written = 0;
    written += snprintf(buf + written, buf_size - written, "[");

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint16_t start_idx = (s_head + s_capacity - s_count) % s_capacity;
        bool first = true;

        for (uint16_t i = 0; i < s_count && written < buf_size - 200; i++) {
            uint16_t idx = (start_idx + i) % s_capacity;
            debug_log_entry_t *entry = &s_entries[idx];

            // Escape message for JSON (simple: replace " with ')
            char escaped_msg[sizeof(entry->message)];
            strncpy(escaped_msg, entry->message, sizeof(escaped_msg) - 1);
            escaped_msg[sizeof(escaped_msg) - 1] = '\0';
            for (char *p = escaped_msg; *p; p++) {
                if (*p == '"') *p = '\'';
                if (*p == '\\') *p = '/';
                if (*p < 32) *p = ' ';  // Replace control chars
            }

            // Convert ms to HH:MM:SS.mmm format
            uint32_t ms = entry->timestamp_ms;
            uint32_t total_sec = ms / 1000;
            uint32_t hours = total_sec / 3600;
            uint32_t mins = (total_sec % 3600) / 60;
            uint32_t secs = total_sec % 60;
            uint32_t millis = ms % 1000;

            // Get level char safely
            char level_char = (entry->level < sizeof(level_chars)) ?
                              level_chars[entry->level] : '?';

            int n = snprintf(buf + written, buf_size - written,
                             "%s{\"time\":\"%02lu:%02lu:%02lu.%03lu\",\"level\":\"%c\",\"tag\":\"%s\",\"msg\":\"%s\"}",
                             first ? "" : ",",
                             (unsigned long)hours,
                             (unsigned long)mins,
                             (unsigned long)secs,
                             (unsigned long)millis,
                             level_char,
                             entry->tag,
                             escaped_msg);

            if (n > 0) {
                written += n;
                first = false;
            }
        }

        xSemaphoreGive(s_mutex);
    }

    written += snprintf(buf + written, buf_size - written, "]");

    return written;
}

bool debug_log_is_initialized(void)
{
    return s_initialized;
}

char debug_log_level_char(uint8_t level)
{
    switch (level) {
        case ESP_LOG_ERROR:   return 'E';
        case ESP_LOG_WARN:    return 'W';
        case ESP_LOG_INFO:    return 'I';
        case ESP_LOG_DEBUG:   return 'D';
        case ESP_LOG_VERBOSE: return 'V';
        default:              return '?';
    }
}
