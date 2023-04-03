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

#ifndef FREEDV_MESSAGE_H
#define FREEDV_MESSAGE_H

#include <cstring>
#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(FREEDV_MESSAGE);
}

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

enum FreeDVMessageTypes
{
    SYNC_STATE = 1,
    SET_FREEDV_MODE = 2,
    SET_PTT_STATE = 3,
    REQUEST_SET_FREEDV_MODE = 4,
    REQUEST_GET_FREEDV_MODE = 5,
    FREEDV_RX_CALLSIGN = 6,
};

class FreeDVSyncStateMessage : public DVTaskMessageBase<SYNC_STATE, FreeDVSyncStateMessage>
{
public:
    FreeDVSyncStateMessage(bool syncStateProvided = false)
        : DVTaskMessageBase<SYNC_STATE, FreeDVSyncStateMessage>(FREEDV_MESSAGE)
        , syncState(syncStateProvided)
        {}
    virtual ~FreeDVSyncStateMessage() = default;

    bool syncState;
};

template<uint32_t MSG_ID>
class FreeDVModeMessageCommon : public DVTaskMessageBase<MSG_ID, FreeDVModeMessageCommon<MSG_ID> >
{
public:
    enum FreeDVMode
    {
        ANALOG,
        FREEDV_700D,
        FREEDV_700E,
        FREEDV_1600,

        MAX_FREEDV_MODES
    };

    FreeDVModeMessageCommon(FreeDVMode modeProvided = ANALOG)
        : DVTaskMessageBase<MSG_ID, FreeDVModeMessageCommon<MSG_ID> >(FREEDV_MESSAGE)
        , mode(modeProvided)
        {}
    virtual ~FreeDVModeMessageCommon() = default;

    FreeDVMode mode;
};

using SetFreeDVModeMessage = FreeDVModeMessageCommon<SET_FREEDV_MODE>;
using RequestSetFreeDVModeMessage = FreeDVModeMessageCommon<REQUEST_SET_FREEDV_MODE>;

class RequestGetFreeDVModeMessage : public DVTaskMessageBase<REQUEST_GET_FREEDV_MODE, RequestGetFreeDVModeMessage>
{
public:
    RequestGetFreeDVModeMessage()
        : DVTaskMessageBase<REQUEST_GET_FREEDV_MODE, RequestGetFreeDVModeMessage>(FREEDV_MESSAGE)
        {}
    virtual ~RequestGetFreeDVModeMessage() = default;
};

class FreeDVSetPTTStateMessage : public DVTaskMessageBase<SET_PTT_STATE, FreeDVSetPTTStateMessage>
{
public:
    FreeDVSetPTTStateMessage(bool pttStateProvided = false)
        : DVTaskMessageBase<SET_PTT_STATE, FreeDVSetPTTStateMessage>(FREEDV_MESSAGE)
        , pttState(pttStateProvided)
        {}
    virtual ~FreeDVSetPTTStateMessage() = default;

    bool pttState;
};

class FreeDVReceivedCallsignMessage : public DVTaskMessageBase<FREEDV_RX_CALLSIGN, FreeDVReceivedCallsignMessage>
{
public:
    enum { MAX_STR_SIZE = 16 };

    FreeDVReceivedCallsignMessage(char* callsignProvided = "")
        : DVTaskMessageBase<FREEDV_RX_CALLSIGN, FreeDVReceivedCallsignMessage>(FREEDV_MESSAGE) 
    { 
        memset(callsign, 0, sizeof(callsign));
        strncpy(callsign, callsignProvided, sizeof(callsign) - 1);
    }
    
    virtual ~FreeDVReceivedCallsignMessage() = default;

    char callsign[MAX_STR_SIZE];
};

}

}
#endif // FREEDV_MESSAGE_H