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
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "WirelessTask.h"
#include "NetworkMessage.h"

#define DEFAULT_AP_NAME_PREFIX "ezDV "
#define DEFAULT_AP_CHANNEL (1)
#define MAX_AP_CONNECTIONS (5)

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE 256
#define CURRENT_LOG_TAG "WirelessTask"

namespace ezdv
{

namespace network
{

void WirelessTask::IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WirelessTask* obj = (WirelessTask*)event_handler_arg;
    
    switch(event_id)
    {
        case IP_EVENT_STA_GOT_IP:
            obj->onNetworkConnected_();
        
            // XX -- just for testing
            {
                icom::IcomConnectRadioMessage message("192.168.4.2", 50001, "RADIO USERNAME", "RADIO PASSWORD");
                obj->publish(&message);
            }
            break;
    }
}

void WirelessTask::WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WirelessTask* obj = (WirelessTask*)event_handler_arg;
    
    ESP_LOGI(CURRENT_LOG_TAG, "Wifi event: %ld", event_id);
    
    switch (event_id)
    {
        case WIFI_EVENT_AP_START:
            obj->onNetworkConnected_();
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            // XX -- just for testing
            {
                icom::IcomConnectRadioMessage message("192.168.4.2", 50001, "RADIO USERNAME", "RADIO PASSWORD");
                obj->publish(&message);
            }
            break;
        case WIFI_EVENT_AP_STOP:
        case WIFI_EVENT_STA_DISCONNECTED:
            obj->onNetworkDisconnected_();
            break;
    }
}

WirelessTask::WirelessTask(audio::AudioInput* freedvHandler, audio::AudioInput* tlv320Handler)
    : ezdv::task::DVTask("WirelessTask", 1, 4096, tskNO_AFFINITY, 10)
    , icomControlTask_(icom::IcomSocketTask::CONTROL_SOCKET)
    , icomAudioTask_(icom::IcomSocketTask::AUDIO_SOCKET)
    , icomCIVTask_(icom::IcomSocketTask::CIV_SOCKET)
    , freedvHandler_(freedvHandler)
    , tlv320Handler_(tlv320Handler)
{
    registerMessageHandler(this, &WirelessTask::onRadioStateChange_);
}

WirelessTask::~WirelessTask()
{
    // empty
}

void WirelessTask::onTaskStart_()
{
    enableWifi_();
    enableHttp_();
    
    icomControlTask_.start();
    icomAudioTask_.start();
    icomCIVTask_.start();
}

void WirelessTask::onTaskWake_()
{
    enableWifi_();
    enableHttp_();
    
    icomControlTask_.wake();
    icomAudioTask_.wake();
    icomCIVTask_.wake();
}

void WirelessTask::onTaskSleep_()
{
    icomControlTask_.sleep();
    icomAudioTask_.sleep();
    icomCIVTask_.sleep();
    
    disableHttp_();
    disableWifi_();
}

void WirelessTask::enableWifi_()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if 1
    esp_netif_create_default_wifi_ap();
#else
    esp_netif_create_default_wifi_sta();
#endif
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
#if 1
        .ap = {
            .password = "",
            .ssid_len = 0, // Will auto-determine length on start.
            .channel = DEFAULT_AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = MAX_AP_CONNECTIONS,
            .pmf_cfg = {
                    .required = false,
            },
        },
#else
        .sta = {
            //.ssid = "RADIO SSID",
            //.password = "RADIO PASSWORD",
            .scan_method = WIFI_FAST_SCAN,
            .bssid_set = false,
            .bssid = "",
            .channel = 0,
            .listen_interval = 0,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            /*.threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,*/
        }
#endif
    };
    
    // Register event handler so we can notify the user on network
    // status changes.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFiEventHandler_,
                                                        this,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &IPEventHandler_,
                                                        this,
                                                        NULL));
#if 1
    // Append last two bytes of MAC address to SSID prefix.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    sprintf((char*)wifi_config.ap.ssid, "%s%02x%02x", DEFAULT_AP_NAME_PREFIX, mac[4], mac[5]);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
#else
    sprintf((char*)wifi_config.sta.ssid, "SSID");
    sprintf((char*)wifi_config.sta.password, "PASSWORD");
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
#endif
}

void WirelessTask::disableWifi_()
{
    ESP_ERROR_CHECK(esp_wifi_stop());
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

static httpd_uri_t rootPage = 
{
    .uri = "/*",
    .method = HTTP_GET,
    .handler = &ServeStaticPage,
    .user_ctx = nullptr
};

void WirelessTask::enableHttp_()
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
    httpd_register_uri_handler(configServerHandle_, &rootPage);
}

void WirelessTask::disableHttp_()
{
    ESP_ERROR_CHECK(httpd_stop(configServerHandle_));
}

void WirelessTask::onNetworkConnected_()
{
    WirelessNetworkStatusMessage message(true);
    publish(&message);
}

void WirelessTask::onNetworkDisconnected_()
{
    WirelessNetworkStatusMessage message(false);
    publish(&message);
}

void WirelessTask::onRadioStateChange_(DVTask* origin, RadioConnectionStatusMessage* message)
{
    if (message->state)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "rerouting audio pipes to network");
        
        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL,
            nullptr
        );
        
        icomAudioTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
            freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
        );
        
        freedvHandler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
            icomAudioTask_.getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
        );
    }
}

}

}