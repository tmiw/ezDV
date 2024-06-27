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

#include "hal/eth_types.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"

#include "NetworkTask.h"
#include "HttpServerTask.h"
#include "NetworkMessage.h"

#include "interfaces/EthernetInterface.h"
#include "interfaces/WirelessInterface.h"

#define DEFAULT_AP_CHANNEL (1)

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE 256
#define CURRENT_LOG_TAG "NetworkTask"

#define ETHERNET_SPI_HOST (SPI2_HOST)
#define ETHERNET_CLOCK_SPEED_HZ (SPI_MASTER_FREQ_16M)
#define GPIO_ETHERNET_SPI_SCLK (42)
#define GPIO_ETHERNET_SPI_MISO (41)
#define GPIO_ETHERNET_SPI_MOSI (40)
#define GPIO_ETHERNET_SPI_CS (39)
#define GPIO_ETHERNET_INTERRUPT (38)

extern "C"
{
    DV_EVENT_DEFINE_BASE(WIRELESS_TASK_MESSAGE);
}

namespace ezdv
{

namespace network
{
    
NetworkTask::NetworkTask(audio::AudioInput* freedvHandler, audio::AudioInput* tlv320Handler, audio::AudioInput* audioMixer, audio::VoiceKeyerTask* vkTask)
    : ezdv::task::DVTask("NetworkTask", 5, 4096, tskNO_AFFINITY, 128)
    , wifiScanTimer_(this, this, &NetworkTask::triggerWifiScan_, 5000000, "WifiScanTimer") // 5 seconds between Wi-Fi scans
    , icomRestartTimer_(this, this, &NetworkTask::restartIcomConnection_, 10000000, "IcomRestartTimer") // 10 seconds, then restart Icom control task.
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
    , wifiInterface_(nullptr)
{
    registerMessageHandler(this, &NetworkTask::onRadioStateChange_);
    registerMessageHandler(this, &NetworkTask::onWifiSettingsMessage_);

    registerMessageHandler(this, &NetworkTask::onWifiScanStartMessage_);
    registerMessageHandler(this, &NetworkTask::onWifiScanStopMessage_);

    // Handlers for internal messages (intended to make events that happen
    // on ESP-IDF tasks happen on this one instead).
    registerMessageHandler(this, &NetworkTask::onApAssignedIpMessage_);
    registerMessageHandler(this, &NetworkTask::onStaAssignedIpMessage_);
    registerMessageHandler(this, &NetworkTask::onWifiScanCompletedMessage_);
    registerMessageHandler(this, &NetworkTask::onApStartedMessage_);
    registerMessageHandler(this, &NetworkTask::onNetworkDownMessage_);
    registerMessageHandler(this, &NetworkTask::onDeviceDisconnectedMessage_);
}

NetworkTask::~NetworkTask()
{
    // empty
}

void NetworkTask::setWiFiOverride(bool wifiOverride)
{
    overrideWifiSettings_ = wifiOverride;
}

void NetworkTask::onTaskStart_()
{
    isAwake_ = true;
}

void NetworkTask::onTaskSleep_()
{
    isAwake_ = false;
    
    // Stop reporting
    if (freeDVReporterTask_.isAwake())
    {
        sleep(&freeDVReporterTask_, pdMS_TO_TICKS(2000));
    }

    if (pskReporterTask_.isAwake())
    {
        sleep(&pskReporterTask_, pdMS_TO_TICKS(1000));
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

void NetworkTask::disableWifi_()
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

    // Stop interfaces
    for (auto& iface : interfaceList_)
    {
        iface->tearDown();
        delete iface;
    }
    interfaceList_.clear();
}

void NetworkTask::enableHttp_()
{
    httpServerTask_.start();
}

void NetworkTask::disableHttp_()
{
    if (wifiRunning_)
    {
        sleep(&httpServerTask_, pdMS_TO_TICKS(1000));
    }
}

void NetworkTask::onNetworkUp_()
{
    WirelessNetworkStatusMessage message(true);
    publish(&message);

    wifiRunning_ = true;
    
    enableHttp_();
}

void NetworkTask::onNetworkConnected_(bool client, char* ip, uint8_t* macAddress)
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

        if (!pskReporterTask_.isAwake())
        {
            start(&pskReporterTask_, pdMS_TO_TICKS(1000));
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

void NetworkTask::onNetworkDisconnected_()
{
    // Stop Icom reset timer if needed.
    icomRestartTimer_.stop();
    
    // Force immediate state transition to idle for the radio tasks.
    if (freeDVReporterTask_.isAwake())
    {
        sleep(&freeDVReporterTask_, pdMS_TO_TICKS(2000));
    }
    
    if (pskReporterTask_.isAwake())
    {
        sleep(&pskReporterTask_, pdMS_TO_TICKS(1000));
    }

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
        sleep(flexTcpTask_, pdMS_TO_TICKS(5000));
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

void NetworkTask::onRadioStateChange_(DVTask* origin, RadioConnectionStatusMessage* message)
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

void NetworkTask::onWifiSettingsMessage_(DVTask* origin, storage::WifiSettingsMessage* message)
{
    // Avoid accidentally trying to re-initialize Wi-Fi.
    if (!wifiRunning_)
    {
        // Start event loop.
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        
        interfaces::INetworkInterface::OnNetworkUpDownHandlerType apUpHandler = [&](interfaces::INetworkInterface&)
        {
            if (numInterfacesRunning_() == 1)
            {
                ApStartedMessage message;
                post(&message);
            }
        };
        
        interfaces::INetworkInterface::OnNetworkIpAssignedType staUpHandler = [&](interfaces::INetworkInterface&, std::string ip)
        {
            if (numInterfacesRunning_() == 1)
            {
                StaAssignedIpMessage message(ip.c_str());
                post(&message);
            }
        };
        
        interfaces::INetworkInterface::OnNetworkUpDownHandlerType downHandler = [&](interfaces::INetworkInterface&)
        {
            if (numInterfacesRunning_() == 0)
            {
                NetworkDownMessage message;
                post(&message);
            }
        };
        
        // Ethernet should always be started in case we're plugged into the network.
        auto ethInterface = new interfaces::EthernetInterface();
        assert(ethInterface != nullptr);
        ethInterface->setOnIpAddressAssigned(staUpHandler);
        ethInterface->setOnNetworkDown(downHandler);
        interfaceList_.push_back(ethInterface);

        // Create interface for built-in WiFi
        wifiInterface_ = new interfaces::WirelessInterface();
        assert(wifiInterface_ != nullptr);
        interfaceList_.push_back(wifiInterface_);
        
        bool isAccessPoint = false;
        if (overrideWifiSettings_)
        {
            // An empty access point name results in automatically using "ezDV xxxx" instead.            
            wifiInterface_->configure(storage::ACCESS_POINT, storage::NONE, DEFAULT_AP_CHANNEL, "", "");
            isAccessPoint = true;
        }
        else if (message->enabled)
        {
            wifiInterface_->configure(message->mode, message->security, message->channel, message->ssid, message->password);            
            isAccessPoint = message->mode == storage::ACCESS_POINT;
        }
        
        // Set WiFi event handlers
        if (isAccessPoint)
        {
            wifiInterface_->setOnNetworkUp(apUpHandler);
        }
        else
        {
            wifiInterface_->setOnIpAddressAssigned(staUpHandler);
        }
        wifiInterface_->setOnNetworkDown(downHandler);
        
        wifiInterface_->setOnWirelessScanComplete([&](interfaces::INetworkInterface&) {
            WifiScanCompletedMessage message;
            post(&message);
        });
        
        // Start interfaces
        for (auto& iface : interfaceList_)
        {
            iface->bringUp();
            iface->setHostname(message->hostname);
        }
        
        // Start SNTP
        esp_sntp_init();
        esp_sntp_setservername(0, "pool.ntp.org");
    }
}

int NetworkTask::numInterfacesRunning_()
{
    int count = 0;
    for (auto& iface : interfaceList_)
    {
        if (iface->status() == interfaces::INetworkInterface::INTERFACE_UP)
        {
            count++;
        }
    }
    
    return count;
}

void NetworkTask::restartIcomConnection_(DVTimer*)
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

void NetworkTask::triggerWifiScan_(DVTimer*)
{
    wifiInterface_->beginScan();
}

void NetworkTask::onWifiScanComplete_()
{
    // Get the number of Wi-Fi networks found. We'll need to use
    // this to allocate the correct amount of RAM to store the Wi-Fi APs found.
    uint16_t numNetworks = 0;
    auto result = esp_wifi_scan_get_ap_num(&numNetworks);
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED)
    {
        // Force a crash.
        ESP_ERROR_CHECK(result);
    }
    else if (result == ESP_ERR_WIFI_NOT_STARTED)
    {
        // If Wi-Fi is not running, there's no point in continuing.
        return;
    }

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

void NetworkTask::onWifiScanStartMessage_(DVTask* origin, StartWifiScanMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting Wi-Fi scan");
    
    wifiScanTimer_.stop();
    triggerWifiScan_(nullptr);
}

void NetworkTask::onWifiScanStopMessage_(DVTask* origin, StopWifiScanMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Stopping Wi-Fi scan");
    
    wifiScanTimer_.stop();
}

void NetworkTask::onApAssignedIpMessage_(DVTask* origin, ApAssignedIpMessage* message)
{
    onNetworkConnected_(false, message->ipString, message->macAddress);
}

void NetworkTask::onStaAssignedIpMessage_(DVTask* origin, StaAssignedIpMessage* message)
{
    onNetworkUp_();
    onNetworkConnected_(true, message->ipString, nullptr);
}

void NetworkTask::onWifiScanCompletedMessage_(DVTask* origin, WifiScanCompletedMessage* message)
{
    onWifiScanComplete_();
}

void NetworkTask::onApStartedMessage_(DVTask* origin, ApStartedMessage* message)
{
    onNetworkUp_();
}

void NetworkTask::onNetworkDownMessage_(DVTask* origin, NetworkDownMessage* message)
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

void NetworkTask::onDeviceDisconnectedMessage_(DVTask* origin, DeviceDisconnectedMessage* message)
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
