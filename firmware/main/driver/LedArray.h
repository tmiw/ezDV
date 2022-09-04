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

#ifndef LED_ARRAY_H
#define LED_ARRAY_H

#include "task/DVTask.h"
#include "LedMessage.h"
#include "OutputGPIO.h"

#define GPIO_SYNC_LED GPIO_NUM_1
#define GPIO_OVL_LED GPIO_NUM_2
#define GPIO_PTT_LED GPIO_NUM_41
#define GPIO_PTT_NPN GPIO_NUM_21 /* bridges GND and PTT together at the 3.5mm jack */
#define GPIO_NET_LED GPIO_NUM_42

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

class LedArray : public DVTask
{
public:
    LedArray();
    virtual ~LedArray();

protected:
    virtual void onTaskStart_(DVTask* origin, TaskStartMessage* message) override;
    virtual void onTaskWake_(DVTask* origin, TaskWakeMessage* message) override;
    virtual void onTaskSleep_(DVTask* origin, TaskSleepMessage* message) override;

private:
    OutputGPIO syncLed_;
    OutputGPIO overloadLed_;
    OutputGPIO pttLed_;
    OutputGPIO pttNpmLed_;
    OutputGPIO networkLed_;

    void onSetLedState_(DVTask* origin, SetLedStateMessage* message);
};

}

}

#endif // LED_ARRAY_H