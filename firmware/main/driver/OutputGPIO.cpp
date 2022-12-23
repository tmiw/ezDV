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

#include "OutputGPIO.h"

namespace ezdv
{

namespace driver
{

OutputGPIO::OutputGPIO(gpio_num_t gpio, bool pwm)
    : gpio_(gpio)
    , pwm_(pwm)
{
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_));
    ESP_ERROR_CHECK(gpio_set_direction(gpio_, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(gpio_, GPIO_FLOATING));

    if (pwm)
    {
        auto pwmChannel = getPWMChannel_();

        ledc_timer_config_t ledc_timer = {
            .speed_mode       = LEDC_LOW_SPEED_MODE,
            .duty_resolution  = LEDC_TIMER_13_BIT,
            .timer_num        = LEDC_TIMER_0,
            .freq_hz          = 5000,  // Set output frequency at 5 kHz
            .clk_cfg          = LEDC_AUTO_CLK
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        // Prepare and then apply the LEDC PWM channel configuration
        ledc_channel_config_t ledc_channel = {
            .gpio_num       = gpio_,
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = pwmChannel,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER_0,
            .duty           = 0, // Set duty to 0%
            .hpoint         = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }
}

OutputGPIO::~OutputGPIO()
{
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_));
}

void OutputGPIO::setState(bool state)
{
    if (pwm_)
    {
        auto pwmChannel = getPWMChannel_();

        // 819 = 10% duty cycle (((2 ** 13) - 1) * 10%)
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, pwmChannel, state ? 819 : 0));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, pwmChannel));
    }
    else
    {
        ESP_ERROR_CHECK(gpio_set_level(gpio_, state));
    }
}

ledc_channel_t OutputGPIO::getPWMChannel_()
{
    ledc_channel_t pwmChannel;

    switch(gpio_)
    {
        case GPIO_NUM_1:
            pwmChannel = LEDC_CHANNEL_0;
            break;
        case GPIO_NUM_2:
            pwmChannel = LEDC_CHANNEL_1;
            break;
        case GPIO_NUM_15:
            pwmChannel = LEDC_CHANNEL_2;
            break;
        case GPIO_NUM_16:
            pwmChannel = LEDC_CHANNEL_3;
            break;
        default:
            assert(0);
            break;
    }

    return pwmChannel;
}

} // namespace driver

} // namespace ezdv