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

#ifndef BUTTON_MESSAGE_H
#define BUTTON_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(BUTTON_MESSAGE);
}

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

enum ButtonMessageTypes
{
    BUTTON_SHORT_PRESSED = 1,
    BUTTON_LONG_PRESSED = 2,
    BUTTON_RELEASED = 3,
};

enum ButtonLabel
{
    NONE,
    PTT,
    MODE,
    VOL_UP,
    VOL_DOWN
};

template<uint32_t MSG_TYPE>
class ButtonMessageCommon : public DVTaskMessageBase<MSG_TYPE, ButtonMessageCommon<MSG_TYPE>>
{
public:
    ButtonMessageCommon(ButtonLabel buttonProvided = NONE)
        : DVTaskMessageBase<MSG_TYPE, ButtonMessageCommon<MSG_TYPE>>(BUTTON_MESSAGE)
        , button(buttonProvided)
        {}
    virtual ~ButtonMessageCommon() = default;

    ButtonLabel button;
};

using ButtonShortPressedMessage = ButtonMessageCommon<BUTTON_SHORT_PRESSED>;
using ButtonLongPressedMessage = ButtonMessageCommon<BUTTON_LONG_PRESSED>;
using ButtonReleasedMessage = ButtonMessageCommon<BUTTON_RELEASED>;

}

}

#endif // LED_MESSAGE_H