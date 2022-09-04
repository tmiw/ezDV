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

#ifndef EZDV_APPLICATION_H
#define EZDV_APPLICATION_H

#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "driver/ButtonArray.h"
#include "driver/ButtonMessage.h"
#include "driver/I2CDevice.h"
#include "driver/LedArray.h"
#include "driver/TLV320.h"
#include "storage/SettingsTask.h"

using namespace ezdv::task;

namespace ezdv
{

class App : public DVTask
{
public:
    App();

protected:
    virtual void onTaskStart_(DVTask* origin, TaskStartMessage* message) override;
    virtual void onTaskWake_(DVTask* origin, TaskWakeMessage* message) override;
    virtual void onTaskSleep_(DVTask* origin, TaskSleepMessage* message) override;
    
private:
    DVTimer timer_;
    driver::ButtonArray buttonArray_;
    driver::I2CDevice i2cDevice_;
    driver::LedArray ledArray_;
    driver::TLV320 tlv320Device_;
    storage::SettingsTask settingsTask_;

    void onLongButtonPressed_(DVTask* origin, driver::ButtonLongPressedMessage* message);
};

}

#endif // EZDV_APPLICATION_H