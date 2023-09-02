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

#ifndef EZDV_APPLICATION_H
#define EZDV_APPLICATION_H

#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "audio/AudioMixer.h"
#include "audio/BeeperTask.h"
#include "audio/FreeDVTask.h"
#include "audio/VoiceKeyerTask.h"
#include "driver/ButtonArray.h"
#include "driver/ButtonMessage.h"
#include "driver/I2CDevice.h"
#include "driver/LedArray.h"
#include "driver/MAX17048.h"
#include "driver/TLV320.h"
#include "network/WirelessTask.h"
#include "storage/SettingsTask.h"
#include "storage/SoftwareUpdateTask.h"
#include "ui/UserInterfaceTask.h"
#include "ui/FuelGaugeTask.h"
#include "ui/RFComplianceTestTask.h"

using namespace ezdv::task;

// Uncomment below to enable automated TX/RX test
// (5s TX, 5s RX, repeat indefinitely)
//#define ENABLE_AUTOMATED_TX_RX_TEST

namespace ezdv
{

class App : public DVTask
{
public:
    App();

#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
    audio::FreeDVTask& getFreeDVTask() { return freedvTask_; }
    ui::UserInterfaceTask& getUITask() { return uiTask_; }
#endif // ENABLE_AUTOMATED_TX_RX_TEST

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;
    
private:
    audio::AudioMixer audioMixer_;
    audio::BeeperTask beeperTask_;
    audio::FreeDVTask freedvTask_;
    driver::ButtonArray buttonArray_;
    driver::I2CDevice i2cDevice_;
    driver::LedArray ledArray_;
    driver::MAX17048 max17048_;
    driver::TLV320 tlv320Device_;
    network::WirelessTask wirelessTask_;
    storage::SettingsTask settingsTask_;
    storage::SoftwareUpdateTask softwareUpdateTask_;
    ui::UserInterfaceTask uiTask_;
    ui::RfComplianceTestTask rfComplianceTask_;

#if 0 /* XXX HW changes are required to fully enable fuel gauge support. */
    ui::FuelGaugeTask fuelGaugeTask_;
#endif // 0

    audio::VoiceKeyerTask voiceKeyerTask_;
    
    bool rfComplianceEnabled_;

    void enablePeripheralPower_();
    void enterDeepSleep_();
    
};

}

#endif // EZDV_APPLICATION_H