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
    : ezdv::task::DVTask("FreeDVReporterTask", 1, 8192, tskNO_AFFINITY, 128, pdMS_TO_TICKS(1000))
    , reportingClientHandle_(nullptr)
    , jsonAuthObj_(nullptr)
    , reportingEnabled_(false)
    , callsign_("")
    , gridSquare_("UN00KN") // TBD: should come from flash
    , pttState_(false)
    , freeDVMode_(audio::ANALOG)
    , frequencyHz_(0)
    , pingIntervalMs_(0)
    , pingTimeoutMs_(0)
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

void FreeDVReporterTask::onTaskWake_()
{
    // Same as start.
    onTaskStart_();
}

void FreeDVReporterTask::onTaskSleep_()
{
    if (reportingEnabled_)
    {
        stopSocketIoConnection_();
    }
}

void FreeDVReporterTask::onReportingSettingsMessage_(DVTask* origin, storage::ReportingSettingsMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got reporting settings update");
    
    bool callsignChanged = callsign_ != message->callsign || gridSquare_ != message->gridSquare;
    callsign_ = message->callsign;
    gridSquare_ = message->gridSquare;

    // Disconnect and reconnect if there were any changes
    if (reportingEnabled_ && callsignChanged)
    {
        stopSocketIoConnection_();
        startSocketIoConnection_();
    }
}

void FreeDVReporterTask::onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message)
{
    if (callsign_ != "" && gridSquare_ != "" && freeDVMode_ != audio::FreeDVMode::ANALOG)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Reporting enabled by radio driver, begin connection");
        startSocketIoConnection_();
    }
}

void FreeDVReporterTask::onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message)
{
    if (reportingEnabled_)
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

        cJSON* snr = cJSON_CreateNumber(message->snr);
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
        cJSON_Delete(messagePayload);        
    }
}

void FreeDVReporterTask::onReportFrequencyChangeMessage_(DVTask* origin, ReportFrequencyChangeMessage* message)
{
    frequencyHz_ = message->frequencyHz;
    if (reportingEnabled_)
    {
        sendFrequencyUpdate_();
    }
}

void FreeDVReporterTask::onSetPTTState_(DVTask* origin, audio::FreeDVSetPTTStateMessage* message)
{
    pttState_ = message->pttState;
    if (reportingEnabled_)
    {
        sendTransmitStateUpdate_();
    }
}

void FreeDVReporterTask::onSetFreeDVMode_(DVTask* origin, audio::SetFreeDVModeMessage* message)
{
    freeDVMode_ = message->mode;

    if (reportingEnabled_)
    {
        if (freeDVMode_ == audio::FreeDVMode::ANALOG)
        {
            stopSocketIoConnection_();
        }
        else
        {
            sendTransmitStateUpdate_();
        }
    }
}

void FreeDVReporterTask::onWebsocketConnectedMessage_(DVTask* origin, WebsocketConnectedMessage* message)
{
    // Send namespace connection request with previously constructed auth data.
    std::string namespaceOpen = "40";
    auto tmp = cJSON_PrintUnformatted(jsonAuthObj_);
    namespaceOpen += tmp;
    cJSON_free(tmp);
    esp_websocket_client_send_text(reportingClientHandle_, namespaceOpen.c_str(), namespaceOpen.length(), portMAX_DELAY);
}

void FreeDVReporterTask::onWebsocketDataMessage_(DVTask* origin, WebsocketDataMessage* message)
{
    handleEngineIoMessage_(message->str, message->length);
    delete message->str;
}

char* FreeDVReporterTask::freeDVModeAsString_()
{
    switch (freeDVMode_)
    {
        case audio::FreeDVMode::ANALOG:
            return "";
        case audio::FreeDVMode::FREEDV_700D:
            return "700D";
        case audio::FreeDVMode::FREEDV_700E:
            return "700E";
        case audio::FreeDVMode::FREEDV_1600:
            return "1600";
        default:
            assert(0);
    }
}

void FreeDVReporterTask::startSocketIoConnection_()
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

    auto verObj = esp_app_get_description();
    std::string versionString = "ezDV ";
    versionString += verObj->version;
    cJSON_AddItemToObject(jsonAuthObj_, "version", cJSON_CreateString(versionString.c_str()));

    std::string uri = "ws://";
    uri += REPORTING_HOSTNAME;
    uri += "/socket.io/?EIO=4&transport=websocket";

    const esp_websocket_client_config_t ws_cfg = {
        .uri = uri.c_str(),
    };

    ESP_LOGI(CURRENT_LOG_TAG, "init client for %s", ws_cfg.uri);
    reportingClientHandle_ = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(reportingClientHandle_, WEBSOCKET_EVENT_ANY, WebsocketEventHandler_, (void *)this);
    ESP_LOGI(CURRENT_LOG_TAG, "start client");
    esp_websocket_client_start(reportingClientHandle_);
}

void FreeDVReporterTask::handleEngineIoMessage_(char* ptr, int length)
{
    ESP_LOGI(CURRENT_LOG_TAG, "got engine.io message %s of length %d", ptr, length);

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

            startSocketIoConnection_();
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
            // ignore all others as they're related to transport upgrades
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
            startSocketIoConnection_();
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
    char* engineIoDisconnectMessage = "1";
    esp_websocket_client_send_text(reportingClientHandle_, engineIoDisconnectMessage, strlen(engineIoDisconnectMessage), portMAX_DELAY);
    esp_websocket_client_stop(reportingClientHandle_);
    esp_websocket_client_destroy(reportingClientHandle_);
    reportingClientHandle_ = nullptr;
    reportingEnabled_ = false;
}

void FreeDVReporterTask::sendFrequencyUpdate_()
{
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
    cJSON_Delete(messagePayload);
}

void FreeDVReporterTask::sendTransmitStateUpdate_()
{
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
    cJSON_Delete(messagePayload);
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
            WebsocketConnectedMessage connMessage;
            thisObj->post(&connMessage);
            break;
        }
        case WEBSOCKET_EVENT_DISCONNECTED:
        {
            ESP_LOGI(CURRENT_LOG_TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            
            // Note: esp-websocket-client has auto-reconnect logic, so we don't
            // strictly need to do anything here.
            WebsocketDisconnectedMessage disconnMessage;
            thisObj->post(&disconnMessage);
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