// Simple ULP RISC-V application to monitor the GPIO corresponding to the
// Mode button. If it's pressed for >= 1 second, we trigger a wakeup of 
// the main processor.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_riscv_gpio.h"

volatile int num_cycles_with_gpio_on;

#define TURN_ON_GPIO_NUM GPIO_NUM_5
#define MIN_NUM_CYCLES (100) /* found by experimentation to take 1s */

int main (void)
{
    ulp_riscv_gpio_init(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_input_enable(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_output_disable(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_pullup(TURN_ON_GPIO_NUM);
    ulp_riscv_gpio_pulldown_disable(TURN_ON_GPIO_NUM);

    if (!ulp_riscv_gpio_get_level(TURN_ON_GPIO_NUM))
    {
        num_cycles_with_gpio_on++;
        if (num_cycles_with_gpio_on >= MIN_NUM_CYCLES)
        {
            ulp_riscv_wakeup_main_processor();
        }
    }
    else
    {
        num_cycles_with_gpio_on = 0;
    }

    /* ulp_riscv_halt() is called automatically when main exits */
    return 0;
}