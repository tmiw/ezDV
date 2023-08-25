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

#include "esp_dsp.h"

#include <cstring>
#include <cmath>
#include "IcomSocketTask.h"
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
    , audioOutTimer_(parent_->getTask(), std::bind(&AudioState::onAudioOutTimer_, this), MS_TO_US(20))
    , audioWatchdogTimer_(parent_->getTask(), std::bind(&AudioState::onAudioWatchdog_, this), MS_TO_US(WATCHDOG_PERIOD))
    , audioSequenceNumber_(0)
{
    parent->getTask()->registerMessageHandler(this, &AudioState::onRightChannelVolumeMessage_);

    for (int index = 0; index < 160; index++)
    {
        audioMultiplier_[index] = 1;
    }
}

void AudioState::onEnterState()
{
    TrackedPacketState::onEnterState();

    // Reset sequence number
    audioSequenceNumber_ = 0;

    // Start audio output timer
    audioOutTimer_.start();
    
    // Start watchdog
    audioWatchdogTimer_.start();
    
    // Grab current volumes to make sure we properly recover TX ALC.
    storage::RequestVolumeSettingsMessage requestMessage;
    parent_->getTask()->publish(&requestMessage);
}

void AudioState::onExitState()
{
    audioOutTimer_.stop();
    audioWatchdogTimer_.stop();

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
        // Restart watchdog
        audioWatchdogTimer_.stop();
        audioWatchdogTimer_.start();
        
        auto task = (IcomSocketTask*)(parent_->getTask());
        auto outputFifo = task->getAudioOutput(ezdv::audio::AudioInput::LEFT_CHANNEL);
        if (outputFifo != nullptr)
        {
            int totalSize = (packet.getSendLength() - 0x18) / sizeof(short);
            codec2_fifo_write(outputFifo, audioData, totalSize); 
        }
    }

    // Call into parent to perform missing packet handling.
    TrackedPacketState::onReceivePacket(packet);
}

void AudioState::onAudioWatchdog_()
{
    ESP_LOGW(parent_->getName().c_str(), "No audio data received recently, reconnecting channel");
    parent_->transitionState(IcomProtocolState::ARE_YOU_THERE);
}

void AudioState::onAudioOutTimer_()
{
    auto task = (IcomSocketTask*)(parent_->getTask());
    auto inputFifo = task->getAudioInput(ezdv::audio::AudioInput::LEFT_CHANNEL);
    if (inputFifo == nullptr)
    {
        ESP_LOGE(parent_->getName().c_str(), "input fifo is null for some reason!");
        return;
    }
    
    // Get input audio and write to socket
    uint16_t samplesToRead = 160; // 320 bytes
    short tempAudioOut[samplesToRead];
    float tempAudioInFloat[samplesToRead];
    float tempAudioOutFloat[samplesToRead];
    //memset(tempAudioOut, 0, samplesToRead * sizeof(short));

    if (codec2_fifo_used(inputFifo) >= samplesToRead)
    {
        codec2_fifo_read(inputFifo, tempAudioOut, samplesToRead);

        for (int index = 0; index < samplesToRead; index++)
        {
            tempAudioInFloat[index] = tempAudioOut[index];
        }

        // Adjust output based on configured volume
        dsps_mul_f32(tempAudioInFloat, audioMultiplier_, tempAudioOutFloat, samplesToRead, 1, 1, 1);

        for (int index = 0; index < samplesToRead; index++)
        {
            tempAudioOut[index] = tempAudioOutFloat[index];
        }
    
        auto packet = IcomPacket::CreateAudioPacket(
            audioSequenceNumber_++,
            parent_->getOurIdentifier(), 
            parent_->getTheirIdentifier(), 
            tempAudioOut, 
            samplesToRead);

        sendTracked_(packet);
    }
}

void AudioState::onRightChannelVolumeMessage_(DVTask* origin, storage::RightChannelVolumeMessage* message)
{
    float volInDb = 0.5 * message->volume;

    float multiplier = exp(volInDb/20.0 * log(10.0));
    for (int index = 0; index < 160; index++)
    {
        audioMultiplier_[index] = multiplier;
    }
}

}

}

}