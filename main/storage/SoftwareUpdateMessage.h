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

#ifndef SOFTWARE_UPDATE_MESSAGE_H
#define SOFTWARE_UPDATE_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(SOFTWARE_UPDATE_MESSAGE);
}

namespace ezdv
{

namespace storage
{

using namespace ezdv::task;

enum VoiceKeyerMessageTypes
{
    FIRMWARE_UPDATE_COMPLETE = 1,
};

class FirmwareUpdateCompleteMessage : public DVTaskMessageBase<FIRMWARE_UPDATE_COMPLETE, FirmwareUpdateCompleteMessage>
{
public:
    FirmwareUpdateCompleteMessage(bool successProvided = false)
        : DVTaskMessageBase<FIRMWARE_UPDATE_COMPLETE, FirmwareUpdateCompleteMessage>(SOFTWARE_UPDATE_MESSAGE)
        , success(successProvided)
        {}
    virtual ~FirmwareUpdateCompleteMessage() = default;
    
    bool success;
};

}

}
#endif // SOFTWARE_UPDATE_MESSAGE_H