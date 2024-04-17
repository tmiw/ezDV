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

#ifndef ARE_YOU_READY_CIV_STATE_H
#define ARE_YOU_READY_CIV_STATE_H

#include "AreYouReadyState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

class AreYouReadyCIVState : public AreYouReadyState
{
public:
    AreYouReadyCIVState(IcomStateMachine* parent);
    virtual ~AreYouReadyCIVState() = default;

protected:
    virtual void onReceivePacketImpl_(IcomPacket& packet) override;
};

}

}

}

#endif // ARE_YOU_READY_CONTROL_STATE_H