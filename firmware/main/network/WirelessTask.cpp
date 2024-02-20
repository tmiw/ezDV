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
                NetworkDownMessage message;
                obj->post(&message);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED:
            {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
                DeviceDisconnectedMessage message(event->mac);
                obj->post(&message);
                break;
            }
        }
    }
}

WirelessTask::WirelessTask(audio::AudioInput* freedvHandler, audio::AudioInput* tlv320Handler, audio::AudioInput* audioMixer, audio::VoiceKeyerTask* vkTask)
    : ezdv::task::DVTask("WirelessTask", 1, 4096, tskNO_AFFINITY, 128)
    , wifiScanTimer_(this, std::bind(&WirelessTask::triggerWifiScan_, this), 5000000) // 5 seconds between Wi-Fi scans
    , icomRestartTimer_(this, std::bind(&WirelessTask::restartIcomConnection_, this), 10000000) // 10 seconds, then restart Icom control task.
    , icomControlTask_(nullptr)
    , icomAudioTask_(nullptr)
    , icomCIVTask_(nullptr)
    , flexTcpTask_(nullptr)
    , flexVitaTask_(nullptr)
    , freedvHandler_(freedvHandler)
    , tlv320Handler_(tlv320Handler)
    , audioMixerHandler_(audioMixer)
    , vkTask_(vkTask)
    , isAwake_(false)
    , overrideWifiSettings_(false)
    , wifiRunning_(false)
    , radioRunning_(false)
{
    registerMessageHandler(this, &WirelessTask::onRadioStateChange_);
    registerMessageHandler(this, &WirelessTask::onWifiSettingsMessage_);

    registerMessageHandler(this, &WirelessTask::onWifiScanStartMessage_);
    registerMessageHandler(this, &WirelessTask::onWifiScanStopMessage_);

    // Handlers for internal messages (intended to make events that happen
    // on ESP-IDF tasks happen on this one instead).
    registerMessageHandler(this, &WirelessTask::onApAssignedIpMessage_);
    registerMessageHandler(this, &WirelessTask::onStaAssignedIpMessage_);
    registerMessageHandler(this, &WirelessTask::onWifiScanCompletedMessage_);
    registerMessageHandler(this, &WirelessTask::onApStartedMessage_);
    registerMessageHandler(this, &WirelessTask::onNetworkDownMessage_);
    registerMessageHandler(this, &WirelessTask::onDeviceDisconnectedMessage_);
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
    isAwake_ = true;
}

void WirelessTask::onTaskSleep_()
{
    isAwake_ = false;
    
    // Stop reporting
    if (freeDVReporterTask_.isAwake())
    {
        sleep(&freeDVReporterTask_, pdMS_TO_TICKS(1000));
    }

    disableHttp_();
        
    // Audio and CIV need to stop before control
    if (icomAudioTask_ != nullptr)
    {
        sleep(icomAudioTask_, pdMS_TO_TICKS(1000));
        delete icomAudioTask_;
        icomAudioTask_ = nullptr;
    }

    if (icomCIVTask_ != nullptr)
    {
        sleep(icomCIVTask_, pdMS_TO_TICKS(1000));
        delete icomCIVTask_;
        icomCIVTask_ = nullptr;
    }

    if (icomControlTask_ != nullptr)
    {
        sleep(icomControlTask_, pdMS_TO_TICKS(1000));
        delete icomControlTask_;
        icomControlTask_ = nullptr;
    }

    if (flexVitaTask_ != nullptr)
    {
        sleep(flexVitaTask_, pdMS_TO_TICKS(1000));
        delete flexVitaTask_;
        flexVitaTask_ = nullptr;
    }

    if (flexTcpTask_ != nullptr)
    {
        sleep(flexTcpTask_, pdMS_TO_TICKS(1000));
        delete flexTcpTask_;
        flexTcpTask_ = nullptr;
    }
    
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
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void WirelessTask::enableWifi_(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password, char* hostname)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_t* netif = nullptr;
    if (mode == storage::WifiMode::ACCESS_POINT)
    {
        netif = esp_netif_create_default_wifi_ap();
    }
    else if (mode == storage::WifiMode::CLIENT)
    {
        netif = esp_netif_create_default_wifi_sta();
    }
    else
    {
        assert(0);
    }

    // Set hostname as configured by the user
    if (hostname != nullptr && strlen(hostname) > 0)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Setting ezDV hostname to %s", hostname);
        ESP_ERROR_CHECK(esp_netif_set_hostname(netif, hostname));
    }

    ESP_LOGI(CURRENT_LOG_TAG, "Setting ezDV SSID to %s", ssid);

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
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
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

        esp_sntp_init();
        esp_sntp_setservername(0, "pool.ntp.org");
    }
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
    if (radioRunning_)
    {
        // Don't reexecute if the radio is already running.
        return;
    }
    
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
    
    // Start the VITA task here since we need it to be able to 
    // get UDP broadcasts from the radio.
    if (flexVitaTask_ == nullptr)
    {
        flexVitaTask_ = new flex::FlexVitaTask();
        start(flexVitaTask_, pdMS_TO_TICKS(1000));
    }
    
    // Get the current Icom radio settings
    if (!overrideWifiSettings_)
    {
        if (!freeDVReporterTask_.isAwake())
        {
            start(&freeDVReporterTask_, pdMS_TO_TICKS(1000));
        }
        
        storage::RequestRadioSettingsMessage request;
        publish(&request);

        auto response = waitFor<storage::RadioSettingsMessage>(pdMS_TO_TICKS(2000), nullptr);
        if (response != nullptr)
        {
            if (response->enabled && !radioRunning_)
            {
                if (client || !strcmp(response->host, ip))
                {
                    // Grab MAC address for later use when disconnecting
                    // (to prevent reconnection when the radio is obviously offline).
                    if (macAddress != nullptr)
                    {
                        memcpy(radioMac_, macAddress, sizeof(radioMac_));
                    }
                    
                    radioType_ = response->type;
                    switch (response->type)
                    {
                        case 0:
                        {
                            ESP_LOGI(CURRENT_LOG_TAG, "Starting Icom radio connectivity");

                            icomControlTask_ = new icom::IcomSocketTask(icom::IcomSocketTask::CONTROL_SOCKET);
                            icomAudioTask_ = new icom::IcomSocketTask(icom::IcomSocketTask::AUDIO_SOCKET);
                            icomCIVTask_ = new icom::IcomSocketTask(icom::IcomSocketTask::CIV_SOCKET);

                            // Wait a bit, then start the connection.
                            icomRestartTimer_.start(true);
                            radioRunning_ = true;
                            break;
                        }
                        case 1:
                        {
                            ESP_LOGI(CURRENT_LOG_TAG, "Starting FlexRadio connectivity");

                            flexTcpTask_ = new flex::FlexTcpTask();
                            start(flexTcpTask_, pdMS_TO_TICKS(1000));

                            flex::FlexConnectRadioMessage connectMessage(response->host);
                            publish(&connectMessage);

                            radioRunning_ = true;
                            break;
                        }
                        default:
                        {
                            ESP_LOGW(CURRENT_LOG_TAG, "Unknown radio type %d, ignoring", response->type);
                            break;
                        }
                    }
                }
            }
            else
            {
                ESP_LOGI(CURRENT_LOG_TAG, "Radio connectivity disabled");
            }
            
            delete response;
        }
        else
        {
            ESP_LOGW(CURRENT_LOG_TAG, "Timed out waiting for radio connection settings!");
        }
    }
}

void WirelessTask::onNetworkDisconnected_()
{
    // Stop Icom reset timer if needed.
    icomRestartTimer_.stop();
    
    // Force immediate state transition to idle for the radio tasks.
    freeDVReporterTask_.sleep();

    if (icomControlTask_ != nullptr)
    {
        sleep(icomControlTask_, pdMS_TO_TICKS(1000));
        delete icomControlTask_;
        icomControlTask_ = nullptr;
    }

    if (icomAudioTask_ != nullptr)
    {
        sleep(icomAudioTask_, pdMS_TO_TICKS(1000));
        delete icomAudioTask_;
        icomAudioTask_ = nullptr;
    }

    if (icomCIVTask_ != nullptr)
    {
        sleep(icomCIVTask_, pdMS_TO_TICKS(1000));
        delete icomCIVTask_;
        icomCIVTask_ = nullptr;
    }

    if (flexTcpTask_ != nullptr)
    {
        sleep(flexTcpTask_, pdMS_TO_TICKS(1000));
        delete flexTcpTask_;
        flexTcpTask_ = nullptr;
    }

    if (flexVitaTask_ != nullptr)
    {
        sleep(flexVitaTask_, pdMS_TO_TICKS(1000));
        delete flexVitaTask_;
        flexVitaTask_ = nullptr;
    }

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
        
        if (radioType_ == 0)
        {
            icomAudioTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
                freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
        
            freedvHandler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
                icomAudioTask_->getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
            );
        }
        else if (radioType_ == 1)
        {
            // Flex 100% goes through SmartSDR, so disable TLV320 user port handling
            tlv320Handler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                nullptr
            );
            
            // Make sure voice keyer can restore Flex mic device once done.
            vkTask_->setMicDeviceTask(flexVitaTask_);
            
            flexVitaTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
            );
                
            flexVitaTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RIGHT_CHANNEL,
                freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
                
            freedvHandler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
                flexVitaTask_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
                
            audioMixerHandler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                flexVitaTask_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
            );
        }
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "rerouting audio pipes internally");

        // Make sure voice keyer can restore TLV320 mic device once done.
        vkTask_->setMicDeviceTask(tlv320Handler_);
        
        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
            freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
        );
            
        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL, 
            freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
        );
        
        if (icomAudioTask_ != nullptr)
        {
            icomAudioTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
                nullptr
            );
        }

        freedvHandler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
            tlv320Handler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
        );
            
        audioMixerHandler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
            tlv320Handler_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
        );
            
        if (radioType_ == 0)
        {
            // If Icom, we'll need to wake up the process again.
            // XXX - this is a special case as it puts itself to sleep as part of the
            // disconnect logic.
            icomRestartTimer_.start(true);
        }
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
            enableWifi_(message->mode, message->security, message->channel, message->ssid, message->password, message->hostname);
        }
    }
}

void WirelessTask::restartIcomConnection_()
{
    storage::RequestRadioSettingsMessage request;
    publish(&request);
    
    auto response = waitFor<storage::RadioSettingsMessage>(pdMS_TO_TICKS(2000), nullptr);
    if (response != nullptr)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Starting Icom radio connectivity");

        start(icomControlTask_, pdMS_TO_TICKS(1000));
        start(icomAudioTask_, pdMS_TO_TICKS(1000));
        start(icomCIVTask_, pdMS_TO_TICKS(1000));

        icom::IcomConnectRadioMessage connectMessage(response->host, response->port, response->username, response->password);
        publish(&connectMessage);
    }
    else
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Could not restart Icom processes!");
    }
}

void WirelessTask::triggerWifiScan_()
{
    // Start Wi-Fi scan using default config. A WIFI_EVENT_SCAN_DONE event
    // will be fired by ESP-IDF once the scan is done.
    ESP_ERROR_CHECK(esp_wifi_scan_start(nullptr, false));
}

void WirelessTask::onWifiScanComplete_()
{
    // Get the number of Wi-Fi networks found. We'll need to use
    // this to allocate the correct amount of RAM to store the Wi-Fi APs found.
    uint16_t numNetworks = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&numNetworks));

    // Always ensure we can allocate at least one AP record. This is to 
    // ensure the Wi-Fi code always frees up whatever it allocates to 
    // perform the scan, even if it doesn't find any other networks.
    numNetworks++;

    // Allocate the necessary memory to grab the AP list.
    wifi_ap_record_t* apRecords = (wifi_ap_record_t*)calloc(numNetworks, sizeof(wifi_ap_record_t));
    assert(apRecords != nullptr);

    // Grab the list of access points found.
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&numNetworks, apRecords));

    // Publish list of Wi-Fi networks to interested parties.
    WifiNetworkListMessage message(numNetworks, apRecords);
    publish(&message);

    // Restart Wi-Fi scan after a predefined time interval.
    wifiScanTimer_.start(true);
}

void WirelessTask::onWifiScanStartMessage_(DVTask* origin, StartWifiScanMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting Wi-Fi scan");
    
    wifiScanTimer_.stop();
    triggerWifiScan_();
}

void WirelessTask::onWifiScanStopMessage_(DVTask* origin, StopWifiScanMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Stopping Wi-Fi scan");
    
    wifiScanTimer_.stop();
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

void WirelessTask::onWifiScanCompletedMessage_(DVTask* origin, WifiScanCompletedMessage* message)
{
    onWifiScanComplete_();
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
        radioRunning_ = false;
        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

void WirelessTask::onDeviceDisconnectedMessage_(DVTask* origin, DeviceDisconnectedMessage* message)
{
    // Prevent attempted reconnection of radio if that's the device
    // that disconnected.
    if (memcmp(message->macAddress, radioMac_, sizeof(radioMac_)) == 0)
    {
        icomRestartTimer_.stop();
        radioRunning_ = false;
    }
}

}

}
