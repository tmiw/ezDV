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

#include "FuelGaugeTask.h"

#define CURRENT_LOG_TAG ("FuelGaugeTask")

// Defined in Application.cpp.
extern void StartSleeping();
extern "C" bool rebootDevice;

namespace ezdv
{

namespace ui
{

FuelGaugeTask::ChargeIndicatorConfiguration FuelGaugeTask::IndicatorConfig_[] = {
    {0, 24, driver::SetLedStateMessage::LedLabel::NETWORK},
    {25, 49, driver::SetLedStateMessage::LedLabel::OVERLOAD},
    {50, 74, driver::SetLedStateMessage::LedLabel::SYNC},
    {75, 99, driver::SetLedStateMessage::LedLabel::PTT},
};

FuelGaugeTask::FuelGaugeTask()
    : DVTask("FuelGaugeTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 32, pdMS_TO_TICKS(1000))
    , blinkStateCtr_(0)
    , sentRequest_(false)
    , socChangeRate_(0)
{
    registerMessageHandler(this, &FuelGaugeTask::onButtonLongPressedMessage_);
    registerMessageHandler(this, &FuelGaugeTask::onButtonPressedMessage_);
    registerMessageHandler(this, &FuelGaugeTask::onBatteryStateMessage_);
}

FuelGaugeTask::~FuelGaugeTask()
{
    // empty
}

void FuelGaugeTask::onTaskStart_()
{
    // empty
}

void FuelGaugeTask::onTaskSleep_()
{
    // empty
}

void FuelGaugeTask::onButtonLongPressedMessage_(DVTask* origin, driver::ButtonLongPressedMessage* message)
{
    if (message->button == driver::ButtonLabel::MODE)
    {
        // Long press Mode button triggers reboot into normal mode
        rebootDevice = true;
        StartSleeping();
    }
}

void FuelGaugeTask::onButtonPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message)
{
    if (message->button == driver::ButtonLabel::USB_POWER_DETECT)
    {
        // We're unplugged, go back to sleep
        rebootDevice = false;
        StartSleeping();
    }
}

void FuelGaugeTask::onTaskTick_()
{
    // Request current battery state once a second to ensure we're still charging.
    sentRequest_ = true;
    driver::RequestBatteryStateMessage message;
    publish(&message);
}

void FuelGaugeTask::onBatteryStateMessage_(DVTask* origin, driver::BatteryStateMessage* message)
{
    // Ignore the unsolicited status messages to ensure that there aren't any glitches
    // in the blinking.
    if (!sentRequest_)
    {
        return;
    }
    sentRequest_ = false;

    // Store off charging rate so we don't unnecessarily blink when charging
    // is complete.
    socChangeRate_ = message->socChangeRate;

    for (int index = 0; index < NUM_LEDS; index++)
    {
        lightIndicator_(message->soc, &IndicatorConfig_[index]);
    }

    blinkStateCtr_++;
}

void FuelGaugeTask::lightIndicator_(float chargeLevel, ChargeIndicatorConfiguration* config)
{
    driver::SetLedStateMessage ledStateMessage;
    ledStateMessage.led = config->ledToLight;
    if (chargeLevel < config->minimumPercentage)
    {
        ledStateMessage.ledState = false;
    }
    else if (socChangeRate_ > 0 && 
             chargeLevel >= config->minimumPercentage && chargeLevel <= config->maximumPercentage)
    {
        ledStateMessage.ledState = (blinkStateCtr_) & 0x1;
    }
    else
    {
        ledStateMessage.ledState = true;
    }
    publish(&ledStateMessage);
}

}

}
