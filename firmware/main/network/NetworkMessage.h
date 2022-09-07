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

#ifndef NETWORK_MESSAGE_H
#define NETWORK_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(NETWORK_MESSAGE);
}

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

enum NetworkMessageTypes
{
    WIRELESS_NETWORK_STATUS = 1,
    RADIO_CONNECTION_STATUS = 2,
};

template<uint32_t MSG_ID>
class NetworkMessageCommon : public DVTaskMessageBase<MSG_ID, NetworkMessageCommon<MSG_ID>>
{
public:
    NetworkMessageCommon(bool stateProvided = false)
        : DVTaskMessageBase<MSG_ID, NetworkMessageCommon<MSG_ID>>(NETWORK_MESSAGE)
        , state(stateProvided)
        {}
    virtual ~NetworkMessageCommon() = default;

    bool state;
};

using WirelessNetworkStatusMessage = NetworkMessageCommon<WIRELESS_NETWORK_STATUS>;
using RadioConnectionStatusMessage = NetworkMessageCommon<RADIO_CONNECTION_STATUS>;

}

}

#endif // LED_MESSAGE_H