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

#ifndef TLV320_DRIVER_H
#define TLV320_DRIVER_H

#include "driver/i2s_std.h"

#include "InputGPIO.h"
#include "I2CMaster.h"
#include "audio/AudioInput.h"
#include "storage/SettingsMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

// GPIO assignments for TLV320 interrupts
#define GPIO_TLV320_INT1 GPIO_NUM_12
#define GPIO_TLV320_INT2 GPIO_NUM_14

namespace ezdv
{

namespace driver
{

using namespace ezdv::task;

class TLV320 : public DVTask, public audio::AudioInput
{
public:
    TLV320(I2CMaster* i2cMaster);
    virtual ~TLV320();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;

private:
    I2CMaster::I2CDevice* i2cDevice_;
    int currentPage_;
    i2s_chan_handle_t i2sTxDevice_;
    i2s_chan_handle_t i2sRxDevice_;
    InputGPIO<GPIO_TLV320_INT1> int1Gpio_;
    InputGPIO<GPIO_TLV320_INT2> int2Gpio_;
    
    void onLeftChannelVolume_(DVTask* origin, storage::LeftChannelVolumeMessage* message);
    void onRightChannelVolume_(DVTask* origin, storage::RightChannelVolumeMessage* message);

    void setPage_(uint8_t page);
    void setConfigurationOption_(uint8_t page, uint8_t reg, uint8_t val);
    void setConfigurationOptionMultiple_(uint8_t page, uint8_t reg, uint8_t* val, uint8_t size);
    uint8_t getConfigurationOption_(uint8_t page, uint8_t reg);

    void setVolumeCommon_(uint8_t reg, int8_t vol);

    void initializeI2S_();
    void initializeResetGPIO_();
        
    void tlv320HardReset_();
    void tlv320ConfigureClocks_();
    void tlv320ConfigureProcessingBlocks_();
    void tlv320ConfigurePower_();
    void tlv320ConfigureRoutingADC_();
    void tlv320ConfigureRoutingDAC_();
    void tlv320ConfigureAGC_();
    void tlv320ConfigureInterrupts_();

    void onInterrupt1Fire_(bool state);
    void onInterrupt2Fire_(bool state);
};

} // namespace driver
    
} // namespace ezdv
namespace driver
{
    
} // namespace driver

#endif // TLV320_DRIVER_H