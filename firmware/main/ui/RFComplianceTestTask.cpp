/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
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
#include <cmath>

#include "RFComplianceTestTask.h"
#include "driver/LedMessage.h"

#define CURRENT_LOG_TAG ("RfComplianceTestTask")

// TBD -- check that these calculations happen at compile time and not runtime.
#define SAMPLE_RATE 8000
#define SAMPLE_RATE_RECIP ((float)1.0/(float)SAMPLE_RATE)
#define LEFT_FREQ_HZ ((float)1275.0)
#define LEFT_FREQ_HZ_RECIP ((float)1.0/LEFT_FREQ_HZ)
#define RIGHT_FREQ_HZ ((float)1725.0)
#define RIGHT_FREQ_HZ_RECIP ((float)1.0/RIGHT_FREQ_HZ)

#define SAMPLES_PER_TICK ((float)0.02*(float)SAMPLE_RATE)

// Max amplitude of the test sine waves is 32767. This has been experimentally determined to 
// produce ~0 dB for the sine wave frequency (without clipping) in an Audacity spectrum plot 
// using the following setup:
//
// * SignaLink USB
// * TLV320 volume set to 0 dB
// 
// Lack of clipping has also been verified by ensuring Effect->Volume and Compression->Amplify
// suggests a positive "Amplification (dB)" value after recording the audio from the TLV320.
//
// NOTE: the max amplitude is assuming no LPF on the output (true for v0.6 HW). The TLV320
/// volume and/or amplitude here may need to be adjusted once a LPF is added.
#define CODEC_VOLUME_LEVEL 0
#define SINE_WAVE_AMPLITUDE ((float)32767.0)

// Defined in Application.cpp.
extern void StartSleeping();

namespace ezdv
{

namespace ui
{

RfComplianceTestTask::RfComplianceTestTask(ezdv::driver::LedArray* ledArrayTask, ezdv::driver::TLV320* tlv320Task)
    : DVTask("RfComplianceTestTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 32, pdMS_TO_TICKS(20))
    , AudioInput(1, 2)
    , leftChannelSineWave_(LEFT_FREQ_HZ, SINE_WAVE_AMPLITUDE)
    , rightChannelSineWave_(RIGHT_FREQ_HZ, SINE_WAVE_AMPLITUDE)
    , isActive_(false)
    , ledArrayTask_(ledArrayTask)
    , tlv320Task_(tlv320Task)
    , leftChannelCtr_(0)
    , rightChannelCtr_(0)
    , currentMode_(0)
    , pttCtr_(0)
{
    registerMessageHandler(this, &RfComplianceTestTask::onButtonShortPressedMessage_);
    registerMessageHandler(this, &RfComplianceTestTask::onButtonLongPressedMessage_);
    registerMessageHandler(this, &RfComplianceTestTask::onButtonReleasedMessage_);
}

RfComplianceTestTask::~RfComplianceTestTask()
{
    // empty
}

void RfComplianceTestTask::onTaskStart_()
{
    isActive_ = true;
    
    // Enable all LEDs with 50% duty cycle
    storage::LedBrightnessSettingsMessage brightnessMessage;
    brightnessMessage.dutyCycle = 4096;
    ledArrayTask_->post(&brightnessMessage);

    ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, true);
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
    publish(&msg);
    
    // Set TLV320 to max volume.
    storage::LeftChannelVolumeMessage leftChanVolMessage(CODEC_VOLUME_LEVEL);
    storage::RightChannelVolumeMessage rightChanVolMessage(CODEC_VOLUME_LEVEL);
    tlv320Task_->post(&leftChanVolMessage);
    tlv320Task_->post(&rightChanVolMessage);
}

void RfComplianceTestTask::onTaskSleep_()
{
    isActive_ = false;
}

void RfComplianceTestTask::onButtonShortPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message)
{
    ezdv::driver::SetLedStateMessage msg;
    msg.ledState = false;
    
    // Turn off LED for pressed button
    switch (message->button)
    {
        case driver::ButtonLabel::PTT:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
            break;
        case driver::ButtonLabel::MODE:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::SYNC;
            break;
        case driver::ButtonLabel::VOL_UP:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
            break;
        case driver::ButtonLabel::VOL_DOWN:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
            break;
        default:
            // ignore unknown buttons
            return;
    }
    
    publish(&msg);
}

void RfComplianceTestTask::onButtonLongPressedMessage_(DVTask* origin, driver::ButtonLongPressedMessage* message)
{
    if (isActive_)
    {
        if (message->button == driver::ButtonLabel::MODE)
        {
            // Long press Mode button triggers shutdown, all other long presses currently ignored
            StartSleeping();
        }
    }
}

void RfComplianceTestTask::onButtonReleasedMessage_(DVTask* origin, driver::ButtonReleasedMessage* message)
{
    ezdv::driver::SetLedStateMessage msg;
    msg.ledState = true;
    
    // Turn on LED for released button
    switch (message->button)
    {
        case driver::ButtonLabel::PTT:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
            break;
        case driver::ButtonLabel::MODE:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::SYNC;
            
            // Cycle between supported modes:
            // 1. Sine wave, both channels.
            // 2. Sine wave, left channel only.
            // 3. Sine wave, right channel only.
            // 4. Square wave, both channels.
            // 5. Square wave, left channel only.
            // 6. Square wave, right channel only.
            // 7. No audio.
            currentMode_++;
            if (currentMode_ >= 7)
            {
                currentMode_ = 0;
            }
            break;
        case driver::ButtonLabel::VOL_UP:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
            break;
        case driver::ButtonLabel::VOL_DOWN:
            msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
            break;
        default:
            // ignore unknown buttons
            return;
    }
    
    publish(&msg);
}

void RfComplianceTestTask::onTaskTick_()
{
    if (isActive_)
    {
        //auto timeBegin = esp_timer_get_time();
        
        if (currentMode_ >= 0 && currentMode_ <= 2)
        {
            sineWave_();
        }
        else if (currentMode_ >= 3 && currentMode_ <= 5)
        {
            squareWave_();
        }
        else if (currentMode_ == 6)
        {
            // do nothing, silence
        }
        
        // Get some I2C traffic flowing.
        storage::LeftChannelVolumeMessage leftChanVolMessage(CODEC_VOLUME_LEVEL);
        storage::RightChannelVolumeMessage rightChanVolMessage(CODEC_VOLUME_LEVEL);
        tlv320Task_->post(&leftChanVolMessage);
        tlv320Task_->post(&rightChanVolMessage);
        
        // Toggle PTT line on radio jack every 20ms as long as there's audio on either jack.
        if (currentMode_ != 6)
        {
            pttCtr_++;
            ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::PTT_NPN, (pttCtr_ & (1 << 1)) != 0);
            publish(&msg);
        }
    }
}

void RfComplianceTestTask::sineWave_()
{
    struct FIFO* outputLeftFifo = getAudioOutput(AudioInput::LEFT_CHANNEL);
    struct FIFO* outputRightFifo = getAudioOutput(AudioInput::RIGHT_CHANNEL);
    
    if (currentMode_ == 0 || currentMode_ == 1)
    {
        for (int index = 0; index < SAMPLES_PER_TICK; index++)
        {
            if (leftChannelCtr_ >= SAMPLE_RATE)
            {
                leftChannelCtr_ = 0;
            }
        
            if (codec2_fifo_free(outputLeftFifo) > 0)
            {
                short sample = leftChannelSineWave_.getSample(leftChannelCtr_++);
                codec2_fifo_write(outputLeftFifo, &sample, 1);
            }
        }
    }
    
    if (currentMode_ == 0 || currentMode_ == 2)
    {
        for (int index = 0; index < SAMPLES_PER_TICK; index++)
        {
            if (rightChannelCtr_ >= SAMPLE_RATE)
            {
                rightChannelCtr_ = 0;
            }
        
            if (codec2_fifo_free(outputRightFifo) > 0)
            {
                short sample = rightChannelSineWave_.getSample(rightChannelCtr_++);
                codec2_fifo_write(outputRightFifo, &sample, 1);
            }
        }
    }
}

void RfComplianceTestTask::squareWave_()
{
    struct FIFO* outputLeftFifo = getAudioOutput(AudioInput::LEFT_CHANNEL);
    struct FIFO* outputRightFifo = getAudioOutput(AudioInput::RIGHT_CHANNEL);
    
    if (currentMode_ == 3 || currentMode_ == 4)
    {
        for (int index = 0; index < SAMPLES_PER_TICK; index++)
        {
            int numSamplesPerPeriod = (int)(SAMPLE_RATE * LEFT_FREQ_HZ_RECIP);
            if (leftChannelCtr_ >= numSamplesPerPeriod)
            {
                leftChannelCtr_ = 0;
            }
        
            if (codec2_fifo_free(outputLeftFifo) > 0)
            {
                int halfway = numSamplesPerPeriod >> 1;
                short val = leftChannelCtr_++ < halfway ? SINE_WAVE_AMPLITUDE : -SINE_WAVE_AMPLITUDE;
                codec2_fifo_write(outputLeftFifo, &val, 1);
            }
        }
    }
    
    if (currentMode_ == 3 || currentMode_ == 5)
    {
        for (int index = 0; index < SAMPLES_PER_TICK; index++)
        {
            int numSamplesPerPeriod = (int)(SAMPLE_RATE * RIGHT_FREQ_HZ_RECIP);
            if (rightChannelCtr_ >= numSamplesPerPeriod)
            {
                rightChannelCtr_ = 0;
            }
        
            if (codec2_fifo_free(outputRightFifo) > 0)
            {
                int halfway = numSamplesPerPeriod >> 1;
                short val = rightChannelCtr_++ < halfway ? SINE_WAVE_AMPLITUDE : -SINE_WAVE_AMPLITUDE;
                codec2_fifo_write(outputRightFifo, &val, 1);
            }
        }
    }
}

}

}
