/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
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

#ifndef FREEDV_REPORTER_TASK_H
#define FREEDV_REPORTER_TASK_H

#include "esp_websocket_client.h"

#include "task/DVTask.h"
#include "ReportingMessage.h"
#include "audio/FreeDVMessage.h"
#include "storage/SettingsMessage.h"

struct cJSON; // forward declaration

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

extern "C"
{
    DV_EVENT_DECLARE_BASE(FREEDV_REPORTER_MESSAGE);
}

/// @brief Handles reporting to FreeDV Reporter.
class FreeDVReporterTask : public DVTask
{
public:
    FreeDVReporterTask();
    virtual ~FreeDVReporterTask();
        
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;
    
private:
    enum SocketIoRequestId 
    {
        WEBSOCKET_CONNECTED = 1,
        WEBSOCKET_DISCONNECTED = 2,
        WEBSOCKET_MESSAGE = 3,
    };
    
    template<uint32_t MSG_ID>
    class SocketIoMessageCommon : public DVTaskMessageBase<MSG_ID, SocketIoMessageCommon<MSG_ID>>
    {
    public:
        SocketIoMessageCommon(char* strProvided = nullptr, int lenProvided = 0)
            : DVTaskMessageBase<MSG_ID, SocketIoMessageCommon<MSG_ID>>(FREEDV_REPORTER_MESSAGE)
            , str(strProvided)
            , length(lenProvided)
            {}
        virtual ~SocketIoMessageCommon() = default;

        char *str;
        int length;
    };
    
    using WebsocketConnectedMessage = SocketIoMessageCommon<WEBSOCKET_CONNECTED>;
    using WebsocketDisconnectedMessage = SocketIoMessageCommon<WEBSOCKET_DISCONNECTED>;
    using WebsocketDataMessage = SocketIoMessageCommon<WEBSOCKET_MESSAGE>;

    esp_websocket_client_handle_t reportingClientHandle_;
    cJSON* jsonAuthObj_;
    bool reportingEnabled_;
    std::string callsign_;
    std::string gridSquare_;
    bool pttState_;
    audio::FreeDVMode freeDVMode_;
    uint64_t frequencyHz_;

    int pingIntervalMs_;
    int pingTimeoutMs_;

    void onReportingSettingsMessage_(DVTask* origin, storage::ReportingSettingsMessage* message);
    void onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message);
    void onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message);
    void onFreeDVCallsignReceivedMessage_(DVTask* origin, audio::FreeDVReceivedCallsignMessage* message);
    void onReportFrequencyChangeMessage_(DVTask* origin, ReportFrequencyChangeMessage* message);
    void onSetPTTState_(DVTask* origin, audio::FreeDVSetPTTStateMessage* message);
    void onSetFreeDVMode_(DVTask* origin, audio::SetFreeDVModeMessage* message);

    void onWebsocketConnectedMessage_(DVTask* origin, WebsocketConnectedMessage* message);
    void onWebsocketDataMessage_(DVTask* origin, WebsocketDataMessage* message);

    char* freeDVModeAsString_();

    void startSocketIoConnection_();
    void stopSocketIoConnection_();
    void handleEngineIoMessage_(char* ptr, int length);
    void handleSocketIoMessage_(char* ptr, int length);

    void sendFrequencyUpdate_();
    void sendTransmitStateUpdate_();

    static void WebsocketEventHandler_(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
};

}

}

#endif // FREEDV_REPORTER_TASK_H