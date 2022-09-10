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

#include <cstring>
#include "audio/AudioInput.h"
#include "AudioState.h"
#include "IcomStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

AudioState::AudioState(IcomStateMachine* parent)
    : TrackedPacketState(parent)
    , audioOutTimer_(parent_->getTask(), std::bind(&AudioState::onAudioOutTimer_, this), MS_TO_US(20000))
    , audioSequenceNumber_(0)
{
    // empty
}

void AudioState::onEnterState()
{
    TrackedPacketState::onEnterState();

    // Reset sequence number
    audioSequenceNumber_ = 0;

    // Start audio output timer
    audioOutTimer_.start();
}

void AudioState::onExitState()
{
    // TBD: cleanup
    audioOutTimer_.stop();

    TrackedPacketState::onExitState();
}

std::string AudioState::getName()
{
    return "Audio";
}

void AudioState::onReceivePacket(IcomPacket& packet)
{
    uint16_t audioSeqId;
    short* audioData;

    if (packet.isAudioPacket(audioSeqId, &audioData))
    {
        /*
        if (audioSeqId == 0)
        {
            // Clear RX buffer
            sm_.rxAudioPackets_.clear();
        }
        
        // The audio packet has to one we haven't already received to go in the map.
        if (audioSeqId > sm_.lastAudioPacketSeqId_ && sm_.rxAudioPackets_.find(audioSeqId) == sm_.rxAudioPackets_.end())
        {
            sm_.lastAudioPacketSeqId_ = audioSeqId;
            sm_.rxAudioPackets_[audioSeqId] = packet;
        }
        
        // Iterate through map and look for gaps in the stream. We'll need to rerequest those missing
        // packets.
        int first = -1, second = -1;
        std::vector<uint16_t> packetIdsToRetransmit;
        for (auto& iter : sm_.rxAudioPackets_)
        {
            first = second;
            second = iter.first;
            
            if (first != -1 && second != -1)
            {
                for (int seq = first; seq < second; seq++)
                {
                    // gap found, request packets
                    packetIdsToRetransmit.push_back(seq);
                    
                    ESP_LOGI(
                        sm_.get_name().c_str(), 
                        "Requesting retransmit of packet %d",
                        seq);
                }
            }
        }
        
        auto reqPacket = IcomPacket::CreateRetransmitRequest(sm_.getOurIdentifier(), sm_.getTheirIdentifier(), packetIdsToRetransmit);
        sm_.sendUntracked(reqPacket);*/
        int totalSize = (packet.getSendLength() - 0x18) / sizeof(short);

        auto task = (ezdv::audio::AudioInput*)(parent_->getTask());
        auto outputFifo = task->getAudioOutput(ezdv::audio::AudioInput::LEFT_CHANNEL);
        if (outputFifo != nullptr)
        {
            codec2_fifo_write(outputFifo, audioData, totalSize); 
        }
    }
    else
    {
        TrackedPacketState::onReceivePacket(packet);
    }
}

void AudioState::onAudioOutTimer_()
{
    auto task = (ezdv::audio::AudioInput*)(parent_->getTask());
    auto inputFifo = task->getAudioInput(ezdv::audio::AudioInput::LEFT_CHANNEL);

    // Get input audio and write to socket
    short tempAudioOut[MAX_PACKET_SIZE];
    memset(tempAudioOut, 0, MAX_PACKET_SIZE * sizeof(short));

    uint16_t samplesToRead = 160; // 320 bytes
    if (codec2_fifo_used(inputFifo) >= samplesToRead)
    {
        codec2_fifo_read(inputFifo, tempAudioOut, samplesToRead);
        auto packet = IcomPacket::CreateAudioPacket(
            audioSequenceNumber_++,
            parent_->getOurIdentifier(), 
            parent_->getTheirIdentifier(), 
            tempAudioOut, 
            samplesToRead);

        sendTracked_(packet);
    }
}

}

}

}