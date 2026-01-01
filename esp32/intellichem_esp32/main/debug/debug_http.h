/**
 * @file debug_http.h
 * @brief HTTP debug endpoints for remote monitoring
 *
 * Provides HTTP endpoints to retrieve system stats and
 * captured log entries for remote debugging.
 *
 * Endpoints:
 *   GET  /debug/stats      - System statistics (JSON)
 *   GET  /debug/logs       - Ring buffer logs (plain text or JSON)
 *   POST /debug/logs/clear - Clear log buffer
 */

#ifndef DEBUG_HTTP_H
#define DEBUG_HTTP_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the debug HTTP server
 *
 * Creates a new HTTP server (or uses existing one) and registers
 * debug endpoint handlers.
 *
 * @param existing_server Existing httpd handle to add handlers to,
 *                        or NULL to create a new server
 * @return ESP_OK on success
 */
esp_err_t debug_http_start(httpd_handle_t existing_server);

/**
 * @brief Stop the debug HTTP server
 *
 * Only stops the server if it was created by debug_http_start().
 * If handlers were registered on an existing server, they remain.
 */
void debug_http_stop(void);

/**
 * @brief Check if debug HTTP server is running
 *
 * @return true if server is running and handling requests
 */
bool debug_http_is_running(void);

/**
 * @brief Get the HTTP server handle
 *
 * @return httpd_handle_t or NULL if not running
 */
httpd_handle_t debug_http_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_HTTP_H
