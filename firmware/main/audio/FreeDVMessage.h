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

class SetFreeDVModeMessage : public DVTaskMessageBase<SET_FREEDV_MODE, SetFreeDVModeMessage>
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

    SetFreeDVModeMessage(FreeDVMode modeProvided = ANALOG)
        : DVTaskMessageBase<SET_FREEDV_MODE, SetFreeDVModeMessage>(FREEDV_MESSAGE)
        , mode(modeProvided)
        {}
    virtual ~SetFreeDVModeMessage() = default;

    FreeDVMode mode;
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

}

}
#endif // FREEDV_MESSAGE_H