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
#include "TrackedPacketState.h"
#include "IcomStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

TrackedPacketState::TrackedPacketState(IcomStateMachine* parent)
    : IcomProtocolState(parent)
    , pingTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onPingTimer_, this), MS_TO_US(PING_PERIOD))
    , idleTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onIdleTimer_, this), MS_TO_US(IDLE_PERIOD))
    , pingSequenceNumber_(0)
    , sendSequenceNumber_(1) // Start sequence at 1.
    //, civSequenceNumber_(0)
    , numSavedBytesInPacketQueue_(0)
{
    // empty
}

void TrackedPacketState::onEnterState()
{
    ESP_LOGI(parent_->getName().c_str(), "Entering state");

    // Reset sequence numbers.
    pingSequenceNumber_ = 0;
    sendSequenceNumber_ = 1; // Start sequence at 1.
    //civSequenceNumber_ = 0;

    // Reset sent packets list
    sentPackets_.clear();
    numSavedBytesInPacketQueue_ = 0;

    // Start ping and idle timers at this point. Idle will be stopped/started
    // whenever we send something.
    pingTimer_.start();
    idleTimer_.start();
}

void TrackedPacketState::onExitState()
{
    // TBD: cleanup
}

void TrackedPacketState::onReceivePacket(IcomPacket& packet)
{
    uint16_t pingSequence;
    bool packetSent;
    std::vector<uint16_t> retryPackets;

    if (packet.isPingRequest(pingSequence))
    {
        // Respond to ping requests        
        //ESP_LOGI(sm_.get_name().c_str(), "Got ping, seq %d", ctr, pingSequence);
        auto packet = std::move(IcomPacket::CreatePingAckPacket(pingSequence, parent_->getOurIdentifier(), parent_->getTheirIdentifier()));
        parent_->sendUntracked(packet);
        packetSent = true;
    }
    else if (packet.isPingResponse(pingSequence))
    {
        // Got ping response, increment to next ping sequence number.
        //ESP_LOGI(sm_.get_name().c_str(), "Got ping ack, seq %d", pingSequence);
        incrementPingSequence_(pingSequence);
        
        //ESP_LOGI("HEAP", "Free memory: %d", xPortGetFreeHeapSize());
    }
    else if (packet.isRetransmitPacket(retryPackets))
    {
        for (auto packetId : retryPackets)
        {
            retransmitPacket_(packetId);
        }
    }

    if (packetSent)
    {
        // Recycle idle timer as we sent something non-idle.
        idleTimer_.stop();
        idleTimer_.start();
    }
}

void TrackedPacketState::sendTracked_(IcomPacket& packet)
{
    uint8_t* rawPacket = const_cast<uint8_t*>(packet.getData());
    
    // If sequence number is now 0, we've probably rolled over.
    if (sendSequenceNumber_ == 0)
    {
        sentPackets_.clear();
        numSavedBytesInPacketQueue_ = 0;
    }
    else
    {
        // Iterate through the current sent queue and delete packets
        // that are older than PURGE_SECONDS.
        auto iter = sentPackets_.begin();
        auto curTime = time(NULL);
        while (iter != sentPackets_.end())
        {
            if (curTime - iter->second.first >= PURGE_SECONDS)
            {
                numSavedBytesInPacketQueue_ -= iter->second.second.getSendLength();
                iter = sentPackets_.erase(iter);
            }
            else
            {
                iter++;
            }
        }
        
        // If we're still going to have more than MAX_NUM_BYTES_AVAILABLE_FOR_RETRANSMIT
        // in the queue after adding this packet, go ahead and delete some more.
        if ((numSavedBytesInPacketQueue_ + packet.getSendLength()) >= MAX_NUM_BYTES_AVAILABLE_FOR_RETRANSMIT)
        {
            auto iter = sentPackets_.begin();
            while (iter != sentPackets_.end())
            {
                numSavedBytesInPacketQueue_ -= iter->second.second.getSendLength();
                iter = sentPackets_.erase(iter);
            
                if (numSavedBytesInPacketQueue_ < MAX_NUM_BYTES_AVAILABLE_FOR_RETRANSMIT)
                {
                    break;
                }
            }
        }
    }
    
    // We need to manually force the sequence number into the packet because
    // simply treating it like a control_packet doesn't work.
    rawPacket[6] = sendSequenceNumber_ & 0xFF;
    rawPacket[7] = (sendSequenceNumber_ >> 8) & 0xFF;
    sendSequenceNumber_++;
    
    numSavedBytesInPacketQueue_ += packet.getSendLength();
    parent_->sendUntracked(packet);
    sentPackets_[sendSequenceNumber_ - 1] = std::pair(time(NULL), std::move(packet));
}

void TrackedPacketState::sendPing_()
{
    auto packet = IcomPacket::CreatePingPacket(pingSequenceNumber_, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    parent_->sendUntracked(packet);
}

void TrackedPacketState::onPingTimer_()
{
    // Ping timer fired. Send ping request.
    //ESP_LOGI(sm_.get_name().c_str(), "Send ping, seq %d", sm_.getCurrentPingSequence());
    auto packet = IcomPacket::CreatePingPacket(pingSequenceNumber_, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    parent_->sendUntracked(packet);
    idleTimer_.stop();
    idleTimer_.start();
}

void TrackedPacketState::onIdleTimer_()
{
    // Idle timer fired. Send control packet with seq = 0
    auto packet = IcomPacket::CreateIdlePacket(0, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    parent_->sendUntracked(packet);
}

void TrackedPacketState::incrementPingSequence_(uint16_t pingSeq)
{
    pingSequenceNumber_ = pingSeq + 1;
}

void TrackedPacketState::retransmitPacket_(uint16_t packet)
{
    ESP_LOGI(parent_->getName().c_str(), "Retransmitting packet %d", packet);
    
    if (sentPackets_.find(packet) != sentPackets_.end())
    {
        // No need to track as we've sent it before.
        parent_->sendUntracked(sentPackets_[packet].second);
    }
    else
    {
        // Send idle packet with the same seq# if we can't find the original packet.
        IcomPacket tmpPacket = IcomPacket::CreateIdlePacket(packet, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
        parent_->sendUntracked(tmpPacket);
    }
}

}

}

}