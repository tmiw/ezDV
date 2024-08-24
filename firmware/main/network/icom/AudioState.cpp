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

// The below values are calculated values for each possible audio multiplier
// in terms of Q5.11 fixed point. The formula for calculating these is:
//
//     e^(volInDb/20.0 * ln(10.0))
//
// And the equivalent fixed point values are calculated by using the calculator
// at https://chummersone.github.io/qformat.html.
//
// (Note: ezDV operates in the range of -63.5dB to +24dB amplification.)
static short FIXED_POINT_AMPLIFICATION_FACTORS[] = {
    1,
    1,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    3,
    3,
    3,
    3,
    3,
    3,
    4,
    4,
    4,
    4,
    5,
    5,
    5,
    5,
    6,
    6,
    6,
    7,
    7,
    8,
    8,
    9,
    9,
    10,
    10,
    11,
    12,
    12,
    13,
    14,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    22,
    23,
    24,
    26,
    27,
    29,
    31,
    32,
    34,
    36,
    39,
    41,
    43,
    46,
    49,
    51,
    54,
    58,
    61,
    65,
    69,
    73,
    77,
    82,
    86,
    91,
    97,
    103,
    109,
    115,
    122,
    129,
    137,
    145,
    154,
    163,
    172,
    183,
    193,
    205,
    217,
    230,
    243,
    258,
    273,
    289,
    306,
    325,
    344,
    364,
    386,
    409,
    433,
    458,
    486,
    514,
    545,
    577,
    611,
    648,
    686,
    727,
    770,
    815,
    864,
    915,
    969,
    1026,
    1087,
    1152,
    1220,
    1292,
    1369,
    1450,
    1536,
    1627,
    1723,
    1825,
    1933,
    2048,
    2169,
    2298,
    2434,
    2578,
    2731,
    2893,
    3064,
    3246,
    3438,
    3642,
    3858,
    4086,
    4328,
    4585,
    4857,
    5144,
    5449,
    5772,
    6114,
    6476,
    6860,
    7267,
    7697,
    8153,
    8636,
    9148,
    9690,
    10264,
    10873,
    11517,
    12199,
    12922,
    13688,
    14499,
    15358,
    16268,
    17232,
    18253,
    19334,
    20480,
    21694,
    22979,
    24341,
    25783,
    27311,
    28929,
    30643,
};

#define MIN_AMPLIFICATION_DB (-127) /* -63.5dB minimum amplification by TLV320 */
#define UNITY_AMPLIFICATION_VAL (2048) /* 1.0 */

namespace ezdv
{

namespace network
{

namespace icom
{

AudioState::AudioState(IcomStateMachine* parent)
    : TrackedPacketState(parent)
    , audioOutTimer_(parent_->getTask(), this, &AudioState::onAudioOutTimer_, MS_TO_US(20), "IcomAudioOutTimer")
    , audioWatchdogTimer_(parent_->getTask(), this, &AudioState::onAudioWatchdog_, MS_TO_US(WATCHDOG_PERIOD), "IcomAudioWatchdogTimer")
    , audioSequenceNumber_(0)
    , completingTransmit_(false)
{
    parent->getTask()->registerMessageHandler(this, &AudioState::onRightChannelVolumeMessage_);
    parent->getTask()->registerMessageHandler(this, &AudioState::onTransmitCompleteMessage_);

    for (int index = 0; index < 160; index++)
    {
        audioMultiplier_[index] = UNITY_AMPLIFICATION_VAL;
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

void AudioState::onAudioWatchdog_(DVTimer*)
{
    ESP_LOGW(parent_->getName().c_str(), "No audio data received recently, reconnecting channel");
    parent_->transitionState(IcomProtocolState::ARE_YOU_THERE);
}

void AudioState::onAudioOutTimer_(DVTimer*)
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
    //memset(tempAudioOut, 0, samplesToRead * sizeof(short));

    if (codec2_fifo_used(inputFifo) >= samplesToRead)
    {
        codec2_fifo_read(inputFifo, tempAudioOut, samplesToRead);

        // Adjust output based on configured volume.
        // Note that since audioMultiplier_ is a Q5.11 fixed point number,
        // the result pre-shift is a Q6.27 fixed point number. Shifting
        // by 11 should cancel this out and result in the proper precision
        // again.
        dsps_mul_s16(tempAudioOut, audioMultiplier_, tempAudioOut, samplesToRead, 1, 1, 1, 11);
    
        auto packet = IcomPacket::CreateAudioPacket(
            audioSequenceNumber_++,
            parent_->getOurIdentifier(), 
            parent_->getTheirIdentifier(), 
            tempAudioOut, 
            samplesToRead);

        sendTracked_(packet);
    }
    else if (completingTransmit_)
    {
        completingTransmit_ = false;
        
        StopTransmitMessage message;
        parent_->getTask()->publish(&message);
    }
}

void AudioState::onRightChannelVolumeMessage_(DVTask* origin, storage::RightChannelVolumeMessage* message)
{
    short calcResult = FIXED_POINT_AMPLIFICATION_FACTORS[message->volume - MIN_AMPLIFICATION_DB];

    for (int index = 0; index < 160; index++)
    {
        audioMultiplier_[index] = calcResult;
    }
}

void AudioState::onTransmitCompleteMessage_(DVTask* origin, ezdv::audio::TransmitCompleteMessage* message)
{
    // Set completingTransmit_ to true. This will let us know to send the CI-V command to stop
    // TX as soon as there's nothing left in the TX buffer.
    completingTransmit_ = true;
}

}

}

}