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

#include <cmath>

#include "esp_log.h"

#include "MAX17048.h"
#include "BatteryMessage.h"

// 5% battery SOC alert threshold (0b00000 = 32%, 0b11111 = 1%)
#define EMPTY_ALERT_THRESHOLD (0x1B)

// VRESET = 3V
#define VRESET_VAL (0x4B)

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

#define ADC_GPIO_NUM GPIO_NUM_18

using namespace std::placeholders;

// Global function to trigger shutdown.
extern void StartSleeping();

namespace ezdv
{

namespace driver
{

MAX17048::MAX17048(I2CDevice* i2cDevice)
    : DVTask("MAX17048", 10 /* TBD */, 2870, tskNO_AFFINITY, 10, pdMS_TO_TICKS(60000))
    , i2cDevice_(i2cDevice)
    , batAlertGpio_(this, std::bind(&MAX17048::onInterrupt_, this, _2))
    , usbPower_(this, std::bind(&MAX17048::onTaskTick_, this), false, false)
    , enabled_(false)
    , adcHandle_(nullptr)
    , adcCalibrationHandle_(nullptr)
    , isLowSoc_(false)
    , isStarting_(true)
    , suppressForcedSleep_(false)
{
    registerMessageHandler(this, &MAX17048::onLowBatteryShutdownMessage_);
    registerMessageHandler(this, &MAX17048::onRequestBatteryStateMessage_);
}

void MAX17048::onTaskStart_()
{
    usbPower_.start();
    usbPower_.enableInterrupt(true);

    ESP_LOGI(CURRENT_LOG_TAG, "Checking for device...");
    
    if (deviceExists_())
    {
        enabled_ = true;
        
        //if (needsConfiguration_())
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Device reset, configuration required.");
            configureDevice_();
        }
        
        // Clear status register to indicate that we're
        // done configuring ourselves/reset interrupt.
        auto val = 0;
        auto rv = writeInt16Reg_(REG_STATUS, val);
        assert(rv == true);
        
        batAlertGpio_.start();
        batAlertGpio_.enableInterrupt(true);
    }
    else
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Device not found; battery monitoring disabled.");
    }
    
    // Start ADC. This will be the primary sensor for battery calibration unless the value is 
    // obviously invalid, then we use the internal temp sensor.
    adc_unit_t adcUnit;
    adc_channel_t adcChannel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ADC_GPIO_NUM, &adcUnit, &adcChannel));
    
    adc_oneshot_unit_init_cfg_t adcConfig = {
        .unit_id = adcUnit, // GPIO 18
        .clk_src = (adc_oneshot_clk_src_t)ADC_DIGI_CLK_SRC_DEFAULT, // default clock source
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adcConfig, &adcHandle_));
    
    adc_oneshot_chan_cfg_t oneshotConfig = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adcHandle_, adcChannel, &oneshotConfig));
    
    // Configure ADC calibrator to get voltages
    adc_cali_curve_fitting_config_t calibrationConfig = {
        .unit_id = adcUnit,
        .chan = (adc_channel_t)0, // not currently used
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&calibrationConfig, &adcCalibrationHandle_));

    // Perform initial task tick in case we need to immediately force sleep on boot
    // (e.g. very low voltage)
    onTaskTick_();

    isStarting_ = false;
}

void MAX17048::onTaskSleep_()
{
    // Stop ADC
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(adcCalibrationHandle_));
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adcHandle_));

#if 0 /* XXX HW changes are required to fully enable fuel gauge support. */
    // Set overvoltage threshold to the same as current VCELL.
    // This is needed to allow detection of charging without needing
    // to be plugged in.
    uint16_t voltage = 0;
    readInt16Reg_(REG_VCELL, &voltage);
    float voltFloat = voltage * 0.000078125;
    uint16_t voltThresholds = (uint16_t)(voltFloat * 50) + 2; // 50 = 1/0.02
    writeInt16Reg_(REG_VALRT, voltThresholds); 
#endif // 0

    // Force MAX17048 into hibernate to reduce power consumption.
    if (enabled_)
    {
        bool rv = writeInt16Reg_(REG_HIBRT, 0xFFFF);
        assert(rv == true);
    }
    enabled_ = false;
    
#if 0 /* XXX HW changes are required to fully enable fuel gauge support. */
    // Clear ALERT to avoid immediate retriggering of the interrupt.
    auto val = 0;
    rv = writeInt16Reg_(REG_STATUS, val);
    assert(rv == true);
#endif // 0
}

void MAX17048::onRequestBatteryStateMessage_(DVTask* origin, RequestBatteryStateMessage* reqMessage)
{
    uint16_t config = 0;
    bool success = true;
        
    if (!enabled_)
    {
        return;
    }
    
    // Read current temperature sensor value (in degC) and update RCOMP
    // based on formula from the datasheet. Note that the ESP32 internal
    // temperature sensor will read higher than ambient a lot of the time,
    // but it's likely better to underestimate capacity than overestimate.
    if (reqMessage->updateTemp)
    {
        success = readInt16Reg_(REG_CONFIG, &config);
        assert(success);
    
        auto degC = temperatureFromADC_();
            
        int rcomp = 0x97;
        if (degC > 20)
        {
            rcomp += (degC - 20) * (-0.5); // TempCoUp default
        }
        else
        {
            rcomp += (degC - 20) * (-5.0); // TempCoDown default
        }
    
        if (rcomp > 255) rcomp = 255;
        else if (rcomp < 0) rcomp = 0;
    
        ESP_LOGI(CURRENT_LOG_TAG, "Current temperature: %.02f C, new RCOMP: %d", degC, rcomp);
    
        config &= 0x00FF;
        config = ((uint8_t)rcomp << 8) | config;
        success = writeInt16Reg_(REG_CONFIG, config);
        assert(success);
    }
    
    // Retrieve voltage, SOC and rate of change
    uint16_t voltage = 0;
    uint16_t soc = 0;
    uint16_t socChangeRate = 0;
    uint16_t status = 0;
    
    success = readInt16Reg_(REG_VCELL, &voltage);
    success &= readInt16Reg_(REG_SOC, &soc);
    success &= readInt16Reg_(REG_CRAT, &socChangeRate);
    success &= readInt16Reg_(REG_STATUS, &status);
    success &= readInt16Reg_(REG_CONFIG, &config);
    assert(success);
    
    // Publish battery status to all interested parties.
    // Also clean up the values as returned by the MAX17048
    // as it can return SOC > 100 or < 0.
    auto calcSoc = soc / 256.0;
    if (calcSoc > 100)
    {
        calcSoc = 100;
        socChangeRate = 0;
    }
    else if (calcSoc < 0)
    {
        calcSoc = 0;
    }
    BatteryStateMessage message(voltage * 0.000078125, calcSoc, (int16_t)socChangeRate * 0.208, usbPower_.getCurrentValue());
    publish(&message);
    
    ESP_LOGI(CURRENT_LOG_TAG, "Current battery stats: STATUS = %x, CONFIG = %x, V = %.2f, SOC = %.2f%%, CRATE = %.2f%%/hr", status, config, message.voltage, message.soc, message.socChangeRate);

    if (!suppressForcedSleep_)
    {
        if (message.voltage <= 3 || calcSoc <= 5 || (calcSoc <= 5.5 && isStarting_))
        {
            // If battery power is extremely low, immediately force sleep.
            isLowSoc_ = true;

            // Post 
            LowBatteryShutdownMessage message;
            post(&message);
        }
    }
}

void MAX17048::onTaskTick_()
{
    if (enabled_)
    {
        RequestBatteryStateMessage message(true);
        post(&message);
    }
}

bool MAX17048::writeInt16Reg_(uint8_t reg, uint16_t val)
{
    // Data is MSB first.
    uint8_t data[] = 
    {
        (uint8_t)((val & 0xFF00) >> 8),
        (uint8_t)(val & 0x00FF)
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
    uint16_t val = 0x9700 | (1 << 6) | EMPTY_ALERT_THRESHOLD;
    ESP_LOGI(CURRENT_LOG_TAG, "setting REG_CONFIG to %x", val);
    auto rv = writeInt16Reg_(REG_CONFIG, val);
    assert(rv == true);
    
    // Set VRESET threshold.
    val = (EMPTY_ALERT_THRESHOLD << 8);
    rv = writeInt16Reg_(REG_VRESET, val);
    assert(rv == true);

#if 0 /* XXX HW changes are required to fully enable fuel gauge support. */
    // Set voltage thresholds back to defaults
    rv = writeInt16Reg_(REG_VALRT, 0x00FF);
    assert(rv == true);
#endif // 0

    // Disable hibernate mode. Hibernate will be forced
    // on sleep.
    rv = writeInt16Reg_(REG_HIBRT, 0);
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
        bool socChange = (val & (1 << 13)) != 0;
        
        ESP_LOGI(
            CURRENT_LOG_TAG, 
            "Interrupt: VH = %d, VL = %d, VR = %d, SOC low = %d, SOC change = %d",
            voltageHigh, voltageLow, voltageReset, socLow, socChange);
            
        if (socLow || voltageLow)
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

float MAX17048::temperatureFromADC_()
{
    adc_unit_t adcUnit;
    adc_channel_t adcChannel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ADC_GPIO_NUM, &adcUnit, &adcChannel));
    
    // Read 8 samples from the temperature ADC and average them out.
    // This improves accuracy.
    int val = 0;
    int valAccum = 0;
    for (int count = 0; count < 8; count++)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adcHandle_, adcChannel, &val));
        valAccum += val;
    }
    val = valAccum >> 3;
    
    ESP_LOGI(CURRENT_LOG_TAG, "ADC: %d", val);
    
    // Formula 1: raw ADC to voltage:
    // V = 3.3x/4096 (4096 = 2^12 since 12 bit ADC)
    //float voltage = (3.3 * val) / 4096;
    //ESP_LOGI(CURRENT_LOG_TAG, "V: %f", voltage);
    int millivolts = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adcCalibrationHandle_, val, &millivolts));
    float voltage = millivolts / 1000.0;
    ESP_LOGI(CURRENT_LOG_TAG, "V: %f", voltage);
    
    // Formula 2: voltage to resistance:
    // (10000/3.3 * V) / (1 - V/3.3) = R
    // 10000: bias resistor value
    float resistance = (3030.3 * voltage) / (1 - voltage / 3.3);
    ESP_LOGI(CURRENT_LOG_TAG, "R: %f", resistance);
    
    // Formula 3: resistance to temperature:
    // 1/T = 1/TO + (1/β) ⋅ ln (R/RO)
    // β: 3950
    float tempInv = (1/298.15) + (1.0/3950.0) * log(resistance / 10000.0);
    float temp = 1.0 / tempInv - 273.15; // convert from K to C
    
    ESP_LOGI(CURRENT_LOG_TAG, "Thermistor temp: %f C", temp);
    return temp;
}

void MAX17048::onLowBatteryShutdownMessage_(DVTask* origin, LowBatteryShutdownMessage* message)
{
    // Sleep before triggering the shutdown.
    // This is in case we detected a low battery condition early in startup,
    // so that the short circuit logic to force shutdown can execute.
    // Otherwise, the waitFor() logic during wake could end up inadvertently
    // triggering the full shutdown sequence, potentially causing crashes.
    vTaskDelay(pdMS_TO_TICKS(1000));

    StartSleeping();
}

}

}