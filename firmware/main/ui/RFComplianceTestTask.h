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

#ifndef RF_COMPLIANCE_TEST_TASK_H
#define RF_COMPLIANCE_TEST_TASK_H

#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "audio/AudioInput.h"
#include "driver/BatteryMessage.h"
#include "driver/ButtonMessage.h"
#include "driver/TLV320Message.h"
#include "storage/SettingsMessage.h"
#include "driver/LedArray.h"
#include "driver/TLV320.h"

namespace ezdv
{

namespace ui
{

using namespace ezdv::task;

class RfComplianceTestTask : public DVTask, public audio::AudioInput
{
public:
    RfComplianceTestTask(ezdv::driver::LedArray* ledArrayTask, ezdv::driver::TLV320* tlv320Task);
    virtual ~RfComplianceTestTask();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;
    
    virtual void onTaskTick_() override;

private:
    bool isActive_;
    ezdv::driver::LedArray* ledArrayTask_;
    ezdv::driver::TLV320* tlv320Task_;
    int leftChannelCtr_;
    int rightChannelCtr_;
    bool pttGpio_;

    // Button handling
    void onButtonShortPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message);
    void onButtonLongPressedMessage_(DVTask* origin, driver::ButtonLongPressedMessage* message);
    void onButtonReleasedMessage_(DVTask* origin, driver::ButtonReleasedMessage* message);
};

}

}

#endif // RF_COMPLIANCE_TEST_TASK_H