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

#define LED_PWM_FREQUENCY (160000) /* 160 KHz */
#define LED_DUTY_RESOLUTION LEDC_TIMER_8_BIT
#define LED_DUTY_CYCLE_SHIFT (5) /* relative to 5 Khz frequency, i.e. add 1 for every doubling of frequency after 5 KHz.
                                    This is so we can translate e.g. 8192 to the maximum for the current duty resolution. */

namespace ezdv
{

namespace driver
{

bool OutputGPIO::FadeFnInitialized_ = false;

OutputGPIO::OutputGPIO(gpio_num_t gpio, bool pwm, bool fade)
    : gpio_(gpio)
    , pwm_(pwm)
    , fade_(fade)
    , dutyCycle_(8192)
    , state_(false)
{
    if (!FadeFnInitialized_)
    {
        FadeFnInitialized_ = true;
        ESP_ERROR_CHECK(ledc_fade_func_install(0));
    }
    
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_));
    ESP_ERROR_CHECK(gpio_set_direction(gpio_, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(gpio_, GPIO_FLOATING));
    
    // Needed to reduce EMI on radio aduio in HW test mode.
    ESP_ERROR_CHECK(gpio_set_drive_capability(gpio_, GPIO_DRIVE_CAP_0));

    if (pwm)
    {
        auto pwmChannel = getPWMChannel_();

        ledc_timer_config_t ledc_timer = {
            .speed_mode       = LEDC_LOW_SPEED_MODE,
            .duty_resolution  = LED_DUTY_RESOLUTION,
            .timer_num        = LEDC_TIMER_0,
            .freq_hz          = LED_PWM_FREQUENCY,
            .clk_cfg          = LEDC_AUTO_CLK,
            .deconfigure      = false
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        // Prepare and then apply the LEDC PWM channel configuration
        ledc_channel_config_t ledc_channel = {
            .gpio_num       = gpio_,
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = pwmChannel,
            .intr_type      = fade_ ? LEDC_INTR_FADE_END : LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER_0,
            .duty           = 0, // Set duty to 0%
            .hpoint         = 0,
            .flags          = {
                .output_invert = 0
            }
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }
}

OutputGPIO::~OutputGPIO()
{
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_));
}

void OutputGPIO::setDutyCycle(int dutyCycle)
{
    dutyCycle_ = dutyCycle;
    
    // Update duty cycle in the PWM driver.
    updateDutyCycle_();
}

void OutputGPIO::setState(bool state)
{
    if (state != state_)
    {
        state_ = state;
        updateDutyCycle_();
    }
}

void OutputGPIO::updateDutyCycle_()
{
    if (pwm_)
    {
        auto pwmChannel = getPWMChannel_();
    
        if (fade_)
        {
            // Fade to the new state within 20ms, wait till done (EMI mitigation).
            // NOTE: only the PTT output GPIO is fading at the moment but we should consider 
            // blocking wait in the future if others need it.
            ESP_ERROR_CHECK(ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, pwmChannel, state_ ? dutyCycle_ >> LED_DUTY_CYCLE_SHIFT: 0, 20, LEDC_FADE_NO_WAIT));
        }
        else
        {
            // 819 = 10% duty cycle (((2 ** 13) - 1) * 10%)
            ESP_ERROR_CHECK(ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, pwmChannel, state_ ? dutyCycle_ >> LED_DUTY_CYCLE_SHIFT : 0, 0));
        }
    }
    else
    {
        ESP_ERROR_CHECK(gpio_set_level(gpio_, state_));
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
        case GPIO_NUM_21:
            pwmChannel = LEDC_CHANNEL_4;
            break;
        default:
            assert(0);
            break;
    }

    return pwmChannel;
}

} // namespace driver

} // namespace ezdv