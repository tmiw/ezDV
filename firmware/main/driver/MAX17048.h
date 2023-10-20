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

#ifndef MAX17048_H
#define MAX17048_H

#include <inttypes.h>

#include "driver/temperature_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "BatteryMessage.h"
#include "task/DVTask.h"
#include "I2CDevice.h"
#include "InputGPIO.h"

// Interrupt GPIO for battery alerts
#define BAT_ALERT_GPIO GPIO_NUM_8

namespace ezdv
{

namespace driver
{

class MAX17048 : public DVTask
{
public:
    MAX17048(I2CDevice* i2cDevice);
    virtual ~MAX17048() = default;

    bool isLowSOC() const { return isLowSoc_; }
    void suppressForcedSleep(bool val) { suppressForcedSleep_ = val; }
    
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;
    
private:
    I2CDevice* i2cDevice_;
    InputGPIO<BAT_ALERT_GPIO> batAlertGpio_;
    bool enabled_;
    adc_oneshot_unit_handle_t adcHandle_;
    adc_cali_handle_t adcCalibrationHandle_;
    bool isLowSoc_;
    bool isStarting_;
    bool suppressForcedSleep_;
    
    bool writeInt16Reg_(uint8_t reg, uint16_t val);
    bool readInt16Reg_(uint8_t reg, uint16_t* val);
    
    bool deviceExists_();
    bool needsConfiguration_();
    void configureDevice_();
    
    void onInterrupt_(bool val);
    
    float temperatureFromADC_();

    void onLowBatteryShutdownMessage_(DVTask* origin, LowBatteryShutdownMessage* message);
    void onRequestBatteryStateMessage_(DVTask* origin, RequestBatteryStateMessage* message);
};
    
}

}

#endif // MAX17048_H