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

#include "AreYouReadyCIVState.h"
#include "IcomStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

AreYouReadyCIVState::AreYouReadyCIVState(IcomStateMachine* parent)
    : AreYouReadyState(parent)
{
    // empty
}

void AreYouReadyCIVState::onReceivePacketImpl_(IcomPacket& packet)
{
    // Weirdly, we need to get the remote ID again here for CIV.
    auto typedPacket = packet.getTypedPacket<control_packet>();
    parent_->setTheirIdentifier(typedPacket->sentid);

    parent_->transitionState(IcomProtocolState::CIV);
}

}

}

}