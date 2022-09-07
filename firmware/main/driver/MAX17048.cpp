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

#include "esp_log.h"

#include "MAX17048.h"

// 5% battery SOC alert threshold (0b00000 = 32%, 0b11111 = 1%)
#define EMPTY_ALERT_THRESHOLD (0x1B)

// VRESET = 2.5V
#define VRESET_VAL (0x7D)

// I2C address of the 17048.
#define I2C_ADDRESS (0b0110110)

// Supported I2C registers on the 17048.
#define REG_VCELL (0x02)
#define REG_SOC (0x04)
#define REG_MODE (0x06)
#define REG_VERSION (0x08)
#define REG_HIBRT (0x0A)
#define REG_CONFIG (0x0C)
#define REG_VALRT (0x14)
#define REG_CRAT (0x16)
#define REG_VRESET (0x18)
#define REG_STATUS (0x1A)
#define REG_CMD (0xFE)

#define CURRENT_LOG_TAG "MAX17048"

using namespace std::placeholders;

// Global function to trigger shutdown.
extern void StartSleeping();

namespace ezdv
{

namespace driver
{

MAX17048::MAX17048(I2CDevice* i2cDevice)
    : DVTask("MAX17048", 10 /* TBD */, 4096, tskNO_AFFINITY, 10, pdMS_TO_TICKS(10000))
    , i2cDevice_(i2cDevice)
    , batAlertGpio_(this, std::bind(&MAX17048::onInterrupt_, this, _2))
    , enabled_(false)
        
{
    // empty
}

void MAX17048::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Checking for device...");
    
    if (deviceExists_())
    {
        enabled_ = true;
        if (needsConfiguration_())
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Device reset, configuration required.");
            configureDevice_();
        }
    }
    else
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Device not found; battery monitoring disabled.");
    }
}

void MAX17048::onTaskWake_()
{
    // Doing the same stuff as start.
    onTaskStart_();
}

void MAX17048::onTaskSleep_()
{
    // empty
}

void MAX17048::onTaskTick_()
{
    if (enabled_)
    {
        // Retrieve voltage, SOC and rate of change
        uint16_t voltage = 0;
        uint16_t soc = 0;
        uint16_t socChangeRate = 0;
        bool success = readInt16Reg_(REG_VCELL, &voltage);
        success &= readInt16Reg_(REG_SOC, &soc);
        success &= readInt16Reg_(REG_CRAT, &socChangeRate);
        assert(success);
        
        ESP_LOGI(CURRENT_LOG_TAG, "Current battery stats: V = %x, SOC = %x, CRATE = %x", voltage, soc, socChangeRate);
    }
}

bool MAX17048::writeInt16Reg_(uint8_t reg, uint16_t val)
{
    // Data is MSB first.
    uint8_t data[] = 
    {
        (val & 0xF0) >> 8,
        (uint8_t)(val & 0x0F)
    };
    
    return i2cDevice_->writeBytes(I2C_ADDRESS, reg, data, sizeof(uint16_t));
}

bool MAX17048::readInt16Reg_(uint8_t reg, uint16_t* val)
{
    uint8_t data[] = {0, 0};
    auto rv = i2cDevice_->readBytes(I2C_ADDRESS, reg, data, sizeof(uint16_t));
    if (rv)
    {
        *val = (data[0] << 8) | data[1];
    }
    
    return rv;
}

bool MAX17048::deviceExists_()
{
    // we should be able to read a version if we're able to communicate
    // with the chip
    uint16_t deviceVersion;
    auto rv = readInt16Reg_(REG_VERSION, &deviceVersion);
    if (rv)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Detected device version %x", deviceVersion);
    }
    
    return rv;
}

bool MAX17048::needsConfiguration_()
{
    uint16_t status = 0;
    auto rv = readInt16Reg_(REG_STATUS, &status);
    assert(rv == true);
    
    return (status & 0x10) != 0;
}

void MAX17048::configureDevice_()
{
    // Configure the following:
    //    Default RCOMP = 0x97 per datasheet
    //    Enable SOC change alert
    //    Low battery threshold
    uint16_t val = (0x97 << 8) | (1 << 6) | EMPTY_ALERT_THRESHOLD;
    auto rv = writeInt16Reg_(REG_CONFIG, val);
    assert(rv == true);
    
    // Set VRESET threshold.
    val = (EMPTY_ALERT_THRESHOLD << 8);
    rv = writeInt16Reg_(REG_VRESET, val);
    assert(rv == true);
    
    // Clear status register to indicate that we're
    // done configuring ourselves.
    val = 0;
    rv = writeInt16Reg_(REG_STATUS, val);
    assert(rv == true);
    
    ESP_LOGI(CURRENT_LOG_TAG, "Device configured.");
}

void MAX17048::onInterrupt_(bool val)
{
    // Trigger only when line goes low.
    if (enabled_ && !val)
    {
        // Retrieve status so we know which alerts were triggered.
        uint16_t val = 0;
        auto rv = readInt16Reg_(REG_STATUS, &val);
        assert(rv == true);
        
        bool voltageHigh = (val & (1 << 9)) != 0;
        bool voltageLow = (val & (1 << 10)) != 0;
        bool voltageReset = (val & (1 << 11)) != 0;
        bool socLow = (val & (1 << 12)) != 0;
        bool socChange = (val & (1 << 12)) != 0;
        
        ESP_LOGI(
            CURRENT_LOG_TAG, 
            "Interrupt: VH = %d, VL = %d, VR = %d, SOC low = %d, SOC change = %d",
            voltageHigh, voltageLow, voltageReset, socLow, socChange);
            
        if (socLow)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "SOC below threshold, triggering immediate shutdown");
            StartSleeping();
        }
        
        // TBD: report other values to the user
        
        // Clear status register to deassert interrupt.
        val = 0;
        rv = writeInt16Reg_(REG_STATUS, val);
        assert(rv == true);
    }
}

}

}