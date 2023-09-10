// Simple ULP RISC-V application to monitor the GPIO corresponding to the
// Mode button. If it's pressed for >= 1 second, we trigger a wakeup of 
// the main processor.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_riscv_gpio.h"

volatile int num_cycles_with_gpio_on = 0;
volatile int num_cycles_with_usb_gpio_on = 0;

// Power-up modes:
// 0: normal bootup
// 1: temperature check bootup
// 2: fuel gauge mode
volatile int power_up_mode = 0;

volatile int num_cycles_between_temp_checks = 0;

#define TURN_ON_GPIO_NUM GPIO_NUM_5
#define MIN_NUM_CYCLES_BOOTUP (10) /* based on wakeup period (100ms), 10 cycles should result in 1s required for wakeup. */
#define MIN_NUM_CYCLES_TEMP_CHECK (36000) /* Set to 60min (36000 cycles) */

// Settings to allow detection of charging before booting
// to fuel gauge mode.
#define FUEL_GAUGE_INTERRUPT_GPIO_NUM 0

int main (void)
{
    ulp_riscv_gpio_init(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_input_enable(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_output_disable(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_pullup(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_pulldown_disable(TURN_ON_GPIO_NUM);

    ulp_riscv_gpio_init(FUEL_GAUGE_INTERRUPT_GPIO_NUM);
    ulp_riscv_gpio_input_enable(FUEL_GAUGE_INTERRUPT_GPIO_NUM);
    ulp_riscv_gpio_output_disable(FUEL_GAUGE_INTERRUPT_GPIO_NUM);
    ulp_riscv_gpio_pullup_disable(FUEL_GAUGE_INTERRUPT_GPIO_NUM);
    ulp_riscv_gpio_pulldown_disable(FUEL_GAUGE_INTERRUPT_GPIO_NUM);

    if (!ulp_riscv_gpio_get_level(TURN_ON_GPIO_NUM))
    {
        num_cycles_with_gpio_on++;
        if (num_cycles_with_gpio_on >= MIN_NUM_CYCLES_BOOTUP)
        {
            power_up_mode = 0;
            num_cycles_between_temp_checks = 0;
            ulp_riscv_wakeup_main_processor();
        }
    }
    else
    {
        num_cycles_with_gpio_on = 0;
    }

    // GPIO0 has a resistor divider on the 5V rail intended to help us determine
    // whether USB power exists. If this becomes 1 for more than 300ms (debounce),
    // we should boot into battery gauge mode.
    if (ulp_riscv_gpio_get_level(FUEL_GAUGE_INTERRUPT_GPIO_NUM))
    {
        num_cycles_with_usb_gpio_on++;
        if (num_cycles_with_usb_gpio_on >= 3)
        {
            power_up_mode = 2;
            num_cycles_between_temp_checks = 0;
            num_cycles_with_usb_gpio_on = 0;
            ulp_riscv_wakeup_main_processor();
        }
    }
    else
    {
        num_cycles_with_usb_gpio_on = 0;
        power_up_mode = 0;
    }

    /* 
        Normally, temperature checks occur every minute to ensure accurate
        battery fuel gauge calculations. However, to minimize power
        consumption, the thermistor only has power when ezDV is fully
        on. Thus, we have a separate check here to allow ezDV to briefly
        turn on every 60 minutes to save the current temperature to the
        fuel gauge chip. This forced power-up occurs infrequently enough
        (and ezDV powered up for short enough) that there should be little 
        impact on self-drain.
    */
    num_cycles_between_temp_checks++;
    if (num_cycles_between_temp_checks >= MIN_NUM_CYCLES_TEMP_CHECK)
    {
        num_cycles_between_temp_checks = 0;
        num_cycles_with_usb_gpio_on = 0;
        num_cycles_with_gpio_on = 0;
        power_up_mode = 1;
        ulp_riscv_wakeup_main_processor();
    }

    /* ulp_riscv_halt() is called automatically when main exits */
    return 0;
}