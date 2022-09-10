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

#ifndef ICOM_PROTOCOL_STATE_H
#define ICOM_PROTOCOL_STATE_H

#include "StateMachineState.h"
#include "IcomPacket.h"

namespace ezdv
{

namespace network
{

namespace icom
{

class StateMachine;

class IcomProtocolState : public StateMachineState
{
public:
    IcomProtocolState(StateMachine* parent);
    virtual ~IcomProtocolState() = default;

    virtual std::string getName() = 0;

    virtual void onReceivePacket(IcomPacket& packet);

protected:
    StateMachine* parent_;
};

}

}

}

#endif // ICOM_PROTOCOL_STATE_H