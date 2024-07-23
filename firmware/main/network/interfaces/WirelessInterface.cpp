/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2024 Mooneer Salem
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

#include "WirelessInterface.h"

#include "esp_wifi.h"
#include "esp_log.h"

#define CURRENT_LOG_TAG "WirelessInterface"
#define MAX_AP_CONNECTIONS (5)
#define DEFAULT_AP_NAME_PREFIX "ezDV "

namespace ezdv
{

namespace network
{
    
namespace interfaces
{

WirelessInterface::WirelessInterface()
{
    // empty
}

WirelessInterface::~WirelessInterface()
{
    // empty
}

void WirelessInterface::configure(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password)
{
    if (mode == storage::WifiMode::ACCESS_POINT)
    {
        interfaceHandle_ = esp_netif_create_default_wifi_ap();
    }
    else if (mode == storage::WifiMode::CLIENT)
    {
        interfaceHandle_ = esp_netif_create_default_wifi_sta();
    }
    else
    {
        assert(0);
    }

    ESP_LOGI(CURRENT_LOG_TAG, "Setting ezDV SSID to %s", ssid);
                                                        
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Disable power save.
    //esp_wifi_set_ps(WIFI_PS_NONE);

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
#if 0
            case storage::WifiSecurityMode::WPA3:
                auth_mode = WIFI_AUTH_WPA3_PSK;
                break;
            case storage::WifiSecurityMode::WPA2_AND_WPA3:
                auth_mode = WIFI_AUTH_WPA2_WPA3_PSK;
                break;
#endif // 0
            default:
                assert(0);
                break;
        }
        
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));

        wifi_config.ap.ssid_len = 0; // Will auto-determine length on start.
        wifi_config.ap.channel = (uint8_t)channel;
        wifi_config.ap.authmode = auth_mode;
        wifi_config.ap.max_connection = MAX_AP_CONNECTIONS;
        wifi_config.ap.pmf_cfg.required = false;
        
        if (strlen(ssid) == 0)
        {
            // Append last two bytes of MAC address to default SSID.
            uint8_t mac[6];
            ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
            sprintf((char*)wifi_config.ap.ssid, "%s%02x%02x", DEFAULT_AP_NAME_PREFIX, mac[4], mac[5]);
        }
        else
        {
            sprintf((char*)wifi_config.ap.ssid, "%s", ssid);
        }
        sprintf((char*)wifi_config.ap.password, "%s", password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    }
    else
    {
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));

        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
        wifi_config.sta.bssid_set = false;
        memset(wifi_config.sta.bssid, 0, sizeof(wifi_config.sta.bssid));
        wifi_config.sta.channel = 0;
        wifi_config.sta.listen_interval = 0;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

        // Enable fast roaming (typically for mesh networks or enterprise setups)
        wifi_config.sta.btm_enabled = 1;
        wifi_config.sta.rm_enabled = 1;
        wifi_config.sta.mbo_enabled = 1;
        wifi_config.sta.ft_enabled = 1;
        
        sprintf((char*)wifi_config.sta.ssid, "%s", ssid);
        sprintf((char*)wifi_config.sta.password, "%s", password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
    
    // Force to HT20 mode to better handle congested airspace
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
}

void WirelessInterface::bringUp()
{
    wifi_mode_t mode;
    bool connectToAp = true;
    
    if (interfaceHandle_ == nullptr)
    {
        // Configure as client without any settings. This is required
        // to ensure Wi-Fi scanning still works.
        connectToAp = false;
        interfaceHandle_ = esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));

        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
        wifi_config.sta.bssid_set = false;
        memset(wifi_config.sta.bssid, 0, sizeof(wifi_config.sta.bssid));
        wifi_config.sta.channel = 0;
        wifi_config.sta.listen_interval = 0;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

        // Enable fast roaming (typically for mesh networks or enterprise setups)
        wifi_config.sta.btm_enabled = 1;
        wifi_config.sta.rm_enabled = 1;
        wifi_config.sta.mbo_enabled = 1;
        wifi_config.sta.ft_enabled = 1;
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
    
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
    
    ESP_ERROR_CHECK(esp_wifi_start());
    status_ = INTERFACE_DEV_UP;
    
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));
    
    if (mode == WIFI_MODE_STA && connectToAp)
    {
        // Connect to AP
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

void WirelessInterface::tearDown()
{
    status_ = INTERFACE_SHUTTING_DOWN;
    
    esp_event_handler_instance_unregister(WIFI_EVENT,
                                         ESP_EVENT_ANY_ID,
                                         &wifiEventHandle_);
    esp_event_handler_instance_unregister(IP_EVENT,
                                          ESP_EVENT_ANY_ID,
                                          &ipEventHandle_);

    esp_wifi_disconnect();
}

void WirelessInterface::getMacAddress(uint8_t* mac)
{
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
}

void WirelessInterface::setOnApAssignedIp(OnNetworkIpAssignedType fn)
{
    onApAssignedIp_ = fn;
}

void WirelessInterface::setOnWirelessApDeviceDisconnected(OnWirelessApDeviceDisconnectedType fn)
{
    onWirelessApDeviceDisconnected_ = fn;
}

void WirelessInterface::setOnWirelessScanComplete(OnWirelessScanCompleteType fn)
{
    onWirelessScanComplete_ = fn;
}

void WirelessInterface::beginScan()
{
    // Start Wi-Fi scan using default config. A WIFI_EVENT_SCAN_DONE event
    // will be fired by ESP-IDF once the scan is done.
    ESP_ERROR_CHECK(esp_wifi_scan_start(nullptr, false));
}

void WirelessInterface::IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(CURRENT_LOG_TAG, "IP event: %ld", event_id);
    
    WirelessInterface* obj = (WirelessInterface*)event_handler_arg;
    
    switch(event_id)
    {
        case IP_EVENT_AP_STAIPASSIGNED:
        {
            ip_event_ap_staipassigned_t* ipData = (ip_event_ap_staipassigned_t*)event_data;
            if (ipData->esp_netif == obj->interfaceHandle_)
            {
                char buf[32];
                sprintf(buf, IPSTR, IP2STR(&ipData->ip));
            
                ESP_LOGI(CURRENT_LOG_TAG, "Assigned IP %s to client", buf);
            
                if (obj->onApAssignedIp_)
                {
                    obj->onApAssignedIp_(*obj, buf);
                }
            }
            break;
        }
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t* ipData = (ip_event_got_ip_t*)event_data;
            if (ipData->esp_netif == obj->interfaceHandle_)
            {
                char buf[32];
                sprintf(buf, IPSTR, IP2STR(&ipData->ip_info.ip));

                ESP_LOGI(CURRENT_LOG_TAG, "Got IP address %s from DHCP server", buf);
            
                obj->status_ = INTERFACE_IP_UP;
                if (obj->onNetworkUpFn_)
                {
                    obj->onNetworkUpFn_(*obj);
                }
            
                if (obj->onIpAddressAssignedFn_)
                {
                    obj->onIpAddressAssignedFn_(*obj, buf);
                }
            }
            break;
        }
    }
}

void WirelessInterface::WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WirelessInterface* obj = (WirelessInterface*)event_handler_arg;
    
    ESP_LOGI(CURRENT_LOG_TAG, "Wi-Fi event: %ld", event_id);
    
    switch (event_id)
    {
        case WIFI_EVENT_SCAN_DONE:
        {
            if (obj->onWirelessScanComplete_)
            {
                obj->onWirelessScanComplete_(*obj);
            }
            break;
        }
        case WIFI_EVENT_AP_START:
        {
            // In AP mode, we're using a static IP (129.168.4.1)
            // and thus can assume we're running.
            obj->status_ = INTERFACE_IP_UP;
            if (obj->onNetworkUpFn_)
            {
                obj->onNetworkUpFn_(*obj);
            }
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
            
            if (networkIsDown && obj->onNetworkDownFn_)
            {
                if (obj->status_ != INTERFACE_SHUTTING_DOWN)
                {
                    obj->status_ = INTERFACE_DEV_UP;
                }
                obj->onNetworkDownFn_(*obj);
            }
            
            // Reattempt connection to access point if we couldn't find
            // it the first time around.
            if (obj->status_ == INTERFACE_SHUTTING_DOWN)
            {
                obj->status_ = INTERFACE_DOWN;
                esp_wifi_stop();
            }
            else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
            {
                esp_wifi_disconnect();
                ESP_ERROR_CHECK(esp_wifi_connect());
            }
            
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
            
            if (obj->onWirelessApDeviceDisconnected_)
            {
                obj->onWirelessApDeviceDisconnected_(*obj, event->mac);
            }
            break;
        }
    }
}

}

}

}