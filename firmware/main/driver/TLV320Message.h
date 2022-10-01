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

#ifndef TLV320_MESSAGE_H
#define TLV320_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(TLV320_MESSAGE);
}

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

enum TLV320MessageTypes
{
    OVERLOAD_STATE = 1,
};

class OverloadStateMessage : public DVTaskMessageBase<OVERLOAD_STATE, OverloadStateMessage>
{
public:
    OverloadStateMessage(bool leftChannelProvided = false, bool rightChannelProvided = false)
        : DVTaskMessageBase<OVERLOAD_STATE, OverloadStateMessage>(TLV320_MESSAGE)
        , leftChannel(leftChannelProvided)
        , rightChannel(rightChannelProvided)
        {}
    virtual ~OverloadStateMessage() = default;

    bool leftChannel;
    bool rightChannel;
};

}

}

#endif // TLV320_MESSAGE_H