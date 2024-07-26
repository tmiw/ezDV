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

#ifndef HTTP_SERVER_TASK_H
#define HTTP_SERVER_TASK_H

#include <vector>
#include <deque>

#include "esp_event.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "task/DVTask.h"
#include "audio/FreeDVMessage.h"
#include "audio/VoiceKeyerMessage.h"
#include "driver/BatteryMessage.h"
#include "storage/SoftwareUpdateMessage.h"
#include "network/NetworkMessage.h"
#include "network/flex/FlexMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(HTTP_SERVER_MESSAGE);
}

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

class HttpServerTask : public DVTask
{
public:
    HttpServerTask();
    virtual ~HttpServerTask();
        
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;
    
private:
    bool firmwareUploadInProgress_;
    
    enum HttpRequestId 
    {
        WEBSOCKET_CONNECTED = 1,
        WEBSOCKET_DISCONNECTED = 2,
        UPDATE_WIFI = 3,
        UPDATE_RADIO = 4,
        UPDATE_VOICE_KEYER = 5,
        BEGIN_UPLOAD_VOICE_KEYER_FILE = 6,
        UPDATE_REPORTING = 7,
        UPDATE_LED_BRIGHTNESS = 8,
        SET_MODE = 9,
        START_STOP_VOICE_KEYER = 10,
        REBOOT_DEVICE = 11,
        START_WIFI_SCAN = 12,
        STOP_WIFI_SCAN = 13,
        SERVE_STATIC_FILE = 14,
    };
    
    template<uint32_t MSG_ID>
    class HttpEventMessageCommon : public DVTaskMessageBase<MSG_ID, HttpEventMessageCommon<MSG_ID>>
    {
    public:
        HttpEventMessageCommon(int fdProvided = 0)
            : DVTaskMessageBase<MSG_ID, HttpEventMessageCommon<MSG_ID>>(HTTP_SERVER_MESSAGE)
            , fd(fdProvided)
            {}
        virtual ~HttpEventMessageCommon() = default;

        int fd;
    };
    
    using HttpWebsocketConnectedMessage = HttpEventMessageCommon<WEBSOCKET_CONNECTED>;
    using HttpWebsocketDisconnectedMessage = HttpEventMessageCommon<WEBSOCKET_DISCONNECTED>;
    
    template<uint32_t MSG_ID>
    class HttpRequestMessageCommon : public DVTaskMessageBase<MSG_ID, HttpRequestMessageCommon<MSG_ID>>
    {
    public:
        HttpRequestMessageCommon(int fdProvided = 0, cJSON* requestProvided = nullptr)
            : DVTaskMessageBase<MSG_ID, HttpRequestMessageCommon<MSG_ID>>(HTTP_SERVER_MESSAGE)
            , fd(fdProvided)
            , request(requestProvided)
            {}
        virtual ~HttpRequestMessageCommon() = default;

        int fd;
        cJSON* request;
    };

    class HttpServeStaticFileMessage : public DVTaskMessageBase<SERVE_STATIC_FILE, HttpServeStaticFileMessage>
    {
    public:
        HttpServeStaticFileMessage(int fdProvided = 0, httpd_req_t* reqProvided = nullptr)
            : DVTaskMessageBase<SERVE_STATIC_FILE, HttpServeStaticFileMessage>(HTTP_SERVER_MESSAGE)
            , fd(fdProvided)
            , request(reqProvided)
            {}
        virtual ~HttpServeStaticFileMessage() = default;

        int fd;
        httpd_req_t* request;
    };
    
    // Internal messages for handling requests
    using UpdateWifiMessage = HttpRequestMessageCommon<UPDATE_WIFI>;
    using UpdateRadioMessage = HttpRequestMessageCommon<UPDATE_RADIO>;
    using UpdateVoiceKeyerMessage = HttpRequestMessageCommon<UPDATE_VOICE_KEYER>;
    using BeginUploadVoiceKeyerFileMessage = HttpRequestMessageCommon<BEGIN_UPLOAD_VOICE_KEYER_FILE>;
    using UpdateReportingMessage = HttpRequestMessageCommon<UPDATE_REPORTING>;
    using UpdateLedBrightnessMessage = HttpRequestMessageCommon<UPDATE_LED_BRIGHTNESS>;
    using SetModeMessage = HttpRequestMessageCommon<SET_MODE>;
    using StartStopVoiceKeyerMessage = HttpRequestMessageCommon<START_STOP_VOICE_KEYER>;
    using RebootDeviceMessage = HttpRequestMessageCommon<REBOOT_DEVICE>;
    using StartWifiScanMessage = HttpRequestMessageCommon<START_WIFI_SCAN>;
    using StopWifiScanMessage = HttpRequestMessageCommon<STOP_WIFI_SCAN>;
    
    using WebSocketList = std::map<int, bool>; // int = socket ID, bool = currently scanning Wi-Fi networks
    
    httpd_handle_t configServerHandle_;
    WebSocketList activeWebSockets_;
    bool isRunning_;
    
    std::deque<int> freqOffsets_;
    int currFreqOffset_;
    int freqOffsetCount_;
    
    void onHttpWebsocketConnectedMessage_(DVTask* origin, HttpWebsocketConnectedMessage* message);
    void onHttpWebsocketDisconnectedMessage_(DVTask* origin, HttpWebsocketDisconnectedMessage* message);
    
    void onBatteryStateMessage_(DVTask* origin, driver::BatteryStateMessage* message);
    void onUpdateWifiMessage_(DVTask* origin, UpdateWifiMessage* message);
    void onUpdateRadioMessage_(DVTask* origin, UpdateRadioMessage* message);
    void onUpdateVoiceKeyerMessage_(DVTask* origin, UpdateVoiceKeyerMessage* message);
    void onUpdateReportingMessage_(DVTask* origin, UpdateReportingMessage* message);
    void onBeginUploadVoiceKeyerFileMessage_(DVTask* origin, BeginUploadVoiceKeyerFileMessage* message);
    void onFileUploadCompleteMessage_(DVTask* origin, audio::FileUploadCompleteMessage* message);
    void onFirmwareUpdateCompleteMessage_(DVTask* origin, storage::FirmwareUpdateCompleteMessage* message);
    void onUpdateLedBrightnessMessage_(DVTask* origin, UpdateLedBrightnessMessage* message);

    void onSetModeMessage_(DVTask* origin, SetModeMessage* message);
    void onSetFreeDVModeMessage_(DVTask* origin, audio::SetFreeDVModeMessage* message);

    void sendVoiceKeyerExecutionState_(bool state);
    void onStartStopVoiceKeyerMessage_(DVTask* origin, StartStopVoiceKeyerMessage* message);
    void onStartVoiceKeyerMessage_(DVTask* origin, audio::StartVoiceKeyerMessage* message);
    void onStopVoiceKeyerMessage_(DVTask* origin, audio::StopVoiceKeyerMessage* message);
    void onVoiceKeyerCompleteMessage_(DVTask* origin, audio::VoiceKeyerCompleteMessage* message);
    
    void onFlexRadioDiscoveredMessage_(DVTask* origin, network::flex::FlexRadioDiscoveredMessage* message);
    
    void onRebootDeviceMessage_(DVTask* origin, RebootDeviceMessage* message);

    void onStartWifiScanMessage_(DVTask* origin, HttpServerTask::StartWifiScanMessage* message);
    void onStopWifiScanMessage_(DVTask* origin, HttpServerTask::StopWifiScanMessage* message);
    void onWifiNetworkListMessage_(DVTask* origin, WifiNetworkListMessage* message);
    
    // FreeDV sync state reporting
    void onFreeDVSyncStateMessage_(DVTask* origin, audio::FreeDVSyncStateMessage* message);

    // Helper to asynchronously serve static files.
    void onHttpServeStaticFileMessage_(DVTask* origin, HttpServeStaticFileMessage* message);
    
    void sendJSONMessage_(cJSON* message, WebSocketList& socketList);
    
    static esp_err_t ServeWebsocketPage_(httpd_req_t *req);
    static esp_err_t ServeStaticPage_(httpd_req_t *req);
};

}

}

#endif // HTTP_SERVER_TASK_H