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

#include "audio/AudioInput.h"

#include "NetworkMessage.h"
#include "storage/SettingsMessage.h"

#include "HttpServerTask.h"

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
    WirelessTask(ezdv::audio::AudioInput* freedvHandler, ezdv::audio::AudioInput* tlv320Handler);
    virtual ~WirelessTask();
    
    void setWiFiOverride(bool wifiOverride);
    
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;
    
private:
    HttpServerTask httpServerTask_;
    icom::IcomSocketTask icomControlTask_;
    icom::IcomSocketTask icomAudioTask_;
    icom::IcomSocketTask icomCIVTask_;
    
    // for rerouting audio after connection
    ezdv::audio::AudioInput* freedvHandler_;
    ezdv::audio::AudioInput* tlv320Handler_; 
    
    bool overrideWifiSettings_;
    bool wifiRunning_;
    bool radioRunning_;
    esp_event_handler_instance_t wifiEventHandle_;
    esp_event_handler_instance_t  ipEventHandle_;
        
    void enableWifi_(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password);
    void enableDefaultWifi_();
    
    void disableWifi_();
    void enableHttp_();
    void disableHttp_();
    
    void onNetworkUp_();
    void onNetworkConnected_(bool client, char* ip);
    void onNetworkDisconnected_();
    
    void onRadioStateChange_(DVTask* origin, RadioConnectionStatusMessage* message);
    void onWifiSettingsMessage_(DVTask* origin, storage::WifiSettingsMessage* message);
    
    static void WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};

}

}

#endif // WIRELESS_TASK_H