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

#ifndef SETTINGS_MESSAGE_H
#define SETTINGS_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(SETTINGS_MESSAGE);
}

namespace ezdv
{

namespace storage
{

using namespace ezdv::task;

enum SettingsMessageTypes
{
    LEFT_CHANNEL_VOLUME = 1,
    RIGHT_CHANNEL_VOLUME = 2,
    SET_LEFT_CHANNEL_VOLUME = 3,
    SET_RIGHT_CHANNEL_VOLUME = 4
};

template<uint32_t TYPE_ID>
class VolumeMessageCommon : public DVTaskMessageBase<TYPE_ID, VolumeMessageCommon<TYPE_ID>>
{
public:
    VolumeMessageCommon()
        : DVTaskMessageBase<TYPE_ID, VolumeMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) { }
    virtual ~VolumeMessageCommon() = default;

    int8_t volume;
};

using LeftChannelVolumeMessage = VolumeMessageCommon<LEFT_CHANNEL_VOLUME>;
using RightChannelVolumeMessage = VolumeMessageCommon<RIGHT_CHANNEL_VOLUME>;
using SetLeftChannelVolumeMessage = VolumeMessageCommon<SET_LEFT_CHANNEL_VOLUME>;
using SetRightChannelVolumeMessage = VolumeMessageCommon<SET_RIGHT_CHANNEL_VOLUME>;

}

}

#endif // SETTINGS_MESSAGE_H