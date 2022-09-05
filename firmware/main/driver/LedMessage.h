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

#ifndef LED_MESSAGE_H
#define LED_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(LED_MESSAGE);
}

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

enum LedMessageTypes
{
    SET_LED_STATE = 1,
};

class SetLedStateMessage : public DVTaskMessageBase<SET_LED_STATE, SetLedStateMessage>
{
public:
    enum LedLabel
    {
        NONE,
        SYNC,
        OVERLOAD,
        PTT,
        PTT_NPN,
        NETWORK
    };

    SetLedStateMessage(LedLabel ledProvided = NONE, bool ledStateProvided = false)
        : DVTaskMessageBase<SET_LED_STATE, SetLedStateMessage>(LED_MESSAGE)
        , led(ledProvided)
        , ledState(ledStateProvided)
        {}
    virtual ~SetLedStateMessage() = default;

    LedLabel led;
    bool ledState;
};

}

}

#endif // LED_MESSAGE_H