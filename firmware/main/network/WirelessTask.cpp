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
#include "HttpServerTask.h"
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
        case WIFI_EVENT_AP_STOP:
        case WIFI_EVENT_STA_DISCONNECTED:
        case WIFI_EVENT_STA_BEACON_TIMEOUT:
            obj->onNetworkDisconnected_();

            if (event_id == WIFI_EVENT_STA_BEACON_TIMEOUT ||
                (event_id == WIFI_EVENT_STA_DISCONNECTED && !obj->wifiRunning_))
            {
                // Reattempt connection to access point if we couldn't find
                // it the first time around.
                obj->wifiRunning_ = false;
                esp_wifi_disconnect();
                ESP_ERROR_CHECK(esp_wifi_connect());
            }

            break;
    }
}

WirelessTask::WirelessTask(audio::AudioInput* freedvHandler, audio::AudioInput* tlv320Handler)
    : ezdv::task::DVTask("WirelessTask", 1, 4096, tskNO_AFFINITY, pdMS_TO_TICKS(1000))
    , icomControlTask_(icom::IcomSocketTask::CONTROL_SOCKET)
    , icomAudioTask_(icom::IcomSocketTask::AUDIO_SOCKET)
    , icomCIVTask_(icom::IcomSocketTask::CIV_SOCKET)
    , freedvHandler_(freedvHandler)
    , tlv320Handler_(tlv320Handler)
    , overrideWifiSettings_(false)
    , wifiRunning_(false)
{
    registerMessageHandler(this, &WirelessTask::onRadioStateChange_);
    registerMessageHandler(this, &WirelessTask::onWifiSettingsMessage_);
}

WirelessTask::~WirelessTask()
{
    // empty
}

void WirelessTask::setWiFiOverride(bool wifiOverride)
{
    overrideWifiSettings_ = wifiOverride;
}

void WirelessTask::onTaskStart_()
{
    icomControlTask_.start();
    icomAudioTask_.start();
    icomCIVTask_.start();
}

void WirelessTask::onTaskWake_()
{
    icomControlTask_.wake();
    icomAudioTask_.wake();
    icomCIVTask_.wake();
}

void WirelessTask::onTaskSleep_()
{
    // Audio and CIV need to stop before control
    icomAudioTask_.sleep();
    icomCIVTask_.sleep();
    waitForSleep(&icomAudioTask_, pdMS_TO_TICKS(1000));
    waitForSleep(&icomCIVTask_, pdMS_TO_TICKS(1000));
    icomControlTask_.sleep();
    waitForSleep(&icomControlTask_, pdMS_TO_TICKS(1000));
    
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

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = 0, // Will auto-determine length on start.
            .channel = DEFAULT_AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = MAX_AP_CONNECTIONS,
            .pmf_cfg = {
                    .required = false,
            },
        }
    };
    
    // Append last two bytes of MAC address to default SSID.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    sprintf((char*)wifi_config.ap.ssid, "%s%02x%02x", DEFAULT_AP_NAME_PREFIX, mac[4], mac[5]);
    
    sprintf((char*)wifi_config.ap.password, "%s", "");
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void WirelessTask::enableWifi_(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    if (mode == storage::WifiMode::ACCESS_POINT)
    {
        esp_netif_create_default_wifi_ap();
    }
    else if (mode == storage::WifiMode::CLIENT)
    {
         esp_netif_create_default_wifi_sta();
    }
    else
    {
        assert(0);
    }
    
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
                                                        
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Disable power save.
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (mode == storage::WifiMode::ACCESS_POINT)
    {
        wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
        
        switch(security)
        {
            case storage::WifiSecurityMode::NONE:
                auth_mode = WIFI_AUTH_OPEN;
                break;
            case storage::WifiSecurityMode::WEP:
                auth_mode = WIFI_AUTH_WEP;
                break;
            case storage::WifiSecurityMode::WPA:
                auth_mode = WIFI_AUTH_WPA_PSK;
                break;
            case storage::WifiSecurityMode::WPA2:
                auth_mode = WIFI_AUTH_WPA2_PSK;
                break;
            case storage::WifiSecurityMode::WPA_AND_WPA2:
                auth_mode = WIFI_AUTH_WPA_WPA2_PSK;
                break;
            case storage::WifiSecurityMode::WPA3:
                auth_mode = WIFI_AUTH_WPA3_PSK;
                break;
            case storage::WifiSecurityMode::WPA2_AND_WPA3:
                auth_mode = WIFI_AUTH_WPA2_WPA3_PSK;
                break;
            default:
                assert(0);
                break;
        }
        wifi_config_t wifi_config = {
            .ap = {
                .ssid_len = 0, // Will auto-determine length on start.
                .channel = (uint8_t)channel,
                .authmode = auth_mode,
                .max_connection = MAX_AP_CONNECTIONS,
                .pmf_cfg = {
                        .required = false,
                },
            }
        };
        
        sprintf((char*)wifi_config.ap.ssid, "%s", ssid);
        sprintf((char*)wifi_config.ap.password, "%s", password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

        // Force to HT20 mode to better handle congested airspace
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));

        ESP_ERROR_CHECK(esp_wifi_start());
    }
    else
    {
        wifi_config_t wifi_config = {
            .sta = {
                .scan_method = WIFI_FAST_SCAN,
                .bssid_set = false,
                .bssid = "",
                .channel = 0,
                .listen_interval = 0,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            }
        };
        
        sprintf((char*)wifi_config.sta.ssid, "%s", ssid);
        sprintf((char*)wifi_config.sta.password, "%s", password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        // Force to HT20 mode to better handle congested airspace
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));

        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

void WirelessTask::disableWifi_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Shutting down Wi-Fi");

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
        httpServerTask_.sleep();
    }
}

void WirelessTask::onNetworkConnected_()
{
    WirelessNetworkStatusMessage message(true);
    publish(&message);

    wifiRunning_ = true;
    
    enableHttp_();
    
    // Get the current Icom radio settings
    storage::RequestRadioSettingsMessage request;
    publish(&request);

    auto response = waitFor<storage::RadioSettingsMessage>(pdMS_TO_TICKS(2000), nullptr);
    if (response != nullptr)
    {
        if (response->enabled)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Starting Icom radio connectivity");
            icom::IcomConnectRadioMessage connectMessage(response->host, response->port, response->username, response->password);
            publish(&connectMessage);
        }
        else
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Icom radio connectivity disabled");
        }
        
        delete response;
    }
    else
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Timed out waiting for radio connection settings!");
    }
}

void WirelessTask::onNetworkDisconnected_()
{
    // Force immediate state transition to idle for the IC-705 tasks.
    icomControlTask_.sleep();
    icomAudioTask_.sleep();
    icomCIVTask_.sleep();

    // Shut down HTTP server.
    disableHttp_();

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
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "rerouting audio pipes internally");

        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL, 
            freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
        );
        
        icomAudioTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
            nullptr
        );

        freedvHandler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
            tlv320Handler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
        );
    }
}

void WirelessTask::onWifiSettingsMessage_(DVTask* origin, storage::WifiSettingsMessage* message)
{
    // Avoid accidentally trying to re-initialize Wi-Fi.
    if (!wifiRunning_)
    {
        if (overrideWifiSettings_)
        {
            // Setup is *just* different enough that we have to have a separate function for it
            // (we can't get the MAC address w/o bringing up Wi-Fi first, and that's not possible
            // with enableWifi_()).
            enableDefaultWifi_();
        }
        else if (message->enabled)
        {
            enableWifi_(message->mode, message->security, message->channel, message->ssid, message->password);
        }
    }
}

}

}