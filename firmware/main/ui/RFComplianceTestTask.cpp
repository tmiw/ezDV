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

#include "RfComplianceTestTask.h"
#include "driver/LedMessage.h"

#define CURRENT_LOG_TAG ("RfComplianceTestTask")

#define SAMPLE_RATE_RECIP 0.000125 /* 8000 Hz */
#define LEFT_FREQ_HZ 1000
#define RIGHT_FREQ_HZ 2000

// Defined in Application.cpp.
extern void StartSleeping();

namespace ezdv
{

namespace ui
{

RfComplianceTestTask::RfComplianceTestTask(ezdv::driver::LedArray* ledArrayTask, ezdv::driver::TLV320* tlv320Task)
    : DVTask("RfComplianceTestTask", 10 /* TBD */, 4096, tskNO_AFFINITY, pdMS_TO_TICKS(10))
    , AudioInput(1, 2)
    , isActive_(false)
    , ledArrayTask_(ledArrayTask)
    , tlv320Task_(tlv320Task)
    , leftChannelCtr_(0)
    , rightChannelCtr_(0)
    , pttGpio_(false)
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
}

void RfComplianceTestTask::onTaskWake_()
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
    storage::LeftChannelVolumeMessage leftChanVolMessage(48);
    storage::RightChannelVolumeMessage rightChanVolMessage(48);
    tlv320Task_->post(&leftChanVolMessage);
    tlv320Task_->post(&rightChanVolMessage);
}

void RfComplianceTestTask::onTaskSleep_()
{
    isActive_ = false;
}

void RfComplianceTestTask::onButtonShortPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message)
{
    // empty
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
    // empty
}

void RfComplianceTestTask::onTaskTick_()
{
    if (isActive_)
    {
        struct FIFO* outputLeftFifo = getAudioOutput(AudioInput::LEFT_CHANNEL);
        struct FIFO* outputRightFifo = getAudioOutput(AudioInput::RIGHT_CHANNEL);
    
        // 160 samples = 20ms @ 8 KHz sample rate
        for (int index = 0; index < 160; index++)
        {
            // 6048 is the amplitude required to have the sine wave appear at 0 dB in Audacity.
            short leftChannelVal = 32767 * sin(2 * M_PI * LEFT_FREQ_HZ * leftChannelCtr_++ * SAMPLE_RATE_RECIP);
            short rightChannelVal = 32767 * sin(2 * M_PI * RIGHT_FREQ_HZ * rightChannelCtr_++ * SAMPLE_RATE_RECIP);
        
            codec2_fifo_write(outputLeftFifo, &leftChannelVal, 1);
            codec2_fifo_write(outputRightFifo, &rightChannelVal, 1);
        }
        
        // Get some I2C traffic flowing.
        storage::LeftChannelVolumeMessage leftChanVolMessage(48);
        storage::RightChannelVolumeMessage rightChanVolMessage(48);
        tlv320Task_->post(&leftChanVolMessage);
        tlv320Task_->post(&rightChanVolMessage);
        
        // Toggle PTT line on radio jack
        pttGpio_ = !pttGpio_;
        ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::PTT_NPN, pttGpio_);
        publish(&msg);
    }
}

}

}