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

#ifndef INPUT_GPIO_H
#define INPUT_GPIO_H

#include "esp_log.h"
#include "driver/gpio.h"

#include "task/DVTask.h"

extern "C"
{
    ESP_EVENT_DECLARE_BASE(INPUT_GPIO_MESSAGE);
}

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

template<gpio_num_t NumGPIO>
class InputGPIO
{
public:
    using GPIOChangeFn = std::function<void(InputGPIO<NumGPIO>*, bool)>;

    InputGPIO(DVTask* owner, GPIOChangeFn onChange);
    virtual ~InputGPIO();

    void enableInterrupt(bool enable);
    bool getCurrentValue();

private:
    class InterruptFireMessage : public DVTaskMessageBase<NumGPIO, InterruptFireMessage>
    {
    public:
        InterruptFireMessage()
            : DVTaskMessageBase<NumGPIO, InterruptFireMessage>(INPUT_GPIO_MESSAGE)
            {}
        virtual ~InterruptFireMessage() = default;
    };

    DVTask* owner_;
    GPIOChangeFn onStateChange_;
    bool interruptEnabled_;

    void onGPIOStateChange_(DVTask* origin, InterruptFireMessage* message);

    static void OnGPIOInterrupt_(void* ptr);
};

template<gpio_num_t NumGPIO>
InputGPIO<NumGPIO>::InputGPIO(DVTask* owner, GPIOChangeFn onChange)
    : owner_(owner)
    , onStateChange_(onChange)
    , interruptEnabled_(false)
{
    ESP_ERROR_CHECK(gpio_reset_pin(NumGPIO));
    ESP_ERROR_CHECK(gpio_set_direction(NumGPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(NumGPIO, GPIO_FLOATING));

    enableInterrupt(false);

    owner_->registerMessageHandler(this, &InputGPIO<NumGPIO>::onGPIOStateChange_);
}

template<gpio_num_t NumGPIO>
InputGPIO<NumGPIO>::~InputGPIO()
{
    enableInterrupt(false);

    // TBD: deregister handler
    assert(0);
}

template<gpio_num_t NumGPIO>
void InputGPIO<NumGPIO>::enableInterrupt(bool enable)
{
    if (enable)
    {
        ESP_ERROR_CHECK(gpio_set_intr_type(NumGPIO, GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(NumGPIO, &OnGPIOInterrupt_, this));
        ESP_ERROR_CHECK(gpio_intr_enable(NumGPIO));
        interruptEnabled_ = true;
    }
    else
    {
        interruptEnabled_ = false;
        ESP_ERROR_CHECK(gpio_isr_handler_remove(NumGPIO));
        ESP_ERROR_CHECK(gpio_intr_disable(NumGPIO));
    }
}

template<gpio_num_t NumGPIO>
bool InputGPIO<NumGPIO>::getCurrentValue()
{
    return gpio_get_level(NumGPIO) == 1;
}

template<gpio_num_t NumGPIO>
void InputGPIO<NumGPIO>::onGPIOStateChange_(DVTask* origin, InterruptFireMessage* message)
{
    onStateChange_(this, getCurrentValue());
}

template<gpio_num_t NumGPIO>
void InputGPIO<NumGPIO>::OnGPIOInterrupt_(void* ptr)
{
    InputGPIO* gpioObj = (InputGPIO*)ptr;
    if (gpioObj->interruptEnabled_)
    {
        InterruptFireMessage message;
        gpioObj->owner_->postISR(&message);
    }
}

} // namespace driver

} // namespace ezdv

#endif // INPUT_GPIO_H