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

#include "ButtonArray.h"
#include "InputGPIO.h"

// One second for long press
#define LONG_PRESS_INTERVAL_US (1000000)

namespace ezdv
{

namespace driver
{

using namespace std::placeholders;

ButtonArray::ButtonArray()
    : DVTask("ButtonArray", 10 /* TBD */, 3072, tskNO_AFFINITY, 10)
    , pttButtonTimer_(this, std::bind(&ButtonArray::handleLongPressButton_, this, ButtonLabel::PTT), LONG_PRESS_INTERVAL_US)
    , modeButtonTimer_(this, std::bind(&ButtonArray::handleLongPressButton_, this, ButtonLabel::MODE), LONG_PRESS_INTERVAL_US)
    , volUpButtonTimer_(this, std::bind(&ButtonArray::handleLongPressButton_, this, ButtonLabel::VOL_UP), LONG_PRESS_INTERVAL_US)
    , volDownButtonTimer_(this, std::bind(&ButtonArray::handleLongPressButton_, this, ButtonLabel::VOL_DOWN), LONG_PRESS_INTERVAL_US)
    , pttButton_(this, std::bind(&ButtonArray::handleButton_, this, ButtonLabel::PTT, _2))
    , modeButton_(this, std::bind(&ButtonArray::handleButton_, this, ButtonLabel::MODE, _2))
    , volUpButton_(this, std::bind(&ButtonArray::handleButton_, this, ButtonLabel::VOL_UP, _2))
    , volDownButton_(this, std::bind(&ButtonArray::handleButton_, this, ButtonLabel::VOL_DOWN, _2))
    , usbPower_(this, std::bind(&ButtonArray::handleButton_, this, ButtonLabel::USB_POWER_DETECT, _2), false, false)
{
    // empty
}

ButtonArray::~ButtonArray()
{
    // empty
}

void ButtonArray::onTaskStart_()
{
    pttButton_.start();
    pttButton_.enableInterrupt(true);
    
    modeButton_.start();
    modeButton_.enableInterrupt(true);
    
    volUpButton_.start();
    volUpButton_.enableInterrupt(true);
    
    volDownButton_.start();
    volDownButton_.enableInterrupt(true);
    
    usbPower_.start();
    usbPower_.enableInterrupt(true);
}

void ButtonArray::onTaskWake_()
{
    // Use same actions as start.
    onTaskStart_();
}

void ButtonArray::onTaskSleep_()
{
    pttButton_.enableInterrupt(false);
    modeButton_.enableInterrupt(false);
    volUpButton_.enableInterrupt(false);
    volDownButton_.enableInterrupt(false);
}

void ButtonArray::handleButton_(ButtonLabel label, bool val)
{
    DVTimer* timerToSet = nullptr;
    bool buttonPressed = !val;

    char* buttonName = "";

    switch(label)
    {
        case ButtonLabel::PTT:
            buttonName = "PTT";
            timerToSet = &pttButtonTimer_;
            break;
        case ButtonLabel::MODE:
            buttonName = "Mode";
            timerToSet = &modeButtonTimer_;
            break;
        case ButtonLabel::VOL_UP:
            buttonName = "VolUp";
            timerToSet = &volUpButtonTimer_;
            break;
        case ButtonLabel::VOL_DOWN:
            buttonName = "VolDown";
            timerToSet = &volDownButtonTimer_;
            break;
        case ButtonLabel::USB_POWER_DETECT:
            buttonName = "UsbPower";
            break;
        default:
            assert(0);
    }

    ESP_LOGI("ButtonArray", "Button %s now %d", buttonName, (int)buttonPressed);

    // Start long press timer
    if (buttonPressed)
    {
        ButtonShortPressedMessage* message = new ButtonShortPressedMessage(label);
        publish(message);
        delete message;

        if (timerToSet != nullptr)
        {
            timerToSet->stop();
            timerToSet->start(true);
        }
    }
    else
    {
        if (timerToSet != nullptr)
        {
            timerToSet->stop();
        }
        
        ButtonReleasedMessage* message = new ButtonReleasedMessage(label);
        publish(message);
        delete message;
    }
}

void ButtonArray::handleLongPressButton_(ButtonLabel label)
{
    char* buttonName = "";

    switch(label)
    {
        case ButtonLabel::PTT:
            buttonName = "PTT";
            break;
        case ButtonLabel::MODE:
            buttonName = "Mode";
            break;
        case ButtonLabel::VOL_UP:
            buttonName = "VolUp";
            break;
        case ButtonLabel::VOL_DOWN:
            buttonName = "VolDown";
            break;
        default:
            assert(0);
    }

    ESP_LOGI("ButtonArray", "Button %s long pressed", buttonName);

    ButtonLongPressedMessage* message = new ButtonLongPressedMessage(label);
    publish(message);
    delete message;
}

}

}