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
#include "IcomMessage.h"

namespace ezdv
{

namespace network
{

namespace icom
{

TrackedPacketState::TrackedPacketState(IcomStateMachine* parent)
    : IcomProtocolState(parent)
    , pingTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onPingTimer_, this), MS_TO_US(PING_PERIOD), "IcomPingTimer")
    , idleTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onIdleTimer_, this), MS_TO_US(IDLE_PERIOD), "IcomIdleTimer")
    , retransmitRequestTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onRetransmitTimer_, this), MS_TO_US(RETRANSMIT_PERIOD), "IcomRetransmitRequestTimer")
    , txRetransmitTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onTxRetransmitTimer_, this), MS_TO_US(TX_RETRANSMIT_PERIOD), "IcomTxRetransmitTimer")
    , cleanupTimer_(parent_->getTask(), std::bind(&TrackedPacketState::onCleanupTimer_, this), MS_TO_US(WATCHDOG_PERIOD), "IcomCleanupTimer")
    , pingSequenceNumber_(0)
    , sendSequenceNumber_(1) // Start sequence at 1.
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

    // Reset sent packets list
    sentPackets_.clear();
    numSavedBytesInPacketQueue_ = 0;

    // Reset received packets
    rxPacketIds_.clear();
    rxMissingPacketIds_.clear();
    
    // Start ping, retransmit and idle timers at this point. Idle will be stopped/started
    // whenever we send something.
    pingTimer_.start();
    idleTimer_.start();
    retransmitRequestTimer_.start();
    cleanupTimer_.start();
}

void TrackedPacketState::onExitState()
{
    ESP_LOGI(parent_->getName().c_str(), "Leaving state");
    
    // Send disconnect packet a bunch of times to make sure the radio
    // actually gets the message. This is needed because once we
    // leave this state we're not going to be able to repeatedly
    // retransmit it.
    for (int count = 0; count < 10; count++)
    {
        auto packet = IcomPacket::CreateDisconnectPacket(parent_->getOurIdentifier(), parent_->getTheirIdentifier());
        parent_->sendUntracked(packet);
    }
    
    // Stop timers
    pingTimer_.stop();
    idleTimer_.stop();
    retransmitRequestTimer_.stop();
    cleanupTimer_.stop();
    txRetransmitTimer_.stop();
}

void TrackedPacketState::onReceivePacket(IcomPacket& packet)
{
    uint16_t pingSequence;
    bool packetSent;
    std::vector<uint16_t> retryPackets;
    bool addReceivedPacket = false;

    if (packet.isPingRequest(pingSequence))
    {
        // Respond to ping requests        
        //ESP_LOGI(sm_.get_name().c_str(), "Got ping, seq %d", ctr, pingSequence);
        auto packet = IcomPacket::CreatePingAckPacket(pingSequence, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
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
        ESP_LOGI(parent_->getName().c_str(), "Received retransmit packet (currSendSeq: %d)", sendSequenceNumber_);
        for (auto packetId : retryPackets)
        {
            txRetryPacketIds_[packetId] = 1;
        }
        
        txRetransmitTimer_.stop();
        txRetransmitTimer_.start(true);
    }
    else
    {
        addReceivedPacket = true;
    }

    if (packetSent)
    {
        // Recycle idle timer as we sent something non-idle.
        idleTimer_.stop();
        idleTimer_.start();
    }
    
    // Missing packet list processing    
    if (addReceivedPacket)
    {
        auto controlPacket = packet.getConstTypedPacket<control_packet>();
        assert(controlPacket != nullptr);
        
        auto rxSeq = controlPacket->seq;
        
        if (rxPacketIds_.size() == 0)
        {
            // Add packet to received list if we just started
            rxPacketIds_.push_back(rxSeq);
        }
        else
        {
            auto firstSeqInBuffer = *rxPacketIds_.begin();
            auto lastSeqInBuffer = *rxPacketIds_.rbegin();
            if (rxSeq < firstSeqInBuffer || (rxSeq - lastSeqInBuffer) > MAX_MISSING)
            {
                // Too many missing packets, clear buffer and add.
                ESP_LOGE(parent_->getName().c_str(), "Too many missing packets, resetting!");
                //ESP_LOGI(parent_->getName().c_str(), "rxSeq = %d, 1st = %d, last = %d", rxSeq, firstSeqInBuffer, lastSeqInBuffer);
                rxPacketIds_.clear();
                rxMissingPacketIds_.clear();
                rxPacketIds_.push_back(rxSeq);
            }
            else
            {
                auto iter = std::find(rxPacketIds_.begin(), rxPacketIds_.end(), rxSeq);
                if (iter == rxPacketIds_.end())
                {
                    if ((rxPacketIds_.size() + 1) > BUFSIZE)
                    {
                        // Make sure RX packet list is no bigger than BUFSIZE
                        rxPacketIds_.erase(rxPacketIds_.begin());
                    }
                    
                    rxPacketIds_.push_back(rxSeq);
                    
                    auto missingIter = rxMissingPacketIds_.find(rxSeq);
                    if (missingIter != rxMissingPacketIds_.end())
                    {
                        rxMissingPacketIds_.erase(missingIter);
                    }
                    else
                    {
                        if (rxSeq > (lastSeqInBuffer + 1))
                        {
                            // Detected missing packets!
                            ESP_LOGW(parent_->getName().c_str(), "Detected missing packets from seq = %d to %d", lastSeqInBuffer + 1, rxSeq);
                            
                            // Don't add to the missing packets list. Because of the way ezDV works,
                            // we wouldn't be able to incorporate these retransmitted packets into e.g.
                            // any audio being fed to higher layers.
                            /*for (int id = lastSeqInBuffer + 1; id < rxSeq; id++)
                            {
                                rxMissingPacketIds_[id] = 1;
                            }*/
                        }
                    }
                }
            }
        }
    }
}

void TrackedPacketState::sendTracked_(IcomPacket& packet)
{
    uint8_t* rawPacket = const_cast<uint8_t*>(packet.getData());
    
    // If sequence number rolled over, clear the sent packets list.
    if (sendSequenceNumber_ == 0)
    {
        ESP_LOGI(parent_->getName().c_str(), "Rollover detected, resetting sent packet list");
        sentPackets_.clear();
        numSavedBytesInPacketQueue_ = 0;
    }
    else if (sentPackets_.size() > BUFSIZE)
    {
        sentPackets_.erase(sentPackets_.begin());
    }
    
    // We need to manually force the sequence number into the packet because
    // simply treating it like a control_packet doesn't work.
    rawPacket[6] = sendSequenceNumber_ & 0xFF;
    rawPacket[7] = (sendSequenceNumber_ >> 8) & 0xFF;
    
    numSavedBytesInPacketQueue_ += packet.getSendLength();
    parent_->sendUntracked(packet);
    sentPackets_[sendSequenceNumber_] = std::pair(time(NULL), std::move(packet));
    sendSequenceNumber_++;
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

void TrackedPacketState::onTxRetransmitTimer_()
{
    for (auto& kvp : txRetryPacketIds_)
    {
        retransmitPacket_(kvp.first);
    }
    
    txRetryPacketIds_.clear();
}
    
void TrackedPacketState::onRetransmitTimer_()
{
    if (rxMissingPacketIds_.size() == 0)
    {
        // Skip processing if there are no missing packets.
        return;
    }
    else if (rxMissingPacketIds_.size() > MAX_MISSING)
    {
        ESP_LOGE(parent_->getName().c_str(), "Too many missing packets while processing retransmit, resetting!");
        rxPacketIds_.clear();
        rxMissingPacketIds_.clear();
        return;
    }
    
    std::vector<uint16_t> retransmitList;
    for (auto& packet : rxMissingPacketIds_)
    {
        retransmitList.push_back(packet.first);
    }
    
    // Send retransmit request
    auto packet = IcomPacket::CreateRetransmitRequest(parent_->getOurIdentifier(), parent_->getTheirIdentifier(), retransmitList);
    parent_->sendUntracked(packet);
}

void TrackedPacketState::onCleanupTimer_()
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
}

void TrackedPacketState::incrementPingSequence_(uint16_t pingSeq)
{
    pingSequenceNumber_ = pingSeq + 1;
}

void TrackedPacketState::retransmitPacket_(uint16_t packet)
{    
    if (sentPackets_.find(packet) != sentPackets_.end())
    {
        // No need to track as we've sent it before.
        ESP_LOGI(parent_->getName().c_str(), "Retransmitting packet %d", packet);
        parent_->sendUntracked(sentPackets_[packet].second);
    }
    else
    {
        // Send idle packet with the same seq# if we can't find the original packet.
        IcomPacket tmpPacket = IcomPacket::CreateIdlePacket(packet, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
        ESP_LOGI(parent_->getName().c_str(), "Packet %d not found, transmitting idle packet instead", packet);
        parent_->sendUntracked(tmpPacket);
    }
}

}

}

}