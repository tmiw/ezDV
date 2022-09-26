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

using StartVoiceKeyerMessage = StartStopCommon<START_KEYER>;
using StopVoiceKeyerMessage = StartStopCommon<STOP_KEYER>;
using RequestTxMessage = StartStopCommon<REQUEST_TX>;
using RequestRxMessage = StartStopCommon<REQUEST_RX>;

}

}
#endif // VOICE_KEYER_MESSAGE_H