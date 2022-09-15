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

#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

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


namespace ezdv
{

namespace network
{

HttpServerTask::HttpServerTask()
    : ezdv::task::DVTask("HttpServerTask", 1, 4096, tskNO_AFFINITY, pdMS_TO_TICKS(1000))
{
    registerMessageHandler(this, &HttpServerTask::onBatteryStateMessage_);
    
    // HTTP handlers called from web socket
    registerMessageHandler(this, &HttpServerTask::onHttpWebsocketConnectedMessage_);
    registerMessageHandler(this, &HttpServerTask::onHttpWebsocketDisconnectedMessage_);
    registerMessageHandler(this, &HttpServerTask::onUpdateWifiMessage_);
    registerMessageHandler(this, &HttpServerTask::onUpdateRadioMessage_);
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

static esp_err_t ServeStaticPage(httpd_req_t *req)
{
    char scratchBuf[SCRATCH_BUFSIZE];
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
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
    
    fd = fopen(filepath, "r");
    if (!fd) 
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(CURRENT_LOG_TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = scratchBuf;
    size_t chunksize;
    do 
    {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) 
        {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) 
            {
                fclose(fd);
                ESP_LOGE(CURRENT_LOG_TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
               return ESP_FAIL;
           }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(CURRENT_LOG_TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
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
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
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
            free(buf);
            return ret;
        }
    }
    
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {    
        cJSON* jsonMessage = cJSON_Parse((char*)buf);
        free(buf);
        
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
            }
        }
    }
    
    return ESP_OK;
}

static const httpd_uri_t rootPage = 
{
    .uri = "/*",
    .method = HTTP_GET,
    .handler = &ServeStaticPage,
    .user_ctx = nullptr
};


void HttpServerTask::onTaskStart_()
{
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/http",
      .partition_label = "http",
      .max_files = 5,
      .format_if_mount_failed = false
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    
    // Generate default configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
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
            .is_websocket = true
    };
    httpd_register_uri_handler(configServerHandle_, &webSocketPage);
    httpd_register_uri_handler(configServerHandle_, &rootPage);
}

void HttpServerTask::onTaskWake_()
{
    // no specific wake tasks, only start/stop supported
}

void HttpServerTask::onTaskSleep_()
{
    ESP_ERROR_CHECK(httpd_stop(configServerHandle_));
}

void HttpServerTask::onHttpWebsocketConnectedMessage_(DVTask* origin, HttpWebsocketConnectedMessage* message)
{
    activeWebSockets_.push_back(message->fd);

    // Request current Wi-Fi/radio settings.
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
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets = { message->fd };
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
                cJSON_AddBoolToObject(root, "enabled", response->enabled);
                cJSON_AddStringToObject(root, "host", response->host);
                cJSON_AddNumberToObject(root, "port", response->port);
                cJSON_AddStringToObject(root, "username", response->username);
                cJSON_AddStringToObject(root, "password", response->password);
        
                // Note: below is responsible for cleanup.
                WebSocketList sockets = { message->fd };
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
}

void HttpServerTask::onHttpWebsocketDisconnectedMessage_(DVTask* origin, HttpWebsocketDisconnectedMessage* message)
{
    auto iter = std::find(activeWebSockets_.begin(), activeWebSockets_.end(), message->fd);
    if (iter != activeWebSockets_.end())
    {
        activeWebSockets_.erase(iter);
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
    // Send to all sockets in list
    for (auto& fd : socketList)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Sending JSON message to socket %d", fd);
        
        httpd_ws_frame_t wsPkt;
        memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
        wsPkt.payload = (uint8_t*)cJSON_Print(message);
        wsPkt.len = strlen((char*)wsPkt.payload);
        wsPkt.type = HTTPD_WS_TYPE_TEXT;
            
        if (httpd_ws_send_data(configServerHandle_, fd, &wsPkt) != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Websocket %d disconnected!", fd);
            
            // Queue up removal from the socket list.
            HttpWebsocketDisconnectedMessage message(fd);
            post(&message);
        }
    }
    
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
    
    bool settingsValid = true;
    
    auto enabledJSON = cJSON_GetObjectItem(message->request, "enabled");
    if (enabledJSON != nullptr)
    {
        enabled = cJSON_IsTrue(enabledJSON);
        if (enabled)
        {
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
                settingsValid &= mode == storage::WifiMode::CLIENT || (security >= storage::WifiSecurityMode::NONE && security <= storage::WifiSecurityMode::WPA2_AND_WPA3);
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
        storage::SetWifiSettingsMessage request(enabled, mode, security, channel, ssid, password);
        publish(&request);
    
        auto response = waitFor<storage::WifiSettingsSavedMessage>(pdMS_TO_TICKS(1000), NULL);
        if (response)
        {
            success = true;
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Timed out waiting for Wi-Fi settings to be saved");
        }
    }

    // Send response
    cJSON *root = cJSON_CreateObject();
    if (root != nullptr)
    {
        cJSON_AddStringToObject(root, "type", JSON_WIFI_SAVED_TYPE);
        cJSON_AddBoolToObject(root, "success", success);

        // Note: below is responsible for cleanup.
        WebSocketList list { message->fd };
        sendJSONMessage_(root, list);
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
    
    bool enabled = false;
    char* hostname = nullptr;
    int port = 0;
    char* username = nullptr;
    char* password = nullptr;
    
    bool settingsValid = true;
    
    auto enabledJSON = cJSON_GetObjectItem(message->request, "enabled");
    if (enabledJSON != nullptr)
    {
        enabled = cJSON_IsTrue(enabledJSON);
        if (enabled)
        {
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
                settingsValid &= port > 0 && port <= 65535;
            }
            else
            {
                settingsValid = false;
            }
        
            auto usernameJSON = cJSON_GetObjectItem(message->request, "username");
            if (usernameJSON != nullptr)
            {
                username = cJSON_GetStringValue(usernameJSON);
                settingsValid &= strlen(username) > 0;
            }
            
            auto passwordJSON = cJSON_GetObjectItem(message->request, "password");
            if (passwordJSON != nullptr)
            {
                password = cJSON_GetStringValue(passwordJSON);
                settingsValid &= strlen(password) > 0;
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
        storage::SetRadioSettingsMessage request(enabled, hostname, port, username, password);
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
        WebSocketList list { message->fd };
        sendJSONMessage_(root, list);
    }
    else
    {
        // HTTP isn't 100% critical but we really should see what's leaking memory.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not create JSON object for radio settings");
    }
    
    cJSON_free(message->request);
}

}

}