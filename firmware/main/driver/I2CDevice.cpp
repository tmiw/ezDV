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

#include <cstring>
#include "driver/i2c.h"
#include "I2CDevice.h"

#define I2C_SDA_GPIO GPIO_NUM_47
#define I2C_SCL_GPIO GPIO_NUM_45
#define I2C_SCK_FREQ_HZ (100000)

namespace ezdv
{

namespace driver
{

I2CDevice::I2CDevice()
{
    i2cDeviceSemaphore_ = xSemaphoreCreateBinary();
    assert(i2cDeviceSemaphore_ != nullptr);

    i2c_config_t conf;
    memset(&conf, 0, sizeof(i2c_config_t));
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA_GPIO;
    conf.scl_io_num = I2C_SCL_GPIO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = I2C_SCK_FREQ_HZ;
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));

    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
}

I2CDevice::~I2CDevice()
{
    ESP_ERROR_CHECK(i2c_driver_delete(I2C_NUM_0));
    vSemaphoreDelete(i2cDeviceSemaphore_);
}

bool I2CDevice::writeBytes(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* val, uint8_t size)
{
    xSemaphoreTake(i2cDeviceSemaphore_, pdMS_TO_TICKS(100));

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress << 1) | I2C_MASTER_WRITE, I2C_MASTER_ACK);
    
    i2c_master_write_byte(cmd, registerAddress, I2C_MASTER_ACK);
    i2c_master_write(cmd, val, size, I2C_MASTER_ACK);
    i2c_master_stop(cmd);
    
    auto rv = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    xSemaphoreGive(i2cDeviceSemaphore_);
    
    return rv == ESP_OK;
}

bool I2CDevice::readBytes(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* buffer, uint8_t size)
{
    xSemaphoreTake(i2cDeviceSemaphore_, pdMS_TO_TICKS(100));

    uint8_t regBuf[] = { registerAddress };
    auto rv = i2c_master_write_read_device(I2C_NUM_0, i2cAddress, regBuf, 1, buffer, size, pdMS_TO_TICKS(1000));

    xSemaphoreGive(i2cDeviceSemaphore_);
    
    return rv == ESP_OK;
}

} // namespace driver

} // namespace ezdv
