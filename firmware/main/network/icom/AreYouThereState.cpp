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

#include "esp_log.h"
#include "AreYouThereState.h"
#include "IcomStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

AreYouThereState::AreYouThereState(IcomStateMachine* parent)
    : IcomProtocolState(parent)
    , resendTimer_(parent_->getTask(), std::bind(&AreYouThereState::retrySend_, this), MS_TO_US(AREYOUTHERE_PERIOD), "IcomResendTimer")
{
    // empty
}
 
 void AreYouThereState::onEnterState()
 {
    ESP_LOGI(parent_->getName().c_str(), "Entering state");
        
    // Send packet.
    auto packet = IcomPacket::CreateAreYouTherePacket(parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    parent_->sendUntracked(packet);

    // Start retry timer
    resendTimer_.start();
 }

void AreYouThereState::onExitState()
{
    ESP_LOGI(parent_->getName().c_str(), "Leaving state");

    // Stop retry timer
    resendTimer_.stop();
}
    
std::string AreYouThereState::getName()
{
    return "AreYouThere";
}

void AreYouThereState::onReceivePacket(IcomPacket& packet)
{
    uint32_t theirId = 0;
    if (packet.isIAmHere(theirId))
    {
        ESP_LOGI(parent_->getName().c_str(), "Received I Am Here from %lx", theirId);
        
        parent_->setTheirIdentifier(theirId);
        parent_->transitionState(IcomProtocolState::ARE_YOU_READY);
    }
    
    // Ignore unexpected packets. TBD -- may need to send Disconnect instead?
}

void AreYouThereState::retrySend_()
{
    ESP_LOGI(parent_->getName().c_str(), "Retrying send");
        
    // Send packet.
    auto packet = IcomPacket::CreateAreYouTherePacket(parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    parent_->sendUntracked(packet);
}

}

}

}