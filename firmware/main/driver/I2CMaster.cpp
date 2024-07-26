/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2024 Mooneer Salem
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
#include "I2CMaster.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA_GPIO GPIO_NUM_47
#define I2C_SCL_GPIO GPIO_NUM_48
#define I2C_SCK_FREQ_HZ (400000)

namespace ezdv
{

namespace driver
{

I2CMaster::I2CMaster()
{
    i2c_master_bus_config_t masterConfig;
    memset(&masterConfig, 0, sizeof(i2c_master_bus_config_t));
    
    masterConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    masterConfig.i2c_port = I2C_PORT;
    masterConfig.scl_io_num = I2C_SCL_GPIO;
    masterConfig.sda_io_num = I2C_SDA_GPIO;
    masterConfig.glitch_ignore_cnt = 7;
    masterConfig.flags.enable_internal_pullup = true;

    ESP_ERROR_CHECK(i2c_new_master_bus(&masterConfig, &masterHandle_));
}

I2CMaster::~I2CMaster()
{
    ESP_ERROR_CHECK(i2c_del_master_bus(masterHandle_));
}

I2CMaster::I2CDevice* I2CMaster::getDevice(uint8_t i2cAddress)
{
    return new I2CDevice(this, i2cAddress);
}

I2CMaster::I2CDevice::I2CDevice(I2CMaster* master, uint8_t i2cAddress)
    : master_(master)
{
    i2c_device_config_t devCfg;
    memset(&devCfg, 0, sizeof(i2c_device_config_t));
    
    devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devCfg.device_address = i2cAddress;
    devCfg.scl_speed_hz = I2C_SCK_FREQ_HZ;

    ESP_ERROR_CHECK(i2c_master_bus_add_device(master_->masterHandle_, &devCfg, &devHandle_));
}

I2CMaster::I2CDevice::~I2CDevice()
{
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(devHandle_));
}

bool I2CMaster::I2CDevice::writeBytes(uint8_t registerAddress, uint8_t* val, uint8_t size)
{
    uint8_t* tmp = new uint8_t[size + 1];
    assert(tmp != nullptr);
    
    tmp[0] = registerAddress;
    memcpy(&tmp[1], val, size);
    
    bool result = i2c_master_transmit(devHandle_, tmp, size + 1, 1000) == ESP_OK;
    delete[] tmp;
    
    return result;
}

bool I2CMaster::I2CDevice::readBytes(uint8_t registerAddress, uint8_t* buffer, uint8_t size)
{
    uint8_t regBuf[] = { registerAddress };
    return i2c_master_transmit_receive(devHandle_, regBuf, 1, buffer, size, 1000) == ESP_OK;
}

} // namespace driver

} // namespace ezdv
