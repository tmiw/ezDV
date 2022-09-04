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

OutputGPIO::OutputGPIO(gpio_num_t gpio)
    : gpio_(gpio)
{
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_));
    ESP_ERROR_CHECK(gpio_set_direction(gpio_, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(gpio_, GPIO_FLOATING));
}

OutputGPIO::~OutputGPIO()
{
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_));
}

void OutputGPIO::setState(bool state)
{
    ESP_ERROR_CHECK(gpio_set_level(gpio_, state));
}

} // namespace driver

} // namespace ezdv