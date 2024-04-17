/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2022 Mooneer Salem
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string>
#include <cstring>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "HttpServerTask.h"
#include "NetworkMessage.h"

extern "C"
{
    DV_EVENT_DEFINE_BASE(HTTP_SERVER_MESSAGE);
}

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE 4096
#define CURRENT_LOG_TAG "HttpServerTask"

namespace ezdv
{

namespace network
{

HttpServerTask::HttpServerTask()
    : ezdv::task::DVTask("HttpServerTask", 4, 4096, tskNO_AFFINITY, 256)
    , isRunning_(false)
{
    // HTTP handlers called from web socket
    registerMessageHandler(this, &HttpServerTask::onHttpWebsocketConnectedMessage_);
    registerMessageHandler(this, &HttpServerTask::onHttpWebsocketDisconnectedMessage_);

    registerMessageHandler(this, &HttpServerTask::onHttpServeStaticFileMessage_);
}

HttpServerTask::~HttpServerTask()
{
    // empty
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static void set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    bool gzipEncoding = false;

    if (IS_FILE_EXT(filename, ".pdf")) 
    {
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "application/pdf"));
    } 
    else if (IS_FILE_EXT(filename, ".html")) 
    {
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/html"));
    } 
    else if (IS_FILE_EXT(filename, ".jpeg")) 
    {
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "image/jpeg"));
    } 
    else if (IS_FILE_EXT(filename, ".ico")) 
    {
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "image/x-icon"));
    }
    else if (IS_FILE_EXT(filename, ".css")) 
    {
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/css"));
    }
    else if (IS_FILE_EXT(filename, ".js")) 
    {
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "application/javascript"));
    }
    else if (IS_FILE_EXT(filename, ".js.gz")) 
    {
        // Special case for compressed JavaScript
        gzipEncoding = true;
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "application/javascript"));
    }
    else if (IS_FILE_EXT(filename, ".css.gz")) 
    {
        // Special case for compressed CSS
        gzipEncoding = true;
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/css"));
    }
    else
    {
        /* This is a limited set only */
        /* For any other type always set as plain text */
        ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/plain"));
    }

    // Enable unsafe inline scripting (required by newer Chrome)
    ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Content-Security-Policy", "script-src 'self' 'unsafe-inline'"));

    // Disable caching to prevent rendering problems during firmware updates
    ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Cache-Control", "no-store"));

    // Enable gzip encoding if required
    if (gzipEncoding)
    {
        ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Content-Encoding", "gzip"));
    }
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

void HttpServerTask::onHttpServeStaticFileMessage_(DVTask* origin, HttpServeStaticFileMessage* message)
{
    // Get path again for debug output
    char filepath[FILE_PATH_MAX];
    get_path_from_uri(filepath, "/http", message->request->uri, sizeof(filepath));

    if (filepath[strlen(filepath) - 1] == '/')
    {
        strcat(filepath, "index.html");
    }

    char* scratchBuf = (char*)heap_caps_malloc(SCRATCH_BUFSIZE, MALLOC_CAP_32BIT);
    assert(scratchBuf != nullptr);

    char *chunk = scratchBuf;
    size_t chunksize = read(message->fd, chunk, SCRATCH_BUFSIZE);

    if (chunksize > 0) 
    {
        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(message->request, chunk, chunksize) != ESP_OK) 
        {
            close(message->fd);
            ESP_LOGE(CURRENT_LOG_TAG, "Sending %s failed!", filepath);

            /* Abort sending file */
            httpd_resp_sendstr_chunk(message->request, NULL);

            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(message->request, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");

            /* Close connection. */
            ESP_ERROR_CHECK(httpd_req_async_handler_complete(message->request));
            goto http_static_file_cleanup;
        }

        /* Repost event to be processed again. */
        post(message);
    }
    else
    {
        /* Close file after sending complete */
        close(message->fd);
        ESP_LOGI(CURRENT_LOG_TAG, "Sending %s complete", filepath);

        /* Respond with an empty chunk to signal HTTP response completion */
        httpd_resp_send_chunk(message->request, NULL, 0);

        /* Close connection. */
        ESP_ERROR_CHECK(httpd_req_async_handler_complete(message->request));
    }

http_static_file_cleanup:
    heap_caps_free(scratchBuf);
}

esp_err_t HttpServerTask::ServeStaticPage_(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    int fd = -1;
    struct stat file_stat;

    char *filename = get_path_from_uri(filepath, "/http",
                                             req->uri, sizeof(filepath));
    if (!filename) 
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }
    
    // Append index.html to the end if the path ends with a slash.
    if (filename[strlen(filename) - 1] == '/')
    {
        strcat(filename, "index.html");
    }
    
    // Return 404 if not found.
    if (stat(filepath, &file_stat) == -1)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Failed to stat file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }
    
    fd = open(filepath, O_RDONLY);
    if (fd < 0) 
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(CURRENT_LOG_TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

#if 0
    char* scratchBuf = (char*)heap_caps_malloc(SCRATCH_BUFSIZE, MALLOC_CAP_32BIT);
    assert(scratchBuf != nullptr);

    char *chunk = scratchBuf;
    size_t chunksize = 0;
    
    while ((chunksize = read(fd, chunk, SCRATCH_BUFSIZE)) > 0)
    {
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) 
        {
            httpd_resp_send_chunk(req, NULL, 0);
            heap_caps_free(scratchBuf);
            close(fd);

            ESP_LOGW(CURRENT_LOG_TAG, "Sending %s failed!", filename);
            return ESP_FAIL;
        }
    }
    heap_caps_free(scratchBuf);

    close(fd);
    httpd_resp_send_chunk(req, NULL, 0);

    ESP_LOGI(CURRENT_LOG_TAG, "Sending %s complete", filename);
    return ESP_OK;

#else
    // Async requests are disabled due to ESP-IDF bug with additional HTTP headers.
    // See https://github.com/espressif/esp-idf/issues/13430.
    httpd_req_t* asyncReq;
    esp_err_t err = httpd_req_async_handler_begin(req, &asyncReq);
    if (err == ESP_OK)
    {
        /* Let associated DVTask handle sending the file to free up the HTTP task. */
        auto thisObj = (HttpServerTask*)req->user_ctx;
        HttpServerTask::HttpServeStaticFileMessage message(fd, asyncReq);
        thisObj->post(&message);
    }

    return err;
#endif // 0
}

esp_err_t HttpServerTask::ServeWebsocketPage_(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    auto thisObj = (HttpServerTask*)req->user_ctx;
    
    if (req->method == HTTP_GET) 
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Websocket connection opened");
        
        HttpWebsocketConnectedMessage message(fd);
        thisObj->post(&message);
        
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(CURRENT_LOG_TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len) 
    {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = (uint8_t*)heap_caps_calloc(1, ws_pkt.len + 1, MALLOC_CAP_32BIT);
        if (buf == NULL) 
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) 
        {
            ESP_LOGE(CURRENT_LOG_TAG, "httpd_ws_recv_frame failed with %d", ret);
            heap_caps_free(buf);
            return ret;
        }
    }
    
    // ignoring all websocket requests    
    heap_caps_free(buf);
    
    return ESP_OK;
}

static const char* HttpPartitionLabels_[] = {
    "http_0",
};

void HttpServerTask::onTaskStart_()
{
    if (!isRunning_)
    {
        auto partition = const_cast<esp_partition_t*>(esp_ota_get_running_partition());
        if (partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0)
        {
            // Should not reach here.
            ESP_LOGE(CURRENT_LOG_TAG, "Detected more than two app slots, this is unexpected");
            assert(false);
        }
        const char* partitionLabel = HttpPartitionLabels_[partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0];
        ESP_LOGI(CURRENT_LOG_TAG, "Using partition %s for HTTP server.", partitionLabel);
        
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/http",
            .partition_label = partitionLabel,
            .max_files = 10,
            .format_if_mount_failed = false
        };

        // Use settings defined above to initialize and mount SPIFFS filesystem.
        // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
        ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
        
        // Generate default configuration
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();

        // Allow HTTP server to auto-purge old connections.
        config.lru_purge_enable = true;
        config.max_open_sockets = 12;

        /* Use the URI wildcard matching function in order to
        * allow the same handler to respond to multiple different
        * target URIs which match the wildcard scheme */
        config.uri_match_fn = httpd_uri_match_wildcard;
        
        // Start HTTP server.
        ESP_ERROR_CHECK(httpd_start(&configServerHandle_, &config));
        
        // Configure URL handlers.
        httpd_uri_t webSocketPage = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = &ServeWebsocketPage_,
            .user_ctx   = this,
            .is_websocket = true,
            .handle_ws_control_frames = false,
            .supported_subprotocol = nullptr,
        };
        httpd_register_uri_handler(configServerHandle_, &webSocketPage);

        httpd_uri_t rootPage = 
        {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = &ServeStaticPage_,
            .user_ctx = this,
            .is_websocket = false,
            .handle_ws_control_frames = false,
            .supported_subprotocol = nullptr
        };
        httpd_register_uri_handler(configServerHandle_, &rootPage);

        isRunning_ = true;
    }
}

void HttpServerTask::onTaskSleep_()
{
    if (isRunning_)
    {
        // Close all active web sockets
        int numWifiScansInProgress = 0;
        for (auto& kvp : activeWebSockets_)
        {
            auto sock = kvp.first;

            if (kvp.second)
            {
                numWifiScansInProgress++;
            }

            httpd_ws_frame_t wsPkt;
            memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
            wsPkt.payload = nullptr;
            wsPkt.len = 0;
            wsPkt.type = HTTPD_WS_TYPE_CLOSE;
            
            httpd_ws_send_data(configServerHandle_, sock, &wsPkt);
        }
        
        ESP_ERROR_CHECK(httpd_stop(configServerHandle_));

        if (numWifiScansInProgress > 0)
        {
            StopWifiScanMessage request;
            publish(&request);
        }

        auto partition = const_cast<esp_partition_t*>(esp_ota_get_running_partition());
        if (partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0)
        {
            // Should not reach here.
            ESP_LOGE(CURRENT_LOG_TAG, "Detected more than two app slots, this is unexpected");
            assert(false);
        }
        const char* partitionLabel = HttpPartitionLabels_[partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0];
        esp_vfs_spiffs_unregister(partitionLabel);

        isRunning_ = false;
    }
}

void HttpServerTask::onHttpWebsocketConnectedMessage_(DVTask* origin, HttpWebsocketConnectedMessage* message)
{
    if (!activeWebSockets_.contains(message->fd))
    {
        activeWebSockets_[message->fd] = false;
    }
}

void HttpServerTask::onHttpWebsocketDisconnectedMessage_(DVTask* origin, HttpWebsocketDisconnectedMessage* message)
{
    if (activeWebSockets_.contains(message->fd))
    {
        activeWebSockets_.erase(message->fd);
    }
}

void HttpServerTask::sendJSONMessage_(cJSON* message, WebSocketList& socketList)
{
    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
    wsPkt.payload = (uint8_t*)cJSON_Print(message);
    wsPkt.len = strlen((char*)wsPkt.payload);
    wsPkt.type = HTTPD_WS_TYPE_TEXT;
    
    // Send to all sockets in list
    for (auto& kvp : socketList)
    {
        auto fd = kvp.first;

        ESP_LOGI(CURRENT_LOG_TAG, "Sending JSON message to socket %d", fd);
            
        if (httpd_ws_send_data(configServerHandle_, fd, &wsPkt) != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Websocket %d disconnected!", fd);
            
            // Queue up removal from the socket list.
            HttpWebsocketDisconnectedMessage message(fd);
            post(&message);
        }
    }
    
    // Make sure we don't leak memory due to the generated JSON.
    cJSON_free(wsPkt.payload);
    
    // Free the JSON object itself.
    cJSON_Delete(message);
}

}

}
