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
#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "TLV320.h"
#include "I2CDevice.h"

// TLV320 reset pin GPIO
#define TLV320_RESET_GPIO GPIO_NUM_13

// TLV320 I2S interface GPIOs
#define TLV320_MCLK_GPIO GPIO_NUM_3
#define TLV320_BCLK_GPIO GPIO_NUM_46
#define TLV320_WCLK_GPIO GPIO_NUM_9
#define TLV320_DIN_GPIO GPIO_NUM_11
#define TLV320_DOUT_GPIO GPIO_NUM_10

// TLV320 I2C address
#define TLV320_I2C_ADDRESS (0x18)

// Tag to prepend to log entries coming from this file.
#define CURRENT_LOG_TAG ("TLV320Driver")

namespace ezdv
{

namespace driver
{

TLV320::TLV320(I2CDevice* i2cDevice)
    : DVTask("TLV320Driver", 10 /* TBD */, 4096, tskNO_AFFINITY, 10)
    , i2cDevice_(i2cDevice)
    , currentPage_(-1) // This will cause the page to be set to 0 on first I2C write.
{
    // Register message handlers
    registerMessageHandler<storage::LeftChannelVolumeMessage>(this, &TLV320::onLeftChannelVolume_);
    registerMessageHandler<storage::RightChannelVolumeMessage>(this, &TLV320::onRightChannelVolume_);

    initializeI2S_();
    initializeResetGPIO_();
}

void TLV320::onTaskStart_(DVTask* origin, TaskStartMessage* message)
{
    // To begin, we need to hard reset the TLV320.
    ESP_LOGI(CURRENT_LOG_TAG, "reset TLV320");
    initializeResetGPIO_();
    tlv320HardReset_();
    
    // Enable required clocks.
    ESP_LOGI(CURRENT_LOG_TAG, "configure clocks");
    tlv320ConfigureClocks_();
    
    // Configure processing blocks
    ESP_LOGI(CURRENT_LOG_TAG, "configure processing blocks");
    tlv320ConfigureProcessingBlocks_();
    
    ESP_LOGI(CURRENT_LOG_TAG, "configure LDOs");
    tlv320ConfigurePower_();
    
    // Set power and I/O routing (ADC).
    ESP_LOGI(CURRENT_LOG_TAG, "configure routing for ADC");
    tlv320ConfigureRoutingADC_();
    
    // Set power and I/O routing (DAC).
    ESP_LOGI(CURRENT_LOG_TAG, "configure routing for DAC");
    tlv320ConfigureRoutingDAC_();

    // Note: restoring AGC volumes is delayed until we get the power-on
    // volumes from storage.
    ESP_LOGI(CURRENT_LOG_TAG, "all audio codec config complete");
}

void TLV320::onTaskWake_(DVTask* origin, TaskWakeMessage* message)
{
    // TBD: investigate power consumption of using TLV320 sleep
    // and wakeup vs. simply hard resetting and forcing full
    // reinitialization.
    onTaskStart_(origin, nullptr);
}

void TLV320::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    // TBD: investigate power consumption of using TLV320 sleep
    // and wakeup vs. simply hard resetting and forcing full
    // reinitialization.
    tlv320HardReset_();
}

void TLV320::setVolumeCommon_(uint8_t reg, int8_t vol)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Volume control: setting volume on register %d to %d", (int)reg, vol);
        
    if (vol <= -127) vol = -127;
    else if (vol >= 48) vol = 48;
    
    setConfigurationOption_(0, reg, vol);
}

void TLV320::onLeftChannelVolume_(DVTask* origin, storage::LeftChannelVolumeMessage* message)
{
    setVolumeCommon_(65, message->volume);
}

void TLV320::onRightChannelVolume_(DVTask* origin, storage::RightChannelVolumeMessage* message)
{
    setVolumeCommon_(66, message->volume);
}

void TLV320::setPage_(uint8_t page)
{
    uint8_t buf[2] = { page };
    i2cDevice_->writeBytes(TLV320_I2C_ADDRESS, 0, buf, 1);
    currentPage_ = page;
}

void TLV320::setConfigurationOption_(uint8_t page, uint8_t reg, uint8_t val)
{
    if (page != currentPage_)
    {
        setPage_(page);
    }
    
    uint8_t buf[2] = { val };
    i2cDevice_->writeBytes(TLV320_I2C_ADDRESS, reg, buf, 1);
}

void TLV320::setConfigurationOptionMultiple_(uint8_t page, uint8_t reg, uint8_t* val, uint8_t size)
{
    if (page != currentPage_)
    {
        setPage_(page);
    }

    i2cDevice_->writeBytes(TLV320_I2C_ADDRESS, reg, val, size);
}

uint8_t TLV320::getConfigurationOption_(uint8_t page, uint8_t reg)
{
    if (page != currentPage_)
    {
        setPage_(page);
    }
    
    uint8_t result;
    i2cDevice_->readBytes(TLV320_I2C_ADDRESS, reg, &result, 1);
    return result;
}

void TLV320::initializeI2S_()
{
    // Get the default channel configuration by helper macro.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    // Allocate a new full duplex channel and get the handles of the channels
    i2s_new_channel(&chan_cfg, &i2sTxDevice_, &i2sRxDevice_);

    // Set up configuration for the I2S device:
    //     8 KHz sample rate
    //     Philips format (1 bit shifted, 16 bit/channel stereo)
    //     GPIOs as listed
    i2s_std_config_t i2sConfiguration = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(8000),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = TLV320_MCLK_GPIO,
            .bclk = TLV320_BCLK_GPIO,
            .ws = TLV320_WCLK_GPIO,
            .dout = TLV320_DOUT_GPIO,
            .din = TLV320_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2sTxDevice_, &i2sConfiguration));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2sRxDevice_, &i2sConfiguration));
    ESP_ERROR_CHECK(i2s_channel_enable(i2sTxDevice_));
    ESP_ERROR_CHECK(i2s_channel_enable(i2sRxDevice_));
}

void TLV320::initializeResetGPIO_()
{
    gpio_intr_disable(TLV320_RESET_GPIO);
    gpio_set_direction(TLV320_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(TLV320_RESET_GPIO, GPIO_FLOATING);
    gpio_set_level(TLV320_RESET_GPIO, 1); // active low
}

void TLV320::tlv320HardReset_()
{
    // TLV320's reset line must be held low for 10ns for the reset
    // to start. We also have to wait for 1ms after the reset
    // goes high for everything to reset properly. See section 3.1-3.2 
    // of the Application Reference Guide 
    // (https://www.ti.com/lit/an/slaa408a/slaa408a.pdf).
    gpio_set_level(TLV320_RESET_GPIO, 0);
    esp_rom_delay_us(1);
    gpio_set_level(TLV320_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
}

void TLV320::tlv320ConfigureClocks_()
{
    // Clock calculations for 8K sample rate per guide at 
    // https://www.ti.com/lit/an/slaa404c/slaa404c.pdf
            
    // AOSR = 128
    // DOSR = 128
    // ADC_FS = 8K
    // DAC_FS = 8K
    // ADC_MOD_CLK = AOSR * ADC_FS = 128 * 8000 = 1.024 MHz <= 6.758 MHz
    // DAC_MOD_CLK = DOSR * DAC_FS = 128 * 8000 = 1.024 MHz <= 6.758 MHz
    
    // ADC Processing Block = PRB_R1
    // DAC Processing Block = PRB_P1
    // MADC = 2
    // MDAC = 2
    // ADC_CLK = MADC * ADC_MOD_CLK = 2 * 1.024 MHz = 2.048 MHz
    // DAC_CLK = MDAC * DAC_MOD_CLK = 2 * 1.024 MHz = 2.048 MHz
    // (MADC * AOSR) / 32 = 256 / 32 = 8 >= RC(R1) = 6
    // (MDAC * DOSR) / 32 = 256 / 32 = 8 >= RC(P1) = 8
    // ADC_CLK <= 55.296 MHz
    // DAC_CLK <= 55.296 MHz
    
    // NADC = 40
    // NDAC = 40
    // CODEC_CLKIN = NADC * ADC_CLK = NDAC * DAC_CLK = 81.92 MHz
    // CODEC_CLKIN <= 137MHz
    // CODEC_CLKIN from PLL_CLK
    
    // MCLK is 2.048MHz (8000 * 256)
    // PLL_CLK = MCLK * R * J.D/P
    // 81.92 MHz = 2.048 * 1 * 40.0000 / 1
    // P = 1, R = 1, J = 40, D = 0 
    
    uint8_t pllOpts[] = {
        // Set CODEC_CLKIN to PLL and use MCLK for PLL
        // (Page 0, register 4)
        (0 << 2) | 0b11,
        
        // Set PLL P = 1, R = 1, J = 40, D = 0, power up PLL
        // (Page 0, registers 5-8)
        (1 << 7) | (0b001 << 4) | (0b001),  // P, R, power up
        40, // J
        0, // D[MSB]
        0 // D[LSB]
    };
    setConfigurationOptionMultiple_(0, 4, pllOpts, 5);
    
    // Wait 10ms for PLL to become available
    // (Section 2.7.1, "TLV320AIC3254 Application Reference Guide")
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Set NDAC = 40, MDAC = 2. Power on divider.
    // (Page 0, registers 11 and 12)
    uint8_t dacOpts[] = {
        (1 << 7) | 40,
        (1 << 7) | 2
    };
    setConfigurationOptionMultiple_(0, 11, dacOpts, 2);
    
    // Program DOSR to 128 (Page 0, registers 13-14)
    uint8_t dosr[] = {
        0,
        128
    };
    setConfigurationOptionMultiple_(0, 13, dosr, 2);
    
    // Set NADC = 40, MADC = 2. Keep dividers powered off as
    // NADC == NDAC and MADC == MDAC.
    // (Page 0, registers 18 and 19)
    uint8_t adcOpts[] = {
        40,
        2
    };
    setConfigurationOptionMultiple_(0, 18, adcOpts, 2);
    
    // Program AOSR to 128 (Page 0, register 20).
    setConfigurationOption_(0, 20, 128);
    
    // Set I2S word size to 16 bits (Page 0, register 27)
    setConfigurationOption_(0, 27, 0);
    
#if 0
    // Loopback audio at the ADC/DAC level
    setConfigurationOption_(0, 29, 1 << 4);
#endif // 0
}

void TLV320::tlv320ConfigurePower_()
{
    // Power up AVDD LDO
    setConfigurationOption_(1, 2, (1 << 3) | (1 << 0));
    
    // Disable weak AVDD in presence of external AVDD supply (Page 1, register 1)
    setConfigurationOption_(1, 1, (1 << 3));
    
    // AVDD/DVDD 1.72V, AVDD LDO powered up (Page 1, register 2)
    setConfigurationOption_(1, 2, (0 << 3) | (1 << 0));
    
    // Set full chip common mode to 0.9V
    // HP output CM = 1.65V
    // HP driver supply = LDOin voltage
    // Line output CM = 1.65V
    // Line output supply = LDOin voltage
    // (Page 1, register 10)
    setConfigurationOption_(1, 10, (3 << 4) | (1 << 3) | (1 << 1) | (1 << 0));
    
    // Set ADC PTM to PTM_R1 (Page 1, register 61)
    setConfigurationOption_(1, 61, 0xFF);
    
    // Set DAC PTM to PTM_P1 (Page 1, registers 3-4)
    // Note: PTM_P4 requires >= 20 bits for I2S, hence not used here.
    uint8_t dacPtm[] = {
        0x2 << 2,
        0x2 << 2
    };
    setConfigurationOptionMultiple_(1, 3, dacPtm, 2);
    
    // Set input powerup time to 3.1ms (Page 1, register 71)
    setConfigurationOption_(1, 71, 0b110001);
    
    // REF will power up in 40ms (Page 1, register 123)
    setConfigurationOption_(1, 123, 1);
}

void TLV320::tlv320ConfigureProcessingBlocks_()
{
    // Set ADC_PRB and DAC_PRB to P2 and R1 (Page 0, registers 60-61).
    uint8_t prb[] = {
        1,
        2
    };
    setConfigurationOptionMultiple_(0, 60, prb, 2);
    
    // Set ADC filter coefficients for HPF, center frequency 130 Hz.
    // All five biquads on both channels are set to this filter to reduce
    // background hiss in the recorded audio.
    uint8_t adcFilter[] = {
        0x77, 0x15, 0x39, 0x00,
        0x88, 0xEA, 0xC7, 0x00,
        0x77, 0x15, 0x39, 0x00,
        0x76, 0xC5, 0xA2, 0x00,
        0x91, 0x36, 0x60, 0x00
    };
    setConfigurationOptionMultiple_(8, 36, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(9, 44, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(8, 56, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(9, 64, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(8, 76, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(9, 84, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(8, 96, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(9, 104, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(8, 116, adcFilter, sizeof(adcFilter));
    setConfigurationOptionMultiple_(9, 124, adcFilter, sizeof(adcFilter));
}

void TLV320::tlv320ConfigureRoutingADC_()
{
    // Enable 2.5V mic bias on headset jack using LDOIN (Page 1, register 51)
    setConfigurationOption_(1, 51, (1 << 6) | (0b10 << 4) | (1 << 3));
    
    // Set ADC routing: IN1_L left channel, IN1_R right channel,
    // 20kohm impedence (Page 1, registers 52, 54, 55, 57)
    setConfigurationOption_(1, 52, 0b10 << 6);
    setConfigurationOption_(1, 54, 0b10 << 6);
    setConfigurationOption_(1, 55, 0b10 << 6);
    setConfigurationOption_(1, 57, 0b10 << 6);
    
    // Weakly connect all unused inputs to ground.
    // (Page 1, register 58)
    setConfigurationOption_(1, 58, (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2));
    
    // Unmute PGAs, gain = 6dB due to 20k impedence
    // (Page 1, registers 59 and 60)
    setConfigurationOption_(1, 59, 0x0c);
    setConfigurationOption_(1, 60, 0x0c);
    
    // Power on ADC (Page 0, register 81)
    setConfigurationOption_(0, 81, (1 << 7) | (1 << 6));
    
    // Unmute ADC (Page 0, register 82)
    setConfigurationOption_(0, 82, 0);
    
    //setConfigurationOption_(0, 84, 0b0101000 /*0x0c*/);
    
    tlv320ConfigureAGC_();
}

void TLV320::tlv320ConfigureRoutingDAC_()
{
    // 6kohm depop, N = 5.0, 50ms soft start (Page 1, register 20)
    setConfigurationOption_(1, 20, (1 << 6) | (0b1001 << 2) | (1 << 0));
    
    // Set DAC routing: HPL, HPR come from DAC
    // (Page 1, registers 12 and 13)
    setConfigurationOption_(1, 12, 1 << 3);
    setConfigurationOption_(1, 13, 1 << 3);
    
    // Power up HPL and HPR
    // (Page 1, register 9)
    setConfigurationOption_(1, 9, (1 << 5) | (1 << 4));
    
    // Unmute HPL and HPR, gain = 0dB
    // (Page 1, registers 16 and 17)
    setConfigurationOption_(1, 16, 0);
    setConfigurationOption_(1, 17, 0);
    
    // Wait until gain fully applied
    // (Page 1, register 63)
    uint8_t gainRegVal = 0;
    int count = 0;
    do
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        gainRegVal = getConfigurationOption_(1, 63);
    } while ((count++ < 50) && (gainRegVal & (11 << 6)) == 0);
    
    // Power on DAC (Page 0, register 63)
    setConfigurationOption_(0, 63, (1 << 7) | (1 << 6) | (1 << 4) | (1 << 2) | (1 << 1));
    
    // Unmute DAC (Page 0, register 64)
    setConfigurationOption_(0, 64, 0);
}

void TLV320::tlv320ConfigureAGC_()
{
    uint8_t agcConfig[] = {
        // Enable AGC, -10dB target, gain hysteresis disabled
        (1 << 7) | (0b010 << 4) | (0b00 << 0),  
        
        // Hysteresis 2dB, -90dB noise threshold
        (0b01 << 6) | (0b11111 << 1),
        
        // 40dB maximum gain
        0x50, //0b100100,
        
        // Attack time = 20ms (25 * 32 clocks, scale 2)
        0x08,
        
        // Decay time = 500ms (19 * 512 clocks, scale 2)
        0x32,
        
        // Noise debounce = 0ms
        0x00,
        
        // Signal debounce = 2ms
        0x06,
    };
    
    // Set AGC configuration for both channels (page 0, register 86 for left, register 94 for right)
    setConfigurationOptionMultiple_(0, 86, agcConfig, sizeof(agcConfig));
    setConfigurationOptionMultiple_(0, 94, agcConfig, sizeof(agcConfig));
}

} // namespace driver

} // namespace ezdv