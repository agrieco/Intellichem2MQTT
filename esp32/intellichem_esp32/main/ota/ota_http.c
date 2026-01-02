/**
 * @file ota_http.c
 * @brief HTTP-based OTA firmware update implementation
 */

#include "ota_http.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "ota_http";

static bool s_ota_in_progress = false;

// HTML page for OTA upload form
static const char OTA_HTML_PAGE[] =
    "<!DOCTYPE html>"
    "<html><head><title>IntelliChem2MQTT OTA Update</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#f5f5f5}"
    ".container{max-width:500px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
    "h1{color:#333;margin-top:0}"
    ".info{background:#e7f3ff;padding:10px;border-radius:4px;margin-bottom:20px}"
    "input[type=file]{margin:10px 0;padding:10px;border:2px dashed #ccc;width:100%%;box-sizing:border-box}"
    "button{background:#4CAF50;color:white;padding:12px 24px;border:none;border-radius:4px;cursor:pointer;width:100%%;font-size:16px}"
    "button:hover{background:#45a049}"
    "button:disabled{background:#ccc;cursor:not-allowed}"
    ".progress{display:none;margin-top:20px}"
    ".progress-bar{height:20px;background:#e0e0e0;border-radius:10px;overflow:hidden}"
    ".progress-fill{height:100%%;background:#4CAF50;width:0%%;transition:width 0.3s}"
    ".status{margin-top:10px;font-weight:bold}"
    ".warning{color:#f44336;margin-top:10px}"
    "</style></head>"
    "<body><div class='container'>"
    "<h1>Firmware Update</h1>"
    "<div class='info'>"
    "<strong>Current version:</strong> %s<br>"
    "<strong>Running partition:</strong> %s"
    "</div>"
    "<form id='uploadForm' enctype='multipart/form-data'>"
    "<input type='file' id='firmware' name='firmware' accept='.bin' required>"
    "<button type='submit' id='uploadBtn'>Upload Firmware</button>"
    "</form>"
    "<div class='progress' id='progress'>"
    "<div class='progress-bar'><div class='progress-fill' id='progressFill'></div></div>"
    "<div class='status' id='status'>Uploading...</div>"
    "</div>"
    "<p class='warning'>Warning: Do not disconnect power during update!</p>"
    "</div>"
    "<script>"
    "document.getElementById('uploadForm').onsubmit=function(e){"
    "e.preventDefault();"
    "var file=document.getElementById('firmware').files[0];"
    "if(!file)return;"
    "var xhr=new XMLHttpRequest();"
    "var form=new FormData();"
    "form.append('firmware',file);"
    "document.getElementById('uploadBtn').disabled=true;"
    "document.getElementById('progress').style.display='block';"
    "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){"
    "var pct=Math.round(e.loaded/e.total*100);"
    "document.getElementById('progressFill').style.width=pct+'%%';"
    "document.getElementById('status').textContent='Uploading: '+pct+'%%';"
    "}};"
    "xhr.onload=function(){"
    "if(xhr.status==200){"
    "document.getElementById('status').textContent='Update complete! Rebooting...';"
    "setTimeout(function(){location.reload();},5000);"
    "}else{"
    "document.getElementById('status').textContent='Error: '+xhr.responseText;"
    "document.getElementById('uploadBtn').disabled=false;"
    "}};"
    "xhr.onerror=function(){"
    "document.getElementById('status').textContent='Upload failed';"
    "document.getElementById('uploadBtn').disabled=false;"
    "};"
    "xhr.open('POST','/ota/upload',true);"
    "xhr.send(form);"
    "};"
    "</script></body></html>";

/**
 * @brief GET /ota - OTA upload page
 */
static esp_err_t ota_page_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    // Allocate on heap - httpd task has limited stack
    size_t buf_size = sizeof(OTA_HTML_PAGE) + 128;
    char *response = malloc(buf_size);
    if (!response) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int len = snprintf(response, buf_size, OTA_HTML_PAGE,
                       app_desc->version,
                       running->label);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, len);
    free(response);
    return ESP_OK;
}

/**
 * @brief POST /ota/upload - Handle firmware upload
 */
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    esp_err_t err;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;
    char buf[1024];
    int received;
    int total_received = 0;
    bool header_checked = false;

    ESP_LOGI(TAG, "OTA update started, content length: %d", req->content_len);

    if (s_ota_in_progress) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA already in progress");
        return ESP_FAIL;
    }

    s_ota_in_progress = true;

    // Get the next OTA partition
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%lx)",
             update_partition->label, update_partition->address);

    // Read the firmware data
    while (total_received < req->content_len) {
        received = httpd_req_recv(req, buf, MIN(sizeof(buf), req->content_len - total_received));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Receive error: %d", received);
            if (ota_handle) {
                esp_ota_abort(ota_handle);
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            s_ota_in_progress = false;
            return ESP_FAIL;
        }

        // Skip multipart form header on first chunk
        if (!header_checked) {
            // Find the binary data start (after \r\n\r\n)
            char *data_start = NULL;
            for (int i = 0; i < received - 3; i++) {
                if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                    data_start = &buf[i + 4];
                    break;
                }
            }

            if (data_start) {
                int header_len = data_start - buf;
                int data_len = received - header_len;

                // Check firmware magic
                if (data_len >= sizeof(esp_image_header_t)) {
                    esp_image_header_t *header = (esp_image_header_t *)data_start;
                    if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
                        ESP_LOGE(TAG, "Invalid firmware magic: 0x%02x", header->magic);
                        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware file");
                        s_ota_in_progress = false;
                        return ESP_FAIL;
                    }
                }

                // Begin OTA
                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
                    s_ota_in_progress = false;
                    return ESP_FAIL;
                }

                // Write first chunk (without header)
                err = esp_ota_write(ota_handle, data_start, data_len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                    esp_ota_abort(ota_handle);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
                    s_ota_in_progress = false;
                    return ESP_FAIL;
                }

                header_checked = true;
                total_received += received;
                ESP_LOGI(TAG, "OTA progress: %d bytes", total_received);
                continue;
            }
        }

        // Write subsequent chunks
        if (ota_handle) {
            // Check for multipart boundary at end
            int write_len = received;
            if (total_received + received >= req->content_len) {
                // Last chunk - find and remove trailing boundary
                for (int i = received - 1; i >= 0 && i > received - 50; i--) {
                    if (buf[i] == '-' && buf[i-1] == '-') {
                        // Found boundary marker, find line start
                        for (int j = i - 2; j >= 0; j--) {
                            if (buf[j] == '\n') {
                                write_len = j;
                                if (j > 0 && buf[j-1] == '\r') write_len = j - 1;
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            if (write_len > 0) {
                err = esp_ota_write(ota_handle, buf, write_len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                    esp_ota_abort(ota_handle);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
                    s_ota_in_progress = false;
                    return ESP_FAIL;
                }
            }
        }

        total_received += received;
        if (total_received % 102400 < 1024) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes", total_received, req->content_len);
        }
    }

    // Finish OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful! Rebooting...");
    httpd_resp_sendstr(req, "OK");

    s_ota_in_progress = false;

    // Reboot after short delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

/**
 * @brief GET /ota/status - OTA status
 */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    char response[256];
    int len = snprintf(response, sizeof(response),
        "{"
        "\"version\":\"%s\","
        "\"running_partition\":\"%s\","
        "\"next_partition\":\"%s\","
        "\"updating\":%s"
        "}",
        app_desc->version,
        running ? running->label : "unknown",
        next ? next->label : "unknown",
        s_ota_in_progress ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

esp_err_t ota_http_register_handlers(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Registering OTA HTTP handlers");

    // OTA page
    httpd_uri_t ota_page = {
        .uri = "/ota",
        .method = HTTP_GET,
        .handler = ota_page_handler,
    };
    httpd_register_uri_handler(server, &ota_page);

    // OTA upload
    httpd_uri_t ota_upload = {
        .uri = "/ota/upload",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
    };
    httpd_register_uri_handler(server, &ota_upload);

    // OTA status
    httpd_uri_t ota_status = {
        .uri = "/ota/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
    };
    httpd_register_uri_handler(server, &ota_status);

    ESP_LOGI(TAG, "OTA endpoints registered:");
    ESP_LOGI(TAG, "  GET  /ota        - Upload page");
    ESP_LOGI(TAG, "  POST /ota/upload - Firmware upload");
    ESP_LOGI(TAG, "  GET  /ota/status - Status JSON");

    return ESP_OK;
}

bool ota_http_is_updating(void)
{
    return s_ota_in_progress;
}
