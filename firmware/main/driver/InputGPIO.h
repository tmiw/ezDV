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
#include "driver/gpio_filter.h"

#include "task/DVTask.h"
#include "task/DVTimer.h"

#define DEBOUNCE_TIMER_US (20000)

extern "C"
{
    DV_EVENT_DECLARE_BASE(INPUT_GPIO_MESSAGE);
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

    InputGPIO(DVTask* owner, GPIOChangeFn onChange, bool enablePullup = true, bool enablePulldown = false);
    virtual ~InputGPIO();
    
    void start();

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

    DVTimer debounceTimer_;
    DVTask* owner_;
    GPIOChangeFn onStateChange_;
    bool interruptEnabled_;
    bool currentState_;
    bool enablePullup_;
    bool enablePulldown_;
    gpio_glitch_filter_handle_t glitchFilterHandle_;

    void onGPIOStateChange_(DVTask* origin, InterruptFireMessage* message);
    void onDebounceTimerFire_();

    static void OnGPIOInterrupt_(void* ptr);
};

template<gpio_num_t NumGPIO>
InputGPIO<NumGPIO>::InputGPIO(DVTask* owner, GPIOChangeFn onChange, bool enablePullup, bool enablePulldown)
    : debounceTimer_(owner, std::bind(&InputGPIO<NumGPIO>::onDebounceTimerFire_, this), DEBOUNCE_TIMER_US)
    , owner_(owner)
    , onStateChange_(onChange)
    , interruptEnabled_(false)
    , currentState_(false)
    , enablePullup_(enablePullup)
    , enablePulldown_(enablePulldown)
    , glitchFilterHandle_(nullptr)
{
    // empty
}

template<gpio_num_t NumGPIO>
InputGPIO<NumGPIO>::~InputGPIO()
{
    enableInterrupt(false);

    // TBD: deregister handler
    assert(0);
}

template<gpio_num_t NumGPIO>
void InputGPIO<NumGPIO>::start()
{
    ESP_ERROR_CHECK(gpio_reset_pin(NumGPIO));
    ESP_ERROR_CHECK(gpio_set_direction(NumGPIO, GPIO_MODE_INPUT));

    if (!enablePullup_ && !enablePulldown_)
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(NumGPIO, GPIO_FLOATING));
        ESP_ERROR_CHECK(gpio_pulldown_dis(NumGPIO));
        ESP_ERROR_CHECK(gpio_pullup_dis(NumGPIO));
    }
    else if (enablePullup_)
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(NumGPIO, GPIO_PULLUP_ONLY));
        ESP_ERROR_CHECK(gpio_pulldown_dis(NumGPIO));
        ESP_ERROR_CHECK(gpio_pullup_en(NumGPIO));
    }
    else if (enablePulldown_)
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(NumGPIO, GPIO_PULLDOWN_ONLY));
        ESP_ERROR_CHECK(gpio_pulldown_en(NumGPIO));
        ESP_ERROR_CHECK(gpio_pullup_dis(NumGPIO));
    }
    enableInterrupt(false);
    
    gpio_pin_glitch_filter_config_t glitchConfig = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = NumGPIO
    };
    ESP_ERROR_CHECK(gpio_new_pin_glitch_filter(&glitchConfig, &glitchFilterHandle_));
    ESP_ERROR_CHECK(gpio_glitch_filter_enable(glitchFilterHandle_));
    
    currentState_ = gpio_get_level(NumGPIO) == 1;

    owner_->registerMessageHandler(this, &InputGPIO<NumGPIO>::onGPIOStateChange_);
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
    // Start debounce timer.
    debounceTimer_.start(true);
}

template<gpio_num_t NumGPIO>
void InputGPIO<NumGPIO>::onDebounceTimerFire_()
{
    // Re-enable interrupt handler so we can restart debounce next button press.
    enableInterrupt(true);
    
    auto pendingCurrent = getCurrentValue();
    if (pendingCurrent != currentState_)
    {
        // Suppress callbacks if there hasn't actually been a change in value.
        currentState_ = pendingCurrent;
        onStateChange_(this, currentState_);
    }
}

template<gpio_num_t NumGPIO>
void InputGPIO<NumGPIO>::OnGPIOInterrupt_(void* ptr)
{
    InputGPIO* gpioObj = (InputGPIO*)ptr;
    if (gpioObj->interruptEnabled_)
    {
        // Temporarily disable the interrupt until after debounce checking finishes.
        gpioObj->enableInterrupt(false);
        
        // Begin debounce logic inside non-interrupt context.
        InterruptFireMessage message;
        gpioObj->owner_->postISR(&message);
    }
}

} // namespace driver

} // namespace ezdv

#endif // INPUT_GPIO_H