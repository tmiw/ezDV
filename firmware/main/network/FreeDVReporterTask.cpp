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

#include <string>

#include "FreeDVReporterTask.h"
#include "cJSON.h"
#include "esp_app_desc.h"

#define REPORTING_HOSTNAME "qso.freedv.org"
#define CURRENT_LOG_TAG "FreeDVReporter"

#define SOCKET_IO_TX_PREFIX "42"

extern "C"
{
    DV_EVENT_DEFINE_BASE(FREEDV_REPORTER_MESSAGE);
}

namespace ezdv
{

namespace network
{

FreeDVReporterTask::FreeDVReporterTask()
    : ezdv::task::DVTask("FreeDVReporterTask", 1, 4096, tskNO_AFFINITY, 128)
    , reconnectTimer_(this, this, &FreeDVReporterTask::startSocketIoConnection_, MS_TO_US(10000), "FDVReporterReconn")
    , reportingClientHandle_(nullptr)
    , jsonAuthObj_(nullptr)
    , reportingEnabled_(false)
    , callsign_("")
    , gridSquare_("UN00KN") // TBD: should come from flash
    , pttState_(false)
    , freeDVMode_(audio::ANALOG)
    , frequencyHz_(0)
    , forceReporting_(false)
    , reportingRefCount_(0)
    , pingIntervalMs_(0)
    , pingTimeoutMs_(0)
    , isConnecting_(false)
{
    registerMessageHandler(this, &FreeDVReporterTask::onReportingSettingsMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onEnableReportingMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onDisableReportingMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onFreeDVCallsignReceivedMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onReportFrequencyChangeMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onSetPTTState_);
    registerMessageHandler(this, &FreeDVReporterTask::onSetFreeDVMode_);

    registerMessageHandler(this, &FreeDVReporterTask::onWebsocketDataMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onWebsocketConnectedMessage_);
    registerMessageHandler(this, &FreeDVReporterTask::onWebsocketDisconnectedMessage_);
}

FreeDVReporterTask::~FreeDVReporterTask()
{
    if (reportingEnabled_)
    {
        stopSocketIoConnection_();
    }
}

void FreeDVReporterTask::onTaskStart_()
{
    // Request current reporting settings
    storage::RequestReportingSettingsMessage reportingRequest;
    publish(&reportingRequest);

    // Request current FreeDV mode
    audio::RequestGetFreeDVModeMessage modeRequest;
    publish(&modeRequest);
}

void FreeDVReporterTask::onTaskSleep_()
{
    if (reportingEnabled_)
    {
        stopSocketIoConnection_();
    }

    reconnectTimer_.stop();
}

void FreeDVReporterTask::onReportingSettingsMessage_(DVTask* origin, storage::ReportingSettingsMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got reporting settings update");
    
    bool callsignChanged = callsign_ != message->callsign || gridSquare_ != message->gridSquare;
    bool messageChanged = message_ != message->message;
    bool forcedReportingChanged = forceReporting_ != message->forceReporting;
    
    callsign_ = message->callsign;
    gridSquare_ = message->gridSquare;
    forceReporting_ = message->forceReporting;
    message_ = message->message;
    
    // Disconnect and reconnect if there were any changes
    if (callsignChanged)
    {
        if (reportingEnabled_)
        {
            stopSocketIoConnection_();
        }
        
        if (reportingRefCount_ > 0 && callsign_.length() > 0 && gridSquare_.length() > 0)
        {
            startSocketIoConnection_(nullptr);
        }
    }
    
    // If forced reporting has changed, trigger disconnect or connect
    // as appropriate.
    if (forcedReportingChanged)
    {
        if (forceReporting_)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Forcing reporting to go live");
            EnableReportingMessage request;
            post(&request);
        }
        else
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Undoing forced reporting");
            DisableReportingMessage request;
            post(&request);
        }
    }
    
    // If reporting is forced, unconditionally send frequency update.
    if (forceReporting_)
    {
        ReportFrequencyChangeMessage request(message->freqHz);
        post(&request);
    }

    // If connected and the reporting message has changed, send that.
    if (reportingEnabled_ && messageChanged)
    {
        sendReportingMessageUpdate_();
    }
}

void FreeDVReporterTask::onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message)
{
    reportingRefCount_++;
    if (callsign_ != "" && gridSquare_ != "")
    {        
        ESP_LOGI(CURRENT_LOG_TAG, "Reporting enabled by radio driver, begin connection");
        if (reportingEnabled_)
        {
            stopSocketIoConnection_();
        }
        startSocketIoConnection_(nullptr);
    }
}

void FreeDVReporterTask::onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message)
{
    reportingRefCount_--;
    if (reportingEnabled_ && reportingRefCount_ == 0)
    {
        stopSocketIoConnection_();
    }
}

void FreeDVReporterTask::onFreeDVCallsignReceivedMessage_(DVTask* origin, audio::FreeDVReceivedCallsignMessage* message)
{
    if (reportingEnabled_)
    {
        cJSON* outMessage = cJSON_CreateArray();
        assert(outMessage != nullptr);

        cJSON* messageName = cJSON_CreateString("rx_report");
        assert(messageName != nullptr);
        cJSON_AddItemToArray(outMessage, messageName);

        cJSON* messagePayload = cJSON_CreateObject();
        assert(messagePayload != nullptr);

        cJSON* callsign = cJSON_CreateString(message->callsign);
        assert(callsign != nullptr);
        cJSON_AddItemToObject(messagePayload, "callsign", callsign);

        cJSON* snr = cJSON_CreateNumber((int)message->snr);
        assert(snr != nullptr);
        cJSON_AddItemToObject(messagePayload, "snr", snr);

        cJSON* mode = cJSON_CreateString(freeDVModeAsString_());
        assert(mode != nullptr);
        cJSON_AddItemToObject(messagePayload, "mode", mode);

        cJSON_AddItemToArray(outMessage, messagePayload);

        auto tmp = cJSON_PrintUnformatted(outMessage);

        std::string messageToSend = SOCKET_IO_TX_PREFIX;
        messageToSend += tmp;
        esp_websocket_client_send_text(reportingClientHandle_, messageToSend.c_str(), messageToSend.length(), portMAX_DELAY);

        cJSON_free(tmp);
        cJSON_Delete(outMessage);        
    }
}

void FreeDVReporterTask::onReportFrequencyChangeMessage_(DVTask* origin, ReportFrequencyChangeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got frequency update: %" PRIu64, message->frequencyHz);
    frequencyHz_ = message->frequencyHz;
    if (reportingEnabled_)
    {
        sendFrequencyUpdate_();
    }
}

void FreeDVReporterTask::onSetPTTState_(DVTask* origin, audio::FreeDVSetPTTStateMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got PTT update: %d", message->pttState);
    pttState_ = message->pttState;
    if (reportingEnabled_)
    {
        sendTransmitStateUpdate_();
    }
}

void FreeDVReporterTask::onSetFreeDVMode_(DVTask* origin, audio::SetFreeDVModeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got mode update: %d", message->mode);
    freeDVMode_ = message->mode;
    if (reportingEnabled_)
    {
        sendTransmitStateUpdate_();
    }
}

void FreeDVReporterTask::onWebsocketConnectedMessage_(DVTask* origin, WebsocketConnectedMessage* message)
{
    isConnecting_ = false;
    
    // Send namespace connection request with previously constructed auth data.
    std::string namespaceOpen = "40";
    auto tmp = cJSON_PrintUnformatted(jsonAuthObj_);
    namespaceOpen += tmp;
    cJSON_free(tmp);
    esp_websocket_client_send_text(reportingClientHandle_, namespaceOpen.c_str(), namespaceOpen.length(), portMAX_DELAY);
}

void FreeDVReporterTask::onWebsocketDisconnectedMessage_(DVTask* origin, WebsocketDisconnectedMessage* message)
{
    if (reportingEnabled_ || isConnecting_)
    {
        // Retry connection if reporting is enabled (i.e. we lost connection).
        stopSocketIoConnection_();
        reconnectTimer_.start(true);
    }
}

void FreeDVReporterTask::onWebsocketDataMessage_(DVTask* origin, WebsocketDataMessage* message)
{
    handleEngineIoMessage_(message->str, message->length);
    delete message->str;
}

static const char* FreeDVModeString_[] = {
    "ANALOG",
    "700D",
    "700E",
    "1600"
};

const char* FreeDVReporterTask::freeDVModeAsString_()
{
    return FreeDVModeString_[freeDVMode_];

}

void FreeDVReporterTask::startSocketIoConnection_(DVTimer*)
{
    if (jsonAuthObj_ != nullptr)
    {
        cJSON_Delete(jsonAuthObj_);
    }
    jsonAuthObj_ = cJSON_CreateObject();
    assert(jsonAuthObj_ != nullptr);

    cJSON_AddItemToObject(jsonAuthObj_, "callsign", cJSON_CreateString(callsign_.c_str()));
    cJSON_AddItemToObject(jsonAuthObj_, "grid_square", cJSON_CreateString(gridSquare_.c_str()));
    cJSON_AddItemToObject(jsonAuthObj_, "role", cJSON_CreateString("report_wo"));
    cJSON_AddItemToObject(jsonAuthObj_, "os", cJSON_CreateString("other"));

    auto verObj = esp_app_get_description();
    std::string versionString = "ezDV ";
    versionString += verObj->version;
    cJSON_AddItemToObject(jsonAuthObj_, "version", cJSON_CreateString(versionString.c_str()));

    std::string uri = "ws://";
    uri += REPORTING_HOSTNAME;
    uri += "/socket.io/?EIO=4&transport=websocket";

    esp_websocket_client_config_t ws_cfg;
    memset(&ws_cfg, 0, sizeof(ws_cfg));

    ws_cfg.uri = uri.c_str();
    ws_cfg.disable_auto_reconnect = true; // we're handling auto-reconnect
    ws_cfg.task_prio = 1; // report to the server when able, don't interfere with others

    isConnecting_ = true;

    ESP_LOGI(CURRENT_LOG_TAG, "init client for %s", ws_cfg.uri);
    reportingClientHandle_ = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(reportingClientHandle_, WEBSOCKET_EVENT_ANY, WebsocketEventHandler_, (void *)this);
    ESP_LOGI(CURRENT_LOG_TAG, "start client");
    esp_websocket_client_start(reportingClientHandle_);
}

void FreeDVReporterTask::handleEngineIoMessage_(char* ptr, int length)
{
    ESP_LOGI(CURRENT_LOG_TAG, "got engine.io message %c of length %d", ptr[0], length);

    switch(ptr[0])
    {
        case '0':
        {
            ESP_LOGI(CURRENT_LOG_TAG, "engine.io open");

            // "open" -- ready to receive socket.io messages

            break;
        }
        case '1':
        {
            ESP_LOGI(CURRENT_LOG_TAG, "engine.io close");

            // "close" -- we're being closed
            esp_websocket_client_stop(reportingClientHandle_);
            esp_websocket_client_destroy(reportingClientHandle_);
            reportingClientHandle_ = nullptr;
            reportingEnabled_ = false;

            startSocketIoConnection_(nullptr);
            break;
        }
        case '2':
        {
            ESP_LOGI(CURRENT_LOG_TAG, "engine.io ping");

            // "ping" -- send pong
            esp_websocket_client_send_text(reportingClientHandle_, "3", 1, portMAX_DELAY);
            break;
        }
        case '4':
        {
            // "message" -- process socket.io message
            handleSocketIoMessage_(ptr + 1, length - 1);
            break;
        }
        default:
            // ignore all others as they're related to transport upgrades, but if we got
            // something invalid, we should treat it as a disconnection.
            if (!isdigit(ptr[0]))
            {
                ESP_LOGI(CURRENT_LOG_TAG, "invalid data received from engine.io -- reconnecting");

                // "close" -- we're being closed
                esp_websocket_client_stop(reportingClientHandle_);
                esp_websocket_client_destroy(reportingClientHandle_);
                reportingClientHandle_ = nullptr;
                reportingEnabled_ = false;

                startSocketIoConnection_(nullptr);
            }
            break;
    }
}

void FreeDVReporterTask::handleSocketIoMessage_(char* ptr, int length)
{
    switch(ptr[0])
    {
        case '0':
        {
            ESP_LOGI(CURRENT_LOG_TAG, "socket.io connect");

            // connection successful
            reportingEnabled_ = true;
            sendFrequencyUpdate_();
            sendTransmitStateUpdate_();
            sendReportingMessageUpdate_();
            break;
        }
        case '2':
        {
            // event received from server
            // NOTE: with the 'report_wo' role, we're not expecting anything,
            // so we can ignore.
            break;
        }
        case '4':
        {
            // error connecting to namespace, close connection and retry
            stopSocketIoConnection_();
            reconnectTimer_.start(true);
            break;
        }
        default:
        {
            // current server doesn't use anything else, so ignore.
            break;
        }
    }
}

void FreeDVReporterTask::stopSocketIoConnection_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "stopping socket.io connection");
    if (reportingClientHandle_ != nullptr)
    {
        if (esp_websocket_client_is_connected(reportingClientHandle_))
        {
            const char* engineIoDisconnectMessage = "1";
            esp_websocket_client_send_text(reportingClientHandle_, engineIoDisconnectMessage, strlen(engineIoDisconnectMessage), portMAX_DELAY);
            esp_websocket_client_stop(reportingClientHandle_);
        }

        // XXX - there's a bug in esp_websocket_client that causes it to take a while to
        // destroy its task after stopping the client. While not ideal, the below code
        // to check for this process and close it at least prevents it from accessing
        // invalid memory if it were to stay running after destroying the handle.
        //
        // Note: as of esp_websocket_client v1.2.3 this might have been fixed, but this
        // shouldn't hurt to keep in here for a bit. Its timeout is apparently 1000ms, so
        // if it's still not dead a bit longer after that, we're probably still good to kill
        // the task.
        auto websocketHandle = xTaskGetHandle("websocket_task");
        if (websocketHandle != nullptr)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "websocket_task is still running despite esp_websocket_client_stop()!");
        }
        auto timeBegin = esp_timer_get_time();
        bool forceKillTask = false;
        while (websocketHandle != nullptr)
        {
            auto currentState = eTaskGetState(websocketHandle);
            auto timeElapsed = esp_timer_get_time() - timeBegin;
            if (currentState == eDeleted || currentState == eInvalid ||
                timeElapsed >= MS_TO_US(1250))
            {
                forceKillTask = timeElapsed >= MS_TO_US(1250);
                break;
            }

            // Make sure other stuff can run.
            taskYIELD();
        }

        if (forceKillTask && websocketHandle != nullptr)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "More than 1000ms has elapsed waiting for websocket_task, force killing it now.");
            vTaskDelete(websocketHandle);
        }
        
        esp_websocket_client_destroy(reportingClientHandle_);
    }
    reportingClientHandle_ = nullptr;
    reportingEnabled_ = false;
    ESP_LOGI(CURRENT_LOG_TAG, "socket.io connection stopped");
}

void FreeDVReporterTask::sendReportingMessageUpdate_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sending reporting message update");

    cJSON* message = cJSON_CreateArray();
    assert(message != nullptr);

    cJSON* messageName = cJSON_CreateString("message_update");
    assert(messageName != nullptr);
    cJSON_AddItemToArray(message, messageName);

    cJSON* messagePayload = cJSON_CreateObject();
    assert(messagePayload != nullptr);

    cJSON* reportingMessage = cJSON_CreateString(message_.c_str());
    assert(reportingMessage != nullptr);
    cJSON_AddItemToObject(messagePayload, "message", reportingMessage);

    cJSON_AddItemToArray(message, messagePayload);

    auto tmp = cJSON_PrintUnformatted(message);

    std::string messageToSend = SOCKET_IO_TX_PREFIX;
    messageToSend += tmp;
    esp_websocket_client_send_text(reportingClientHandle_, messageToSend.c_str(), messageToSend.length(), portMAX_DELAY);

    cJSON_free(tmp);
    cJSON_Delete(message);
}

void FreeDVReporterTask::sendFrequencyUpdate_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sending frequency update");

    cJSON* message = cJSON_CreateArray();
    assert(message != nullptr);

    cJSON* messageName = cJSON_CreateString("freq_change");
    assert(messageName != nullptr);
    cJSON_AddItemToArray(message, messageName);

    cJSON* messagePayload = cJSON_CreateObject();
    assert(messagePayload != nullptr);

    cJSON* frequency = cJSON_CreateNumber(frequencyHz_);
    assert(frequency != nullptr);
    cJSON_AddItemToObject(messagePayload, "freq", frequency);

    cJSON_AddItemToArray(message, messagePayload);

    auto tmp = cJSON_PrintUnformatted(message);

    std::string messageToSend = SOCKET_IO_TX_PREFIX;
    messageToSend += tmp;
    esp_websocket_client_send_text(reportingClientHandle_, messageToSend.c_str(), messageToSend.length(), portMAX_DELAY);

    cJSON_free(tmp);
    cJSON_Delete(message);
}

void FreeDVReporterTask::sendTransmitStateUpdate_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sending PTT update");

    cJSON* message = cJSON_CreateArray();
    assert(message != nullptr);

    cJSON* messageName = cJSON_CreateString("tx_report");
    assert(messageName != nullptr);
    cJSON_AddItemToArray(message, messageName);

    cJSON* messagePayload = cJSON_CreateObject();
    assert(messagePayload != nullptr);

    cJSON* mode = cJSON_CreateString(freeDVModeAsString_());
    assert(mode != nullptr);
    cJSON_AddItemToObject(messagePayload, "mode", mode);

    cJSON* transmitting = cJSON_CreateBool(pttState_);
    assert(transmitting != nullptr);
    cJSON_AddItemToObject(messagePayload, "transmitting", transmitting);

    cJSON_AddItemToArray(message, messagePayload);

    auto tmp = cJSON_PrintUnformatted(message);

    std::string messageToSend = SOCKET_IO_TX_PREFIX;
    messageToSend += tmp;
    esp_websocket_client_send_text(reportingClientHandle_, messageToSend.c_str(), messageToSend.length(), portMAX_DELAY);

    cJSON_free(tmp);
    cJSON_Delete(message);
}

void FreeDVReporterTask::WebsocketEventHandler_(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    FreeDVReporterTask* thisObj = (FreeDVReporterTask*)handler_args;

    switch (event_id) 
    {
        case WEBSOCKET_EVENT_CONNECTED:
        {
            ESP_LOGI(CURRENT_LOG_TAG, "WEBSOCKET_EVENT_CONNECTED");
            if (thisObj->isAwake())
            {
                WebsocketConnectedMessage connMessage;
                thisObj->post(&connMessage);
            }
            break;
        }
        case WEBSOCKET_EVENT_DISCONNECTED:
        {
            ESP_LOGI(CURRENT_LOG_TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            if (thisObj->isAwake())
            {
                WebsocketDisconnectedMessage disconnMessage;
                thisObj->post(&disconnMessage);
            }
            break;
        }
        case WEBSOCKET_EVENT_DATA:
        {
            if (data->data_len > 0)
            {
                char* dataCopy = (char*)malloc(data->data_len + 1);
                assert(dataCopy != nullptr);

                memcpy(dataCopy, (char*)data->data_ptr, data->data_len);
                dataCopy[data->data_len] = 0;

                WebsocketDataMessage dataMessage(dataCopy, data->data_len);
                thisObj->post(&dataMessage);
            }
            break;
        }
        case WEBSOCKET_EVENT_ERROR:
        {
            ESP_LOGI(CURRENT_LOG_TAG, "WEBSOCKET_EVENT_ERROR");
            break;
        }
    }
}

}

}
