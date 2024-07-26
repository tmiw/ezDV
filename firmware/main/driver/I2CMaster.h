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

#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#include <inttypes.h>
#include "driver/i2c_master.h"

namespace ezdv
{

namespace driver
{

/// @brief Represents the I2C interface in the system.
class I2CMaster
{
public:
    /// @brief Represents a I2C device in the system.
    class I2CDevice
    {
    public:
        /// @brief Initializes the I2C device.
        I2CDevice(I2CMaster* master, uint8_t i2cAddress);
        
        /// @brief Deinitializes the I2C device.
        virtual ~I2CDevice();
        
        /// @brief Writes a register to the given I2C device.
        /// @param registerAddress The register on the device to update.
        /// @param val The bytes to write to the register.
        /// @param size The size of the provided buffer in bytes.
        bool writeBytes(uint8_t registerAddress, uint8_t* val, uint8_t size);

        /// @brief Reads bytes from a given I2C device's register.
        /// @param registerAddress The register on the device to start reading from.
        /// @param buffer The buffer in which to store the bytes read.
        /// @param size The number of bytes to read from the register.
        bool readBytes(uint8_t registerAddress, uint8_t* buffer, uint8_t size);
        
    private:
        I2CMaster* master_;
        i2c_master_dev_handle_t devHandle_;
    };
    
    /// @brief Initializes the I2C master.
    I2CMaster();

    /// @brief Deinitializes the I2C master.
    virtual ~I2CMaster();
    
    /// @brief Creates I2C device for reading and writing.
    /// @param i2cAddress The address of the device to manipulate.
    I2CDevice* getDevice(uint8_t i2cAddress);
    
private:
    i2c_master_bus_handle_t masterHandle_;
};

} // namespace driver

} // namespace ezdv

#endif // I2C_DEVICE_H