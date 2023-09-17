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

#ifndef FUEL_GAUGE_TASK_H
#define FUEL_GAUGE_TASK_H

#include "task/DVTask.h"
#include "driver/BatteryMessage.h"
#include "driver/ButtonMessage.h"
#include "driver/LedArray.h"
#include "driver/LedMessage.h"

namespace ezdv
{

namespace ui
{

using namespace ezdv::task;

class FuelGaugeTask : public DVTask
{
public:
    FuelGaugeTask();
    virtual ~FuelGaugeTask();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;

private:
    enum { NUM_LEDS = 4 };
    
    struct ChargeIndicatorConfiguration
    {
        float minimumPercentage;
        float maximumPercentage;
        driver::SetLedStateMessage::LedLabel ledToLight;
    };
    
    int blinkStateCtr_;
    bool sentRequest_;

    // Button handling
    void onButtonLongPressedMessage_(DVTask* origin, driver::ButtonLongPressedMessage* message);

    // USB unplug detection
    void onButtonPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message);
    
    // Fuel gauge handling
    void onBatteryStateMessage_(DVTask* origin, driver::BatteryStateMessage* message);
    
    // Helper to enable correct LED.
    void lightIndicator_(float chargeLevel, ChargeIndicatorConfiguration* config);
    
    static ChargeIndicatorConfiguration IndicatorConfig_[];
};

}

}

#endif // FUEL_GAUGE_TASK_H
