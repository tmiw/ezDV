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

#ifndef OUTPUT_GPIO_H
#define OUTPUT_GPIO_H

#include "driver/gpio.h"
#include "driver/ledc.h"

namespace ezdv
{

namespace driver
{

class OutputGPIO
{
public:
    OutputGPIO(gpio_num_t gpio, bool pwm = false);
    virtual ~OutputGPIO();
    
    void setState(bool state);
    void setDutyCycle(int dutyCycle);

private:
    gpio_num_t gpio_;
    bool pwm_;
    int dutyCycle_;
    bool state_;

    ledc_channel_t getPWMChannel_();
};

} // namespace driver

} // namespace ezdv

#endif // OUTPUT_GPIO_H