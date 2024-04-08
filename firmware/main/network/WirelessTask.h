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

#ifndef WIRELESS_TASK_H
#define WIRELESS_TASK_H

#include "esp_event.h"
#include "esp_http_server.h"

#include "task/DVTask.h"
#include "icom/IcomSocketTask.h"
#include "flex/FlexTcpTask.h"
#include "flex/FlexVitaTask.h"
#include "FreeDVReporterTask.h"

#include "audio/AudioInput.h"
#include "audio/VoiceKeyerTask.h"

#include "NetworkMessage.h"
#include "storage/SettingsMessage.h"

#include "HttpServerTask.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(WIRELESS_TASK_MESSAGE);
}

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

class HttpServerTask;

/// @brief Handles wireless setup in the application.
class WirelessTask : public DVTask
{
public:
    WirelessTask(ezdv::audio::AudioInput* freedvHandler, ezdv::audio::AudioInput* tlv320Handler, ezdv::audio::AudioInput* audioMixerHandler, audio::VoiceKeyerTask* vkTask);
    virtual ~WirelessTask();
    
    void setWiFiOverride(bool wifiOverride);
    
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;
    
private:
    enum WirelessTaskRequestId 
    {
        AP_ASSIGNED_IP = 1,
        STA_ASSIGNED_IP = 2,
        WIFI_SCAN_COMPLETED = 3,
        AP_STARTED = 4,
        NETWORK_DOWN = 5,
        DEVICE_DISCONNECTED = 6,
    };

    class ApAssignedIpMessage : public DVTaskMessageBase<AP_ASSIGNED_IP, ApAssignedIpMessage>
    {
    public:
        ApAssignedIpMessage(const char* ipStringProvided = nullptr, uint8_t* macProvided = nullptr)
            : DVTaskMessageBase<AP_ASSIGNED_IP, ApAssignedIpMessage>(WIRELESS_TASK_MESSAGE)
        {
            memset(ipString, 0, sizeof(ipString));
            memset(macAddress, 0, sizeof(macAddress));

            if (ipStringProvided != nullptr)
            {
                strncpy(ipString, ipStringProvided, sizeof(ipString) - 1);
            }

            if (macProvided != nullptr)
            {
                memcpy(macAddress, macProvided, sizeof(macAddress));
            }
        }
        virtual ~ApAssignedIpMessage() = default;

        char ipString[32];
        uint8_t macAddress[6];
    };

    class StaAssignedIpMessage : public DVTaskMessageBase<STA_ASSIGNED_IP, StaAssignedIpMessage>
    {
    public:
        StaAssignedIpMessage(const char* ipStringProvided = nullptr)
            : DVTaskMessageBase<STA_ASSIGNED_IP, StaAssignedIpMessage>(WIRELESS_TASK_MESSAGE)
        {
            memset(ipString, 0, sizeof(ipString));

            if (ipStringProvided != nullptr)
            {
                strncpy(ipString, ipStringProvided, sizeof(ipString) - 1);
            }
        }
        virtual ~StaAssignedIpMessage() = default;

        char ipString[32];
    };

    template<uint32_t MSG_ID>
    class ZeroArgumentMessageCommon : public DVTaskMessageBase<MSG_ID, ZeroArgumentMessageCommon<MSG_ID> >
    {
    public:
        ZeroArgumentMessageCommon()
            : DVTaskMessageBase<MSG_ID, ZeroArgumentMessageCommon<MSG_ID> >(WIRELESS_TASK_MESSAGE)
        {
        }
        virtual ~ZeroArgumentMessageCommon() = default;
    };

    using WifiScanCompletedMessage = ZeroArgumentMessageCommon<WIFI_SCAN_COMPLETED>;
    using ApStartedMessage = ZeroArgumentMessageCommon<AP_STARTED>;
    using NetworkDownMessage = ZeroArgumentMessageCommon<NETWORK_DOWN>;

    class DeviceDisconnectedMessage : public DVTaskMessageBase<DEVICE_DISCONNECTED, DeviceDisconnectedMessage>
    {
    public:
        DeviceDisconnectedMessage(uint8_t* macProvided = nullptr)
            : DVTaskMessageBase<DEVICE_DISCONNECTED, DeviceDisconnectedMessage>(WIRELESS_TASK_MESSAGE)
        {
            memset(macAddress, 0, sizeof(macAddress));

            if (macProvided != nullptr)
            {
                memcpy(macAddress, macProvided, sizeof(macAddress));
            }
        }
        virtual ~DeviceDisconnectedMessage() = default;

        uint8_t macAddress[6];
    };

    DVTimer wifiScanTimer_;
    DVTimer icomRestartTimer_;
    HttpServerTask httpServerTask_;
    icom::IcomSocketTask* icomControlTask_;
    icom::IcomSocketTask* icomAudioTask_;
    icom::IcomSocketTask* icomCIVTask_;
    flex::FlexTcpTask* flexTcpTask_;
    flex::FlexVitaTask* flexVitaTask_;
    FreeDVReporterTask freeDVReporterTask_;
    
    // for rerouting audio after connection
    ezdv::audio::AudioInput* freedvHandler_;
    ezdv::audio::AudioInput* tlv320Handler_; 
    ezdv::audio::AudioInput* audioMixerHandler_; 
    ezdv::audio::VoiceKeyerTask* vkTask_; 
    
    bool isAwake_;
    bool overrideWifiSettings_;
    bool wifiRunning_;
    bool radioRunning_;
    int radioType_;
    esp_event_handler_instance_t wifiEventHandle_;
    esp_event_handler_instance_t  ipEventHandle_;
    uint8_t radioMac_[6];
    esp_netif_t* netif_;
        
    void enableWifi_(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password, char* hostname);
    void enableDefaultWifi_();
    
    void disableWifi_();
    void enableHttp_();
    void disableHttp_();
    
    void onNetworkUp_();
    void onNetworkConnected_(bool client, char* ip, uint8_t* macAddress);
    void onNetworkDisconnected_();
    
    void onRadioStateChange_(DVTask* origin, RadioConnectionStatusMessage* message);
    void onWifiSettingsMessage_(DVTask* origin, storage::WifiSettingsMessage* message);
    void onWifiScanStartMessage_(DVTask* origin, StartWifiScanMessage* message);
    void onWifiScanStopMessage_(DVTask* origin, StopWifiScanMessage* message);
    
    void restartIcomConnection_(DVTimer*);
    void triggerWifiScan_(DVTimer*);
    void onWifiScanComplete_();
    
    void onApAssignedIpMessage_(DVTask* origin, ApAssignedIpMessage* message);
    void onStaAssignedIpMessage_(DVTask* origin, StaAssignedIpMessage* message);
    void onWifiScanCompletedMessage_(DVTask* origin, WifiScanCompletedMessage* message);
    void onApStartedMessage_(DVTask* origin, ApStartedMessage* message);
    void onNetworkDownMessage_(DVTask* origin, NetworkDownMessage* message);
    void onDeviceDisconnectedMessage_(DVTask* origin, DeviceDisconnectedMessage* message);

    static void WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};

}

}

#endif // WIRELESS_TASK_H