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

#include "LedArray.h"

#define GPIO_SYNC_LED GPIO_NUM_1
#define GPIO_OVL_LED GPIO_NUM_2
#define GPIO_PTT_LED GPIO_NUM_15
#define GPIO_PTT_NPN GPIO_NUM_21 /* bridges GND and PTT together at the 3.5mm jack */
#define GPIO_NET_LED GPIO_NUM_16

#define CURRENT_LOG_TAG ("LedArray")

namespace ezdv
{

namespace driver
{

LedArray::LedArray()
    : DVTask("LedArray", 10 /* TBD */, 4096, tskNO_AFFINITY, 10)
    , syncLed_(GPIO_SYNC_LED, true)
    , overloadLed_(GPIO_OVL_LED, true)
    , pttLed_(GPIO_PTT_LED, true)
    , pttNpmLed_(GPIO_PTT_NPN, true, true)
    , networkLed_(GPIO_NET_LED, true)
{
    registerMessageHandler(this, &LedArray::onSetLedState_);
    registerMessageHandler(this, &LedArray::onLedBrightnessSettingsMessage_);
}

LedArray::~LedArray()
{
    assert(0);
}

void LedArray::onTaskStart_()
{
    // empty
}

void LedArray::onTaskSleep_()
{
    // empty
}

void LedArray::onSetLedState_(DVTask* origin, SetLedStateMessage* message)
{
    //ESP_LOGI(CURRENT_LOG_TAG, "LED %d now %d", (int)message->led, message->ledState);
    
    switch(message->led)
    {
        case SetLedStateMessage::LedLabel::NETWORK:
            networkLed_.setState(message->ledState);
            break;
        case SetLedStateMessage::LedLabel::OVERLOAD:
            overloadLed_.setState(message->ledState);
            break;
        case SetLedStateMessage::LedLabel::PTT:
            pttLed_.setState(message->ledState);
            break;
        case SetLedStateMessage::LedLabel::PTT_NPN:
            pttNpmLed_.setState(message->ledState);
            break;
        case SetLedStateMessage::LedLabel::SYNC:
            syncLed_.setState(message->ledState);
            break;
        default:
            assert(0);
    }
}

void LedArray::onLedBrightnessSettingsMessage_(DVTask* origin, storage::LedBrightnessSettingsMessage* message)
{
    networkLed_.setDutyCycle(message->dutyCycle);
    overloadLed_.setDutyCycle(message->dutyCycle);
    pttLed_.setDutyCycle(message->dutyCycle);
    syncLed_.setDutyCycle(message->dutyCycle);
}

}

}