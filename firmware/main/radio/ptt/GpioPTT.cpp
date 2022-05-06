#include "GpioPTT.h"

#include "driver/gpio.h"

#define PTT_OUT_GPIO GPIO_NUM_21
#define PTT_OUT_GUI_GPIO GPIO_NUM_41

namespace sm1000neo::radio::ptt
{

GpioPTT::GpioPTT()
    : GenericPTT()
{
    // empty
}

void GpioPTT::init()
{
    // Configure PTT output GPIOs on task start.
    gpio_intr_disable(PTT_OUT_GPIO);
    gpio_set_direction(PTT_OUT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(PTT_OUT_GPIO, GPIO_FLOATING);
    gpio_set_level(PTT_OUT_GPIO, 0);
    
    gpio_intr_disable(PTT_OUT_GUI_GPIO);
    gpio_set_direction(PTT_OUT_GUI_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(PTT_OUT_GUI_GPIO, GPIO_FLOATING);
    gpio_set_level(PTT_OUT_GUI_GPIO, 0);
}

void GpioPTT::setPTT_(bool pttState)
{
    // Set PTT state based on 
    gpio_set_level(PTT_OUT_GPIO, pttState ? 1 : 0);
    gpio_set_level(PTT_OUT_GUI_GPIO, pttState ? 1 : 0);
}

}