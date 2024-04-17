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
#include "esp_sntp.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "WirelessTask.h"
#include "HttpServerTask.h"
#include "NetworkMessage.h"

#define DEFAULT_AP_NAME_PREFIX "ezDV "
#define DEFAULT_AP_CHANNEL (1)
#define MAX_AP_CONNECTIONS (5)

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE 256
#define CURRENT_LOG_TAG "WirelessTask"

extern "C"
{
    DV_EVENT_DEFINE_BASE(WIRELESS_TASK_MESSAGE);
}

namespace ezdv
{

namespace network
{

void WirelessTask::IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(CURRENT_LOG_TAG, "IP event: %ld", event_id);
    
    WirelessTask* obj = (WirelessTask*)event_handler_arg;
    
    if (obj->isAwake())
    {
        switch(event_id)
        {
            case IP_EVENT_AP_STAIPASSIGNED:
            {
                ip_event_ap_staipassigned_t* ipData = (ip_event_ap_staipassigned_t*)event_data;
                char buf[32];
                sprintf(buf, IPSTR, IP2STR(&ipData->ip));
                
                ESP_LOGI(CURRENT_LOG_TAG, "Assigned IP %s to client", buf);
                ApAssignedIpMessage message(buf, ipData->mac);
                obj->post(&message);

                break;
            }
            case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t* ipData = (ip_event_got_ip_t*)event_data;
                char buf[32];
                sprintf(buf, "IP " IPSTR, IP2STR(&ipData->ip_info.ip));
            
                StaAssignedIpMessage message(buf);
                obj->post(&message);

                break;
            }
        }
    }
}

void WirelessTask::WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WirelessTask* obj = (WirelessTask*)event_handler_arg;
    
    ESP_LOGI(CURRENT_LOG_TAG, "Wi-Fi event: %ld", event_id);
    
    if (obj->isAwake())
    {
        switch (event_id)
        {
            case WIFI_EVENT_SCAN_DONE:
            {
                WifiScanCompletedMessage message;
                obj->post(&message);
                break;
            }
            case WIFI_EVENT_AP_START:
            {
                ApStartedMessage message;
                obj->post(&message);
                break;
            }
            case WIFI_EVENT_AP_STOP:
            case WIFI_EVENT_STA_DISCONNECTED:
            {
                bool networkIsDown = true;

                if (event_id == WIFI_EVENT_STA_DISCONNECTED)
                {
                    wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t*)event_data;
                    if (disconn->reason == WIFI_REASON_ROAMING) 
                    {
                        ESP_LOGW(CURRENT_LOG_TAG, "Network disconnected due to roaming");
                        networkIsDown = false;
                    }
                }

                if (networkIsDown)
                {
                    NetworkDownMessage message;
                    obj->post(&message);
                }

                break;
            }
        }
    }
}

WirelessTask::WirelessTask()
    : ezdv::task::DVTask("WirelessTask", 5, 4096, tskNO_AFFINITY, 128)
    , isAwake_(false)
    , wifiRunning_(false)
{
    // Handlers for internal messages (intended to make events that happen
    // on ESP-IDF tasks happen on this one instead).
    registerMessageHandler(this, &WirelessTask::onApAssignedIpMessage_);
    registerMessageHandler(this, &WirelessTask::onStaAssignedIpMessage_);
    registerMessageHandler(this, &WirelessTask::onApStartedMessage_);
    registerMessageHandler(this, &WirelessTask::onNetworkDownMessage_);
}

WirelessTask::~WirelessTask()
{
    // empty
}

void WirelessTask::onTaskStart_()
{
    isAwake_ = true;
    enableDefaultWifi_();
}

void WirelessTask::onTaskSleep_()
{
    isAwake_ = false;
    disableHttp_();
    disableWifi_();
}

void WirelessTask::enableDefaultWifi_()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    // Register event handler so we can notify the user on network
    // status changes.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFiEventHandler_,
                                                        this,
                                                        &wifiEventHandle_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &IPEventHandler_,
                                                        this,
                                                        &ipEventHandle_));
                                                        
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    wifi_config.ap.ssid_len = 0; // Will auto-determine length on start.
    wifi_config.ap.channel = DEFAULT_AP_CHANNEL;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = MAX_AP_CONNECTIONS;
    wifi_config.ap.pmf_cfg.required = false;
    
    // Append last two bytes of MAC address to default SSID.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    sprintf((char*)wifi_config.ap.ssid, "%s%02x%02x", DEFAULT_AP_NAME_PREFIX, mac[4], mac[5]);
    
    sprintf((char*)wifi_config.ap.password, "%s", "");
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void WirelessTask::disableWifi_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Shutting down Wi-Fi");

    // Shut down SNTP.
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
    
    esp_event_handler_instance_unregister(WIFI_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &wifiEventHandle_);
    esp_event_handler_instance_unregister(IP_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &ipEventHandle_);

    esp_wifi_disconnect();
    esp_wifi_stop();
}

void WirelessTask::enableHttp_()
{
    httpServerTask_.start();
}

void WirelessTask::disableHttp_()
{
    if (wifiRunning_)
    {
        sleep(&httpServerTask_, pdMS_TO_TICKS(1000));
    }
}

void WirelessTask::onNetworkUp_()
{
    WirelessNetworkStatusMessage message(true);
    publish(&message);

    wifiRunning_ = true;
    
    enableHttp_();
}

void WirelessTask::onNetworkConnected_(bool client, char* ip, uint8_t* macAddress)
{
    // Broadcast our current IP address if available.
    if (client)
    {
        IpAddressAssignedMessage ipAssigned(ip);
        publish(&ipAssigned);
    }
    else
    {
        IpAddressAssignedMessage ipAssigned("");
        publish(&ipAssigned);
    }
}

void WirelessTask::onNetworkDisconnected_()
{
    // Shut down HTTP server.
    disableHttp_();

    WirelessNetworkStatusMessage message(false);
    publish(&message);
}

void WirelessTask::onApAssignedIpMessage_(DVTask* origin, ApAssignedIpMessage* message)
{
    onNetworkConnected_(false, message->ipString, message->macAddress);
}

void WirelessTask::onStaAssignedIpMessage_(DVTask* origin, StaAssignedIpMessage* message)
{
    onNetworkUp_();
    onNetworkConnected_(true, message->ipString, nullptr);
}

void WirelessTask::onApStartedMessage_(DVTask* origin, ApStartedMessage* message)
{
    onNetworkUp_();
}

void WirelessTask::onNetworkDownMessage_(DVTask* origin, NetworkDownMessage* message)
{
    onNetworkDisconnected_();

    if (isAwake_)
    {
        // Reattempt connection to access point if we couldn't find
        // it the first time around.
        wifiRunning_ = false;
        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

}

}
