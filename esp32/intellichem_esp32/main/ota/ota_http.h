/**
 * @file ota_http.h
 * @brief HTTP-based OTA firmware update
 */

#ifndef OTA_HTTP_H
#define OTA_HTTP_H

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Register OTA HTTP handlers on an existing server
 *
 * Adds the following endpoints:
 * - GET  /ota        - Upload form page
 * - POST /ota/upload - Firmware upload endpoint
 * - GET  /ota/status - OTA status
 *
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t ota_http_register_handlers(httpd_handle_t server);

/**
 * @brief Check if OTA is in progress
 * @return true if OTA update is currently running
 */
bool ota_http_is_updating(void);

#endif // OTA_HTTP_H
