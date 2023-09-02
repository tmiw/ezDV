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
#include "driver/LedMessage.h"

#define CURRENT_LOG_TAG ("FuelGaugeTask")

// Defined in Application.cpp.
extern void StartSleeping();
extern "C" bool rebootDevice;

namespace ezdv
{

namespace ui
{

FuelGaugeTask::FuelGaugeTask()
    : DVTask("FuelGaugeTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 32, pdMS_TO_TICKS(1000))
    , blinkStateCtr_(0)
{
    registerMessageHandler(this, &FuelGaugeTask::onButtonLongPressedMessage_);
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

void FuelGaugeTask::onTaskWake_()
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

void FuelGaugeTask::onTaskTick_()
{
    // Request current battery state once a second to ensure we're still charging.
    driver::RequestBatteryStateMessage message;
    publish(&message);
}

void FuelGaugeTask::onBatteryStateMessage_(DVTask* origin, driver::BatteryStateMessage* message)
{
    if (message->socChangeRate < 0)
    {
        // We're likely unplugged, go back to sleep
        rebootDevice = false;
        StartSleeping();
    }

    driver::SetLedStateMessage ledStateMessage;
    ledStateMessage.led = driver::SetLedStateMessage::LedLabel::NETWORK;
    if (message->soc >= 25)
    {
        ledStateMessage.ledState = true;
    }
    else
    {
        ledStateMessage.ledState = (blinkStateCtr_++) & 0x1;
    }
    publish(&ledStateMessage);

    ledStateMessage.led = driver::SetLedStateMessage::LedLabel::SYNC;
    if (message->soc >= 50)
    {
        ledStateMessage.ledState = true;
    }
    else if (message->soc > 25 && message->soc < 50)
    {
        ledStateMessage.ledState = (blinkStateCtr_++) & 0x1;
    }
    else
    {
        ledStateMessage.ledState = false;
    }
    publish(&ledStateMessage);

    ledStateMessage.led = driver::SetLedStateMessage::LedLabel::OVERLOAD;
    if (message->soc >= 75)
    {
        ledStateMessage.ledState = true;
    }
    else if (message->soc > 50 && message->soc < 75)
    {
        ledStateMessage.ledState = (blinkStateCtr_++) & 0x1;
    }
    else
    {
        ledStateMessage.ledState = false;
    }
    publish(&ledStateMessage);

    ledStateMessage.led = driver::SetLedStateMessage::LedLabel::PTT;
    if (message->soc >= 100)
    {
        ledStateMessage.ledState = true;
    }
    else if (message->soc > 75 && message->soc < 100)
    {
        ledStateMessage.ledState = (blinkStateCtr_++) & 0x1;
    }
    else
    {
        ledStateMessage.ledState = false;
    }
    publish(&ledStateMessage);
}

}

}
