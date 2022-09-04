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

#ifndef BUTTON_ARRAY_H
#define BUTTON_ARRAY_H

#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "InputGPIO.h"
#include "ButtonMessage.h"

#define GPIO_PTT_BUTTON GPIO_NUM_4
#define GPIO_MODE_BUTTON GPIO_NUM_5 // PTT button doesn't work on v0.1 @ GPIO 4
#define GPIO_VOL_UP_BUTTON GPIO_NUM_6
#define GPIO_VOL_DOWN_BUTTON GPIO_NUM_7

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

class ButtonArray : public DVTask
{
public:
    ButtonArray();
    virtual ~ButtonArray();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;

private:
    DVTimer pttButtonTimer_;
    DVTimer modeButtonTimer_;
    DVTimer volUpButtonTimer_;
    DVTimer volDownButtonTimer_;

    InputGPIO<GPIO_PTT_BUTTON> pttButton_;
    InputGPIO<GPIO_MODE_BUTTON> modeButton_;
    InputGPIO<GPIO_VOL_UP_BUTTON> volUpButton_;
    InputGPIO<GPIO_VOL_DOWN_BUTTON> volDownButton_;

    void handleButton_(ButtonLabel label, bool val);
    void handleLongPressButton_(ButtonLabel label);
};

}

}

#endif // BUTTON_ARRAY_H