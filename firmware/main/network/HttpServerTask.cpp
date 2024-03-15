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
#include <string.h>
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
#include "storage/SettingsMessage.h"

extern "C"
{
    DV_EVENT_DEFINE_BASE(HTTP_SERVER_MESSAGE);
}

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE 256
#define CURRENT_LOG_TAG "HttpServerTask"

#define JSON_BATTERY_STATUS_TYPE "batteryStatus"
#define JSON_WIFI_STATUS_TYPE "wifiInfo"
#define JSON_WIFI_SAVED_TYPE "wifiSaved"
#define JSON_RADIO_STATUS_TYPE "radioInfo"
#define JSON_RADIO_SAVED_TYPE "radioSaved"

#define JSON_VOICE_KEYER_STATUS_TYPE "voiceKeyerInfo"
#define JSON_VOICE_KEYER_SAVED_TYPE "voiceKeyerSaved"
#define JSON_VOICE_KEYER_UPLOAD_COMPLETE "voiceKeyerUploadComplete"

#define JSON_REPORTING_STATUS_TYPE "reportingInfo"
#define JSON_REPORTING_SAVED_TYPE "reportingSaved"

#define JSON_LED_BRIGHTNESS_STATUS_TYPE "ledBrightnessInfo"
#define JSON_LED_BRIGHTNESS_SAVED_TYPE "ledBrightnessSaved"

#define JSON_FIRMWARE_UPLOAD_COMPLETE "firmwareUploadComplete"

#define JSON_CURRENT_MODE_TYPE "currentMode"

#define JSON_VOICE_KEYER_RUNNING_TYPE "voiceKeyerRunning"

#define JSON_FLEX_RADIO_DISCOVERED_TYPE "flexRadioDiscovered"

#define JSON_WIFI_SCAN_RESULTS_TYPE "wifiScanResults"

extern void StartSleeping();

namespace ezdv
{

namespace network
{

HttpServerTask::HttpServerTask()
    : ezdv::task::DVTask("HttpServerTask", 1, 4096, tskNO_AFFINITY, 256)
    , firmwareUploadInProgress_(false)
    , isRunning_(false)
{
    registerMessageHandler(this, &HttpServerTask::onBatteryStateMessage_);
    
    // HTTP handlers called from web socket
    registerMessageHandler(this, &HttpServerTask::onHttpWebsocketConnectedMessage_);
    registerMessageHandler(this, &HttpServerTask::onHttpWebsocketDisconnectedMessage_);
    registerMessageHandler(this, &HttpServerTask::onUpdateWifiMessage_);
    registerMessageHandler(this, &HttpServerTask::onUpdateRadioMessage_);
    registerMessageHandler(this, &HttpServerTask::onUpdateVoiceKeyerMessage_);

    registerMessageHandler(this, &HttpServerTask::onBeginUploadVoiceKeyerFileMessage_);
    registerMessageHandler(this, &HttpServerTask::onFileUploadCompleteMessage_);

    registerMessageHandler(this, &HttpServerTask::onUpdateReportingMessage_);
    registerMessageHandler(this, &HttpServerTask::onUpdateLedBrightnessMessage_);
    
    registerMessageHandler(this, &HttpServerTask::onFirmwareUpdateCompleteMessage_);

    registerMessageHandler(this, &HttpServerTask::onSetModeMessage_);
    registerMessageHandler(this, &HttpServerTask::onSetFreeDVModeMessage_);

    registerMessageHandler(this, &HttpServerTask::onStartStopVoiceKeyerMessage_);
    registerMessageHandler(this, &HttpServerTask::onStartVoiceKeyerMessage_);
    registerMessageHandler(this, &HttpServerTask::onStopVoiceKeyerMessage_);
    registerMessageHandler(this, &HttpServerTask::onVoiceKeyerCompleteMessage_);
    
    registerMessageHandler(this, &HttpServerTask::onFlexRadioDiscoveredMessage_);
    
    registerMessageHandler(this, &HttpServerTask::onRebootDeviceMessage_);

    registerMessageHandler(this, &HttpServerTask::onStartWifiScanMessage_);
    registerMessageHandler(this, &HttpServerTask::onStopWifiScanMessage_);
    registerMessageHandler(this, &HttpServerTask::onWifiNetworkListMessage_);

    registerMessageHandler(this, &HttpServerTask::onHttpServeStaticFileMessage_);
}

HttpServerTask::~HttpServerTask()
{
    // empty
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)
        
/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    // Enable unsafe inline scripting (required by newer Chrome)
    httpd_resp_set_hdr(req, "Content-Security-Policy", "script-src 'self' 'unsafe-inline'");

    if (IS_FILE_EXT(filename, ".pdf")) 
    {
        return httpd_resp_set_type(req, "application/pdf");
    } 
    else if (IS_FILE_EXT(filename, ".html")) 
    {
        return httpd_resp_set_type(req, "text/html");
    } 
    else if (IS_FILE_EXT(filename, ".jpeg")) 
    {
        return httpd_resp_set_type(req, "image/jpeg");
    } 
    else if (IS_FILE_EXT(filename, ".ico")) 
    {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if (IS_FILE_EXT(filename, ".css")) 
    {
        return httpd_resp_set_type(req, "text/css");
    }
    else if (IS_FILE_EXT(filename, ".js")) 
    {
        return httpd_resp_set_type(req, "application/javascript");
    }
    else if (IS_FILE_EXT(filename, ".js.gz")) 
    {
        // Special case for compressed JavaScript
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_set_type(req, "application/javascript");
    }
    else if (IS_FILE_EXT(filename, ".css.gz")) 
    {
        // Special case for compressed CSS
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_set_type(req, "text/css");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
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

    char scratchBuf[SCRATCH_BUFSIZE];
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
            return;
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

    // XXX - Send first chunk to make sure headers get sent. Otherwise, we'll crash in httpd_resp_send_chunk
    // in HttpServerTask.
    char scratchBuf[SCRATCH_BUFSIZE];
    char *chunk = scratchBuf;
    size_t chunksize = read(fd, chunk, SCRATCH_BUFSIZE);

    if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) 
    {
        close(fd);
        return ESP_FAIL;
    }

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
        buf = (uint8_t*)heap_caps_calloc(1, ws_pkt.len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
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
    
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {    
        cJSON* jsonMessage = cJSON_Parse((char*)buf);
        heap_caps_free(buf);
        
        // Ignore messages that we can't parse.
        if (jsonMessage != nullptr)
        {            
            if (cJSON_GetObjectItem(jsonMessage, "type")) 
            {                
                char *type = cJSON_GetStringValue(cJSON_GetObjectItem(jsonMessage,"type"));
                
                ESP_LOGI(CURRENT_LOG_TAG, "Received message of type %s", type);
                
                if (!strcmp(type, "saveWifiInfo"))
                {
                    UpdateWifiMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "saveRadioInfo"))
                {
                    UpdateRadioMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "saveVoiceKeyerInfo"))
                {
                    UpdateVoiceKeyerMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "saveReportingInfo"))
                {
                    UpdateReportingMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "saveLedBrightnessInfo"))
                {
                    UpdateLedBrightnessMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "uploadVoiceKeyerFile"))
                {
                    BeginUploadVoiceKeyerFileMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "uploadFirmwareFile"))
                {
                    // Note: this has to be set sooner than in the handler for the below
                    // as we could end up getting file blocks before the handler can be
                    // processed.
                    thisObj->firmwareUploadInProgress_ = true;
                    
                    ESP_LOGI(CURRENT_LOG_TAG, "Configuring firmware file upload");    
                    StartFirmwareUploadMessage reqMessage;
                    thisObj->publish(&reqMessage);
                }
                else if (!strcmp(type, "setMode"))
                {
                    SetModeMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "startStopVoiceKeyer"))
                {
                    StartStopVoiceKeyerMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "rebootDevice"))
                {
                    RebootDeviceMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "startWifiScan"))
                {
                    StartWifiScanMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
                else if (!strcmp(type, "stopWifiScan"))
                {
                    thisObj->activeWebSockets_[fd] = false;
                    
                    StopWifiScanMessage message(fd, jsonMessage);
                    thisObj->post(&message);
                }
            }
        }
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_BINARY)
    {
        // Binary packet, used for sending over the new voice keyer or firmware file
        if (thisObj->firmwareUploadInProgress_)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Received %d bytes of firmware data", ws_pkt.len);
            FirmwareUploadDataMessage message((char*)buf, ws_pkt.len);
            thisObj->publish(&message); // note: buf will be freed by voice keyer task.
        }
        else
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Received %d bytes of voice keyer data", ws_pkt.len);
            FileUploadDataMessage message((char*)buf, ws_pkt.len);
            thisObj->publish(&message); // note: buf will be freed by voice keyer task.
        }
    }
    
    return ESP_OK;
}

static const char* HttpPartitionLabels_[] = {
    "http_0",
    "http_1"
};

void HttpServerTask::onTaskStart_()
{
    if (!isRunning_)
    {
        auto partition = const_cast<esp_partition_t*>(esp_ota_get_running_partition());
        if (partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
            partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1)
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
            .max_files = 5,
            .format_if_mount_failed = false
        };

        // Use settings defined above to initialize and mount SPIFFS filesystem.
        // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
        ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
        
        // Generate default configuration
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();

        // Allow HTTP server to auto-purge old connections.
        config.lru_purge_enable = true;

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
        if (partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
            partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1)
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

    // Request current settings.
    {
        storage::RequestWifiSettingsMessage request;
        publish(&request);
        
        auto response = waitFor<storage::WifiSettingsMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            cJSON *root = cJSON_CreateObject();
            if (root != nullptr)
            {
                cJSON_AddStringToObject(root, "type", JSON_WIFI_STATUS_TYPE);
                cJSON_AddBoolToObject(root, "enabled", response->enabled);
                cJSON_AddNumberToObject(root, "mode", response->mode);
                cJSON_AddNumberToObject(root, "security", response->security);
                cJSON_AddNumberToObject(root, "channel", response->channel);
                cJSON_AddStringToObject(root, "ssid", response->ssid);
                cJSON_AddStringToObject(root, "password", response->password);
                cJSON_AddStringToObject(root, "hostname", response->hostname);
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets;
                sockets[message->fd] = false;
                sendJSONMessage_(root, sockets);
                delete response;
            }
            else
            {
                // HTTP isn't 100% critical but we really should see what's leaking memory.
                ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for Wi-Fi info!");
            }
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for current Wi-Fi settings");
        }
    }
    
    {
        storage::RequestRadioSettingsMessage request;
        publish(&request);
        
        auto response = waitFor<storage::RadioSettingsMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            cJSON *root = cJSON_CreateObject();
            if (root != nullptr)
            {
                cJSON_AddStringToObject(root, "type", JSON_RADIO_STATUS_TYPE);
                cJSON_AddBoolToObject(root, "headsetPtt", response->headsetPtt);
                cJSON_AddNumberToObject(root, "timeOutTimer", response->timeOutTimer);
                cJSON_AddBoolToObject(root, "enabled", response->enabled);
                cJSON_AddNumberToObject(root, "radioType", response->type);
                cJSON_AddStringToObject(root, "host", response->host);
                cJSON_AddNumberToObject(root, "port", response->port);
                cJSON_AddStringToObject(root, "username", response->username);
                cJSON_AddStringToObject(root, "password", response->password);
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets;
                sockets[message->fd] = false;
                sendJSONMessage_(root, sockets);
                delete response;
            }
            else
            {
                // HTTP isn't 100% critical but we really should see what's leaking memory.
                ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for radio info!");
            }
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for current radio settings");
        }
    }

    {
        storage::RequestVoiceKeyerSettingsMessage request;
        publish(&request);
        
        auto response = waitFor<storage::VoiceKeyerSettingsMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            cJSON *root = cJSON_CreateObject();
            if (root != nullptr)
            {
                cJSON_AddStringToObject(root, "type", JSON_VOICE_KEYER_STATUS_TYPE);
                cJSON_AddBoolToObject(root, "enabled", response->enabled);
                cJSON_AddNumberToObject(root, "secondsToWait", response->secondsToWait);
                cJSON_AddNumberToObject(root, "timesToTransmit", response->timesToTransmit);
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets;
                sockets[message->fd] = false;
                sendJSONMessage_(root, sockets);
                delete response;
            }
            else
            {
                // HTTP isn't 100% critical but we really should see what's leaking memory.
                ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for voice keyer info!");
            }
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for current voice keyer settings");
        }
    }

    {
        storage::RequestReportingSettingsMessage request;
        publish(&request);
        
        auto response = waitFor<storage::ReportingSettingsMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            cJSON *root = cJSON_CreateObject();
            if (root != nullptr)
            {
                cJSON_AddStringToObject(root, "type", JSON_REPORTING_STATUS_TYPE);
                cJSON_AddStringToObject(root, "callsign", response->callsign);
                cJSON_AddStringToObject(root, "gridSquare", response->gridSquare);
                cJSON_AddBoolToObject(root, "forceReporting", response->forceReporting);
                cJSON_AddNumberToObject(root, "reportingFrequency", response->freqHz);
                cJSON_AddStringToObject(root, "reportingMessage", response->message);

                // Note: below is responsible for cleanup.
                WebSocketList sockets;
                sockets[message->fd] = false;
                sendJSONMessage_(root, sockets);
                delete response;
            }
            else
            {
                // HTTP isn't 100% critical but we really should see what's leaking memory.
                ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for reporting info!");
            }
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for current reporting settings");
        }
    }
    
    {
        storage::RequestLedBrightnessSettingsMessage request;
        publish(&request);
        
        auto response = waitFor<storage::LedBrightnessSettingsMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            cJSON *root = cJSON_CreateObject();
            if (root != nullptr)
            {
                cJSON_AddStringToObject(root, "type", JSON_LED_BRIGHTNESS_STATUS_TYPE);
                cJSON_AddNumberToObject(root, "dutyCycle", response->dutyCycle);
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets;
                sockets[message->fd] = false;
                sendJSONMessage_(root, sockets);
                delete response;
            }
            else
            {
                // HTTP isn't 100% critical but we really should see what's leaking memory.
                ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for LED brightness info!");
            }
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for current LED brightness settings");
        }
    }

    {
        audio::RequestGetFreeDVModeMessage request;
        publish(&request);
        
        auto response = waitFor<audio::SetFreeDVModeMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            cJSON *root = cJSON_CreateObject();
            if (root != nullptr)
            {
                cJSON_AddStringToObject(root, "type", JSON_CURRENT_MODE_TYPE);
                cJSON_AddNumberToObject(root, "currentMode", (int)response->mode);
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets;
                sockets[message->fd] = false;
                sendJSONMessage_(root, sockets);
                delete response;
            }
            else
            {
                // HTTP isn't 100% critical but we really should see what's leaking memory.
                ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for FreeDV mode info!");
            }
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for current FreeDV mode info");
        }
    }

    {
        audio::GetKeyerStateMessage request;
        publish(&request);
        
        // This is asynchronous, so we don't need to handle here.
    }

    {
        driver::RequestBatteryStateMessage request;
        publish(&request);

        // This is asynchronous, so we don't need to handle here.
    }
}

void HttpServerTask::onHttpWebsocketDisconnectedMessage_(DVTask* origin, HttpWebsocketDisconnectedMessage* message)
{
    if (activeWebSockets_.contains(message->fd))
    {
        activeWebSockets_.erase(message->fd);
    }
    
    int numWifiScansInProgress = 0;
    for (auto& kvp : activeWebSockets_)
    {
        if (kvp.second)
        {
            numWifiScansInProgress++;
        }
    }

    if (numWifiScansInProgress == 0)
    {
        StopWifiScanMessage request;
        publish(&request);
    }
}

void HttpServerTask::onBatteryStateMessage_(DVTask* origin, driver::BatteryStateMessage* message)
{
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_BATTERY_STATUS_TYPE);
        cJSON_AddNumberToObject(root, "voltage", message->voltage);
        cJSON_AddNumberToObject(root, "stateOfCharge", message->soc);
        cJSON_AddNumberToObject(root, "stateOfChargeChange", message->socChangeRate);
        
        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for battery status!");
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

void HttpServerTask::onUpdateWifiMessage_(DVTask* origin, UpdateWifiMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Updating Wi-Fi settings");
    
    bool enabled = false;
    storage::WifiMode mode = storage::WifiMode::ACCESS_POINT;
    storage::WifiSecurityMode security = storage::WifiSecurityMode::NONE;
    int channel = 1;
    char *ssid = nullptr;
    char *password = nullptr;
    char *hostname = nullptr;
    
    bool settingsValid = true;
    
    auto enabledJSON = cJSON_GetObjectItem(message->request, "enabled");
    if (enabledJSON != nullptr)
    {
        enabled = cJSON_IsTrue(enabledJSON);
        if (enabled)
        {
            auto hostnameJSON = cJSON_GetObjectItem(message->request, "hostname");
            if (hostnameJSON != nullptr)
            {
                hostname = cJSON_GetStringValue(hostnameJSON);
                settingsValid &= strlen(hostname) > 0;
            }
            else
            {
                settingsValid = false;
            }

            auto modeJSON = cJSON_GetObjectItem(message->request, "mode");
            if (modeJSON != nullptr)
            {
                mode = (storage::WifiMode)(int)cJSON_GetNumberValue(modeJSON);
                settingsValid &= mode == storage::WifiMode::ACCESS_POINT || mode == storage::WifiMode::CLIENT;
            }
            else
            {
                settingsValid = false;
            }
        
            auto securityJSON = cJSON_GetObjectItem(message->request, "security");
            if (securityJSON != nullptr)
            {
                security = (storage::WifiSecurityMode)(int)cJSON_GetNumberValue(securityJSON);
                settingsValid &= mode == storage::WifiMode::CLIENT || (security >= storage::WifiSecurityMode::NONE && security <= storage::WifiSecurityMode::WPA_AND_WPA2 /*WPA2_AND_WPA3*/);
            }
            else
            {
                settingsValid = true;
            }
        
            auto channelJSON = cJSON_GetObjectItem(message->request, "channel");
            if (channelJSON != nullptr)
            {
                channel = (int)cJSON_GetNumberValue(channelJSON);
                settingsValid &= channel >= 1 && channel <= 11;
            }
        
            auto ssidJSON = cJSON_GetObjectItem(message->request, "ssid");
            if (ssidJSON != nullptr)
            {
                ssid = cJSON_GetStringValue(ssidJSON);
                settingsValid &= strlen(ssid) > 0;
            }
            
            auto passwordJSON = cJSON_GetObjectItem(message->request, "password");
            if (passwordJSON != nullptr)
            {
                password = cJSON_GetStringValue(passwordJSON);
                settingsValid &= 
                    mode == storage::WifiMode::CLIENT || 
                    (security == storage::WifiSecurityMode::NONE && strlen(password) == 0) ||
                    strlen(password) > 0;
            }
        }
    }
    else
    {
        settingsValid = false;
    }
    
    bool success = false;
    if (settingsValid)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Wi-Fi settings valid, requesting save");
        
        storage::SetWifiSettingsMessage* request = 
            new storage::SetWifiSettingsMessage(
                enabled, mode, security, channel,
                ssid, password, hostname);
        publish(request);
    
        auto response = waitFor<storage::WifiSettingsSavedMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            success = true;
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for Wi-Fi settings to be saved");
        }

        delete request;
    }

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_WIFI_SAVED_TYPE);
        cJSON_AddBoolToObject(root, "success", success);

        // Note: below is responsible for cleanup.
        WebSocketList sockets;
        sockets[message->fd] = false;
        sendJSONMessage_(root, sockets);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for Wi-Fi settings");
    }
    
    cJSON_free(message->request);
}

void HttpServerTask::onUpdateRadioMessage_(DVTask* origin, UpdateRadioMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Updating radio settings");
    
    bool headsetPtt = false;
    bool enabled = false;
    int type = 0;
    char* hostname = nullptr;
    int port = 0;
    char* username = nullptr;
    char* password = nullptr;
    int timeOutTimer = 0;
    
    bool settingsValid = true;

    auto headsetPttJSON = cJSON_GetObjectItem(message->request, "headsetPtt");
    if (headsetPttJSON != nullptr)
    {
        headsetPtt = cJSON_IsTrue(headsetPttJSON);
    }
    else
    {
        settingsValid = false;
    }
    
    auto timeOutTimerJSON = cJSON_GetObjectItem(message->request, "timeOutTimer");
    if (timeOutTimerJSON != nullptr)
    {
        timeOutTimer = cJSON_GetNumberValue(timeOutTimerJSON);
        settingsValid &= timeOutTimer > 0;
    }
    else
    {
        settingsValid = false;
    }

    auto enabledJSON = cJSON_GetObjectItem(message->request, "enabled");
    if (enabledJSON != nullptr)
    {
        enabled = cJSON_IsTrue(enabledJSON);
        if (enabled)
        {
            auto typeJSON = cJSON_GetObjectItem(message->request, "radioType");
            if (typeJSON != nullptr)
            {
                type = (int)cJSON_GetNumberValue(typeJSON);
                settingsValid &= type == 0 || type == 1;
            }
            else
            {
                settingsValid = false;
            }
            
            auto hostJSON = cJSON_GetObjectItem(message->request, "host");
            if (hostJSON != nullptr)
            {
                hostname = cJSON_GetStringValue(hostJSON);
                settingsValid &= strlen(hostname) > 0;
            }
            
            auto portJSON = cJSON_GetObjectItem(message->request, "port");
            if (portJSON != nullptr)
            {
                port = (int)cJSON_GetNumberValue(portJSON);
                settingsValid &= type == 1 || (port > 0 && port <= 65535);
            }
            else
            {
                settingsValid = false;
            }
        
            auto usernameJSON = cJSON_GetObjectItem(message->request, "username");
            if (usernameJSON != nullptr)
            {
                username = cJSON_GetStringValue(usernameJSON);
                settingsValid &= type == 1 || strlen(username) > 0;
            }
            
            auto passwordJSON = cJSON_GetObjectItem(message->request, "password");
            if (passwordJSON != nullptr)
            {
                password = cJSON_GetStringValue(passwordJSON);
                settingsValid &= type == 1 || strlen(password) > 0;
            }
        }
    }
    else
    {
        settingsValid = false;
    }
    
    bool success = false;
    if (settingsValid)
    {
        storage::SetRadioSettingsMessage request(headsetPtt, timeOutTimer, enabled, type, hostname, port, username, password);
        publish(&request);
    
        auto response = waitFor<storage::RadioSettingsSavedMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            success = true;
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for radio settings to be saved");
        }
    }

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_RADIO_SAVED_TYPE);
        cJSON_AddBoolToObject(root, "success", success);

        // Note: below is responsible for cleanup.
        WebSocketList sockets;
        sockets[message->fd] = false;
        sendJSONMessage_(root, sockets);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for radio settings");
    }
    
    cJSON_free(message->request);
}

void HttpServerTask::onBeginUploadVoiceKeyerFileMessage_(DVTask* origin, BeginUploadVoiceKeyerFileMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Configuring voice keyer file upload");
    firmwareUploadInProgress_ = false;
    
    int sizeToUpload = 0;
    auto sizeJSON = cJSON_GetObjectItem(message->request, "size");
    if (sizeJSON != nullptr)
    {
        sizeToUpload = (int)cJSON_GetNumberValue(sizeJSON);

        StartFileUploadMessage message(sizeToUpload);
        publish(&message);
    }
}

void HttpServerTask::onUpdateVoiceKeyerMessage_(DVTask* origin, UpdateVoiceKeyerMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Updating voice keyer settings");
    
    bool enabled = false;
    int secondsToWait = 0;
    int timesToTransmit = 0;
    
    bool settingsValid = true;
    
    auto enabledJSON = cJSON_GetObjectItem(message->request, "enabled");
    if (enabledJSON != nullptr)
    {
        enabled = cJSON_IsTrue(enabledJSON);
        if (enabled)
        {            
            auto secondsJSON = cJSON_GetObjectItem(message->request, "secondsToWait");
            if (secondsJSON != nullptr)
            {
                secondsToWait = (int)cJSON_GetNumberValue(secondsJSON);
                settingsValid &= secondsToWait > 0;
            }
            else
            {
                settingsValid = false;
            }

            auto waitJSON = cJSON_GetObjectItem(message->request, "timesToTransmit");
            if (waitJSON != nullptr)
            {
                timesToTransmit = (int)cJSON_GetNumberValue(waitJSON);
                settingsValid &= timesToTransmit > 0;
            }
            else
            {
                settingsValid = false;
            }
        }
    }
    else
    {
        settingsValid = false;
    }
    
    bool success = false;
    if (settingsValid)
    {
        storage::SetVoiceKeyerSettingsMessage request(enabled, timesToTransmit, secondsToWait);
        publish(&request);
    
        auto response = waitFor<storage::VoiceKeyerSettingsSavedMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            success = true;
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for voice keyer settings to be saved");
        }
    }

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_VOICE_KEYER_SAVED_TYPE);
        cJSON_AddBoolToObject(root, "success", success);
        
        if (!success)
        {
            if (!settingsValid)
            {
                cJSON_AddNumberToObject(root, "errorType", audio::FileUploadCompleteMessage::MISSING_FIELDS);
            }
            else
            {
                cJSON_AddNumberToObject(root, "errorType", audio::FileUploadCompleteMessage::UNABLE_SAVE_SETTINGS);
            }
        }

        // Note: below is responsible for cleanup.
        WebSocketList sockets;
        sockets[message->fd] = false;
        sendJSONMessage_(root, sockets);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for voice keyer settings");
    }
    
    cJSON_free(message->request);
}

void HttpServerTask::onUpdateReportingMessage_(DVTask* origin, UpdateReportingMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Updating reporting settings");
    
    bool settingsValid = false;
    char* callsign = nullptr;
    char* gridSquare = nullptr;
    char* reportingMessage = nullptr;
    bool forceReporting = false;
    uint64_t freqHz = 0;
    
    auto callsignJSON = cJSON_GetObjectItem(message->request, "callsign");
    if (callsignJSON != nullptr)
    {
        callsign = cJSON_GetStringValue(callsignJSON);
        assert(callsign != nullptr);
        settingsValid = true; // empty callsign / N0CALL == disable FreeDV Reporter
    }
    
    auto gridSquareJSON = cJSON_GetObjectItem(message->request, "gridSquare");
    if (gridSquareJSON != nullptr)
    {
        gridSquare = cJSON_GetStringValue(gridSquareJSON);
        assert(gridSquare != nullptr);
        settingsValid = true; // empty gridsquare / UN00KN == disable FreeDV Reporter
    }

    auto reportingMessageJSON = cJSON_GetObjectItem(message->request, "reportingMessage");
    if (reportingMessageJSON != nullptr)
    {
        reportingMessage = cJSON_GetStringValue(reportingMessageJSON);
        assert(reportingMessage != nullptr);
    }
    
    auto forceReportingJSON = cJSON_GetObjectItem(message->request, "forceEnable");
    settingsValid &= forceReportingJSON != nullptr;
    if (settingsValid)
    {
        forceReporting = cJSON_IsTrue(forceReportingJSON);
        
        auto freqHzJSON = cJSON_GetObjectItem(message->request, "frequency");
        if (freqHzJSON != nullptr)
        {
            freqHz = (uint64_t)cJSON_GetNumberValue(freqHzJSON);
        }
    }

    bool success = false;
    if (settingsValid)
    {
        storage::SetReportingSettingsMessage request(callsign, gridSquare, forceReporting, freqHz, reportingMessage);
        publish(&request);
    
        auto response = waitFor<storage::ReportingSettingsSavedMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            success = true;
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for reporting settings to be saved");
        }
    }

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_REPORTING_SAVED_TYPE);
        cJSON_AddBoolToObject(root, "success", success);

        // Note: below is responsible for cleanup.
        WebSocketList sockets;
        sockets[message->fd] = false;
        sendJSONMessage_(root, sockets);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for reporting settings");
    }
    
    cJSON_free(message->request);
}

void HttpServerTask::onFileUploadCompleteMessage_(DVTask* origin, audio::FileUploadCompleteMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "File upload complete");

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_VOICE_KEYER_UPLOAD_COMPLETE);
        cJSON_AddBoolToObject(root, "success", message->success);
        cJSON_AddNumberToObject(root, "errorType", message->errorType);
        cJSON_AddNumberToObject(root, "errno", message->errorNumber);

        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for VK file upload");
    }
}

void HttpServerTask::onFirmwareUpdateCompleteMessage_(DVTask* origin, storage::FirmwareUpdateCompleteMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Firmware upload complete");

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_FIRMWARE_UPLOAD_COMPLETE);
        cJSON_AddBoolToObject(root, "success", message->success);

        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for firmware file upload");
    }
}

void HttpServerTask::onUpdateLedBrightnessMessage_(DVTask* origin, UpdateLedBrightnessMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Updating LED brightness settings");
    
    int dutyCycle = 0;
    bool settingsValid = true;
         
    auto dutyCycleJSON = cJSON_GetObjectItem(message->request, "dutyCycle");
    if (dutyCycleJSON != nullptr)
    {
        dutyCycle = (int)cJSON_GetNumberValue(dutyCycleJSON);
        settingsValid &= dutyCycle > 819 && dutyCycle <= 8192;
    }
    else
    {
        settingsValid = false;
    }
    
    bool success = false;
    if (settingsValid)
    {
        storage::SetLedBrightnessSettingsMessage request(dutyCycle);
        publish(&request);
    
        auto response = waitFor<storage::LedBrightnessSettingsSavedMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            success = true;
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for LED brightness settings to be saved");
        }
    }

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_LED_BRIGHTNESS_SAVED_TYPE);
        cJSON_AddBoolToObject(root, "success", success);

        // Note: below is responsible for cleanup.
        WebSocketList sockets;
        sockets[message->fd] = false;
        sendJSONMessage_(root, sockets);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for LED brightness settings");
    }
    
    cJSON_free(message->request);
}

void HttpServerTask::onSetModeMessage_(DVTask* origin, SetModeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Updating current mode settings");
    
    int mode = 0;
    bool settingsValid = true;
         
    auto modeJSON = cJSON_GetObjectItem(message->request, "mode");
    if (modeJSON != nullptr)
    {
        mode = (int)cJSON_GetNumberValue(modeJSON);
        settingsValid &= mode >= 0 && mode <= 3;
    }
    else
    {
        settingsValid = false;
    }
    
    if (settingsValid)
    {
        audio::RequestSetFreeDVModeMessage request((audio::FreeDVMode)mode);
        publish(&request);
    }

    // Note: this is an async request due to storage not being involved.    
    cJSON_free(message->request);
}

void HttpServerTask::onSetFreeDVModeMessage_(DVTask* origin, audio::SetFreeDVModeMessage* message)
{
    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_CURRENT_MODE_TYPE);
        cJSON_AddNumberToObject(root, "currentMode", (int)message->mode);

        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for mode settings");
    }
}

void HttpServerTask::onStartStopVoiceKeyerMessage_(DVTask* origin, StartStopVoiceKeyerMessage* message)
{    
    bool running = 0;
         
    auto runningJSON = cJSON_GetObjectItem(message->request, "running");
    if (runningJSON != nullptr)
    {
        running = cJSON_IsTrue(runningJSON);
    }

    audio::RequestStartStopKeyerMessage vkRequest(running);
    publish(&vkRequest);

    // Note: this is an async request due to storage not being involved.    
    cJSON_free(message->request);
}

void HttpServerTask::sendVoiceKeyerExecutionState_(bool state)
{
    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_VOICE_KEYER_RUNNING_TYPE);
        cJSON_AddNumberToObject(root, "running", state);

        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for voice keyer state");
    }
}

void HttpServerTask::onStartVoiceKeyerMessage_(DVTask* origin, audio::StartVoiceKeyerMessage* message)
{
    sendVoiceKeyerExecutionState_(true);
}

void HttpServerTask::onStopVoiceKeyerMessage_(DVTask* origin, audio::StopVoiceKeyerMessage* message)
{
    sendVoiceKeyerExecutionState_(false);
}

void HttpServerTask::onVoiceKeyerCompleteMessage_(DVTask* origin, audio::VoiceKeyerCompleteMessage* message)
{
    sendVoiceKeyerExecutionState_(false);
}

void HttpServerTask::onStartWifiScanMessage_(DVTask* origin, StartWifiScanMessage* message)
{
    ezdv::network::StartWifiScanMessage request;
    publish(&request);

    activeWebSockets_[message->fd] = true;
}

void HttpServerTask::onStopWifiScanMessage_(DVTask* origin, HttpServerTask::StopWifiScanMessage* message)
{
    int numWifiScansInProgress = 0;

    for (auto& kvp : activeWebSockets_)
    {
        if (kvp.second)
        {
            numWifiScansInProgress++;
        }
    }

    if (numWifiScansInProgress == 0)
    {
        ezdv::network::StopWifiScanMessage request;
        publish(&request);
    }
}

void HttpServerTask::onWifiNetworkListMessage_(DVTask* origin, WifiNetworkListMessage* message)
{
    cJSON* root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_WIFI_SCAN_RESULTS_TYPE);
        cJSON* networkList = cJSON_AddArrayToObject(root, "networkList");
        
        if (networkList != nullptr)
        {
            // The logic below prevents duplicate SSIDs from being sent over.
            std::map<std::string, bool> ssidList;
            for (int index = 0; index < message->numRecords; index++)
            {
                ssidList[(const char*)message->records[index].ssid] = true;
            }
            
            for (auto& ssid : ssidList)
            {
                cJSON_AddItemToArray(networkList, cJSON_CreateString(ssid.first.c_str()));
            }
        }
        
        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }

    // Free list of APs when we're done.
    free(message->records);
}

void HttpServerTask::onFlexRadioDiscoveredMessage_(DVTask* origin, network::flex::FlexRadioDiscoveredMessage* message)
{
    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_FLEX_RADIO_DISCOVERED_TYPE);
        cJSON_AddStringToObject(root, "ip", message->ip);
        cJSON_AddStringToObject(root, "description", message->desc);

        // Note: below is responsible for cleanup.
        sendJSONMessage_(root, activeWebSockets_);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for Flex radio info");
    }
}

extern "C" bool rebootDevice;

void HttpServerTask::onRebootDeviceMessage_(DVTask* origin, RebootDeviceMessage* message)
{
    rebootDevice = true;
    StartSleeping();
}

}

}