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

#ifndef VOICE_KEYER_MESSAGE_H
#define VOICE_KEYER_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(VOICE_KEYER_MESSAGE);
}

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

enum VoiceKeyerMessageTypes
{
    START_KEYER = 1,
    STOP_KEYER = 2,

    // Sent by voice keyer to trigger TX and RX
    REQUEST_TX = 3,
    REQUEST_RX = 4,

    // Sent to indicate upload finished
    FILE_UPLOAD_COMPLETE = 5,

    // Sent to indicate keyer has iterated the configured number of times
    VOICE_KEYER_COMPLETE = 6,

    // VK request messages to maintain sync between web interface and physical
    // buttons
    REQUEST_START_STOP_KEYER = 7,
    GET_KEYER_STATE = 8,
};

template<uint32_t MSG_ID>
class StartStopCommon : public DVTaskMessageBase<MSG_ID, StartStopCommon<MSG_ID>>
{
public:
    StartStopCommon()
        : DVTaskMessageBase<MSG_ID, StartStopCommon<MSG_ID>>(VOICE_KEYER_MESSAGE)
        {}
    virtual ~StartStopCommon() = default;
};

class FileUploadCompleteMessage : public DVTaskMessageBase<FILE_UPLOAD_COMPLETE, FileUploadCompleteMessage>
{
public:
    enum ErrorType 
    {
        NONE,
        SYSTEM_ERROR,
        INCORRECT_SAMPLE_RATE,
        INCORRECT_NUM_CHANNELS,
        MISSING_FIELDS,
        UNABLE_SAVE_SETTINGS,
    };

    FileUploadCompleteMessage(bool successProvided = true, ErrorType errorTypeProvided = NONE, int errnoProvided = 0)
        : DVTaskMessageBase<FILE_UPLOAD_COMPLETE, FileUploadCompleteMessage>(VOICE_KEYER_MESSAGE)
        , success(successProvided)
        , errorType(errorTypeProvided)
        , errorNumber(errnoProvided)
        {}
    virtual ~FileUploadCompleteMessage() = default;

    bool success;
    ErrorType errorType;
    int errorNumber;
};

using StartVoiceKeyerMessage = StartStopCommon<START_KEYER>;
using StopVoiceKeyerMessage = StartStopCommon<STOP_KEYER>;
using RequestTxMessage = StartStopCommon<REQUEST_TX>;
using RequestRxMessage = StartStopCommon<REQUEST_RX>;

using VoiceKeyerCompleteMessage = StartStopCommon<VOICE_KEYER_COMPLETE>;

class RequestStartStopKeyerMessage : public DVTaskMessageBase<REQUEST_START_STOP_KEYER, RequestStartStopKeyerMessage>
{
public:
    RequestStartStopKeyerMessage(bool reqProvided = false)
        : DVTaskMessageBase<REQUEST_START_STOP_KEYER, RequestStartStopKeyerMessage>(VOICE_KEYER_MESSAGE)
        , request(reqProvided)
        {}
    virtual ~RequestStartStopKeyerMessage() = default;

    bool request;
};

using GetKeyerStateMessage = StartStopCommon<GET_KEYER_STATE>;

}

}
#endif // VOICE_KEYER_MESSAGE_H