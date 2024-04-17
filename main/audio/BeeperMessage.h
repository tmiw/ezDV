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

#ifndef BEEPER_MESSAGE_H
#define BEEPER_MESSAGE_H

#include <cstring>

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(BEEPER_MESSAGE);
}

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

enum BeeperMessageTypes
{
    SET_TEXT = 1,
    CLEAR_TEXT = 2,
};

class SetBeeperTextMessage : public DVTaskMessageBase<SET_TEXT, SetBeeperTextMessage>
{
public:
    SetBeeperTextMessage(const char* textProvided = nullptr)
        : DVTaskMessageBase<SET_TEXT, SetBeeperTextMessage>(BEEPER_MESSAGE)
        {
            memset(text, 0, sizeof(text));
            if (textProvided != nullptr)
            {
                int amountToCopy = 
                    strlen(textProvided) < (sizeof(text) - 1) ?
                    strlen(textProvided) :
                    sizeof(text) - 1;
                memcpy(text, textProvided, amountToCopy);
            }
        }
    virtual ~SetBeeperTextMessage() = default;

    // Shouldn't need 32 characters but just in case.
    char text[32];
};

class ClearBeeperTextMessage : public DVTaskMessageBase<CLEAR_TEXT, ClearBeeperTextMessage>
{
public:
    ClearBeeperTextMessage()
        : DVTaskMessageBase<CLEAR_TEXT, ClearBeeperTextMessage>(BEEPER_MESSAGE)
        {}
    virtual ~ClearBeeperTextMessage() = default;
};

}

}
#endif // FREEDV_MESSAGE_H