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

#include "CIVState.h"
#include "IcomStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

CIVState::CIVState(IcomStateMachine* parent)
    : TrackedPacketState(parent)
    , civSequenceNumber_(0)
    , civId_(0)
{
    parent_->getTask()->registerMessageHandler(this, &CIVState::onFreeDVSetPTTStateMessage_);
}

void CIVState::onEnterState()
{
    TrackedPacketState::onEnterState();
    
    // Initialize state
    civSequenceNumber_ = 0;
    civId_ = 0;
    
    sendCIVOpenPacket_();
        
    // Send request to get the radio ID on the other side.
    uint8_t civPacket[] = {
        0xFE,
        0xFE,
        0x00,
        0xE0,
        0x19, // Request radio ID command/subcommand
        0x00,
        0xFD
    };

    sendCIVPacket_(civPacket, sizeof(civPacket));
}

void CIVState::onExitState()
{
    TrackedPacketState::onExitState();
}

std::string CIVState::getName()
{
    return "CIV";
}

void CIVState::onReceivePacket(IcomPacket& packet)
{
    uint8_t* civPacket;
    uint16_t civLength;
    
    if (packet.isCivPacket(&civPacket, &civLength))
    {
        // ignore for now except to get the CI-V ID of the 705. TBD
        ESP_LOGI(parent_->getName().c_str(), "Received CIV packet (from %02x, to %02x, type %02x)", civPacket[3], civPacket[2], civPacket[4]);
        
        if (civPacket[2] == 0xE0)
        {
            civId_ = civPacket[3];
        }
    }
    else
    {
        TrackedPacketState::onReceivePacket(packet);
    }
}

void CIVState::sendCIVOpenPacket_()
{
    ESP_LOGI(parent_->getName().c_str(), "Sending CIV open packet");
    auto packet = IcomPacket::CreateCIVOpenClosePacket(civSequenceNumber_++, parent_->getOurIdentifier(), parent_->getTheirIdentifier(), false);
    sendTracked_(packet);
}

void CIVState::sendCIVPacket_(uint8_t* civPacket, uint16_t civLength)
{
    ESP_LOGI(parent_->getName().c_str(), "Sending CIV data packet");
    auto packet = IcomPacket::CreateCIVPacket(parent_->getOurIdentifier(), parent_->getTheirIdentifier(), civSequenceNumber_++, civPacket, civLength);
    sendTracked_(packet);
}

void CIVState::onFreeDVSetPTTStateMessage_(DVTask* origin, ezdv::audio::FreeDVSetPTTStateMessage* message)
{
    if (civId_ > 0)
    {
        ESP_LOGI(parent_->getName().c_str(), "Sending PTT CIV message (PTT = %d)", message->pttState);
        
        uint8_t civPacket[] = {
            0xFE,
            0xFE,
            civId_,
            0xE0,
            0x1C, // PTT on/off command/subcommand
            0x00,
            message->pttState ? (uint8_t)0x01 : (uint8_t)0x00,
            0xFD
        };
        
        sendCIVPacket_(civPacket, sizeof(civPacket));
    }
}

}

}

}