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

#include "UserInterfaceTask.h"
#include "audio/BeeperMessage.h"
#include "driver/LedMessage.h"

#define VOL_BUTTON_HOLD_TIMER_TICK_US 100000
#define NET_LED_FLASH_TIMER_TICK_US 1000000

#define CURRENT_LOG_TAG ("UserInterfaceTask")

// Defined in Application.cpp.
extern void StartSleeping();

namespace ezdv
{

namespace ui
{

static std::map<audio::SetFreeDVModeMessage::FreeDVMode, std::string> ModeList_ = {
    { audio::SetFreeDVModeMessage::ANALOG, "  ANA" },
    { audio::SetFreeDVModeMessage::FREEDV_700D, "  700D" },
    { audio::SetFreeDVModeMessage::FREEDV_700E, "  700E" },
    { audio::SetFreeDVModeMessage::FREEDV_1600, "  1600" },
};

UserInterfaceTask::UserInterfaceTask()
    : DVTask("UserInterfaceTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 10)
    , volHoldTimer_(this, std::bind(&UserInterfaceTask::updateVolumeCommon_, this), VOL_BUTTON_HOLD_TIMER_TICK_US)
    , networkFlashTimer_(this, std::bind(&UserInterfaceTask::flashNetworkLight_, this), NET_LED_FLASH_TIMER_TICK_US)
    , currentMode_(audio::SetFreeDVModeMessage::ANALOG)
    , isTransmitting_(false)
    , isActive_(false)
    , leftVolume_(0)
    , rightVolume_(0)
    , volIncrement_(0)
    , netLedStatus_(false)
    , radioStatus_(false)
{
    registerMessageHandler(this, &UserInterfaceTask::onButtonShortPressedMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onButtonLongPressedMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onButtonReleasedMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onFreeDVSyncStateMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onNetworkStateChange_);
    registerMessageHandler(this, &UserInterfaceTask::onRadioStateChange_);
}

UserInterfaceTask::~UserInterfaceTask()
{
    // Disable PTT
    driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::PTT_NPN, false);
    publish(ledMessage);
    delete ledMessage;

    volHoldTimer_.stop();
}

void UserInterfaceTask::onTaskStart_()
{
    isActive_ = true;
}

void UserInterfaceTask::onTaskWake_()
{
    isActive_ = true;

    // Disable all LEDs as we're fully up now.
    ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, false);
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
    publish(&msg);

    // Send welcome message to beeper
    ESP_LOGI(CURRENT_LOG_TAG, "Sending welcome message to beeper");
    audio::SetBeeperTextMessage* beeperMessage = new audio::SetBeeperTextMessage(ModeList_[currentMode_].c_str());
    publish(beeperMessage);
    delete beeperMessage;
}

void UserInterfaceTask::onTaskSleep_()
{
    isActive_ = false;

    // Enable all LEDs so the user knows we're shutting down. They'll turn off
    // once the ESP32 enters deep sleep
    ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, true);
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
    publish(&msg);

    // Send goodbye message to beeper. The extra spaces are to give a bit more timing
    // so the beeper doesn't step on itself.
    audio::SetBeeperTextMessage* beeperMessage = new audio::SetBeeperTextMessage("  73  ");
    publish(beeperMessage);
    delete beeperMessage;
}

void UserInterfaceTask::onButtonShortPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message)
{
    if (isActive_)
    {
        switch(message->button)
        {
            case driver::ButtonLabel::PTT:
            {
                isTransmitting_ = true;

                // Switch FreeDV to TX mode
                audio::FreeDVSetPTTStateMessage* pttStateMessage = new audio::FreeDVSetPTTStateMessage(true);
                publish(pttStateMessage);
                delete pttStateMessage;

                // Enable LED and LED NPN so the radio itself starts transmitting
                driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::PTT_NPN, true);
                publish(ledMessage);
                ledMessage->led = driver::SetLedStateMessage::PTT;
                publish(ledMessage);
                delete ledMessage;

                break;
            }
            case driver::ButtonLabel::MODE:
            {
                int tmpMode = (int)currentMode_ + 1;
                currentMode_ = (audio::SetFreeDVModeMessage::FreeDVMode)tmpMode;
                if (currentMode_ == audio::SetFreeDVModeMessage::MAX_FREEDV_MODES)
                {
                    currentMode_ = audio::SetFreeDVModeMessage::ANALOG;
                }

                audio::SetFreeDVModeMessage* setModeMessage = new audio::SetFreeDVModeMessage(currentMode_);
                publish(setModeMessage);
                delete setModeMessage;

                // Send new mode to beeper
                audio::SetBeeperTextMessage* beeperMessage = new audio::SetBeeperTextMessage(ModeList_[currentMode_].c_str());
                publish(beeperMessage);
                delete beeperMessage;

                break;
            }
            case driver::ButtonLabel::VOL_UP:
                volIncrement_ = 1;
                updateVolumeCommon_();
                break;
            case driver::ButtonLabel::VOL_DOWN:
                volIncrement_ = -1;
                updateVolumeCommon_();
                break;
            default:
                assert(0);
        }
    }
}

void UserInterfaceTask::onButtonLongPressedMessage_(DVTask* origin, driver::ButtonLongPressedMessage* message)
{
    if (isActive_)
    {
        if (message->button == driver::ButtonLabel::MODE)
        {
            // Long press Mode button triggers shutdown, all other long presses currently ignored
            StartSleeping();
        }
    }
}

void UserInterfaceTask::onButtonReleasedMessage_(DVTask* origin, driver::ButtonReleasedMessage* message)
{
    if (isActive_)
    {
        switch(message->button)
        {
            case driver::ButtonLabel::PTT:
            {
                isTransmitting_ = false;

                // Disable LED and LED NPN so the radio itself stops transmitting
                driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::PTT_NPN, false);
                publish(ledMessage);
                ledMessage->led = driver::SetLedStateMessage::PTT;
                publish(ledMessage);
                delete ledMessage;

                // Switch FreeDV to RX mode
                audio::FreeDVSetPTTStateMessage* pttStateMessage = new audio::FreeDVSetPTTStateMessage(false);
                publish(pttStateMessage);
                delete pttStateMessage;

                break;
            }
            case driver::ButtonLabel::MODE:
                // Ignored
                break;
            case driver::ButtonLabel::VOL_UP:
            case driver::ButtonLabel::VOL_DOWN:
                volHoldTimer_.stop();
                break;
            default:
                assert(0);
        }
    }
}

void UserInterfaceTask::onFreeDVSyncStateMessage_(DVTask* origin, audio::FreeDVSyncStateMessage* message)
{
    // Enable/disable sync LED as appropriate
    driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::SYNC, message->syncState);
    publish(ledMessage);
    delete ledMessage;
}

void UserInterfaceTask::onLeftChannelVolumeMessage_(DVTask* origin, storage::LeftChannelVolumeMessage* message)
{
    leftVolume_ = message->volume;
}

void UserInterfaceTask::onRightChannelVolumeMessage_(DVTask* origin, storage::RightChannelVolumeMessage* message)
{
    rightVolume_ = message->volume;
}

void UserInterfaceTask::updateVolumeCommon_()
{
    if (isTransmitting_)
    {
        rightVolume_ += volIncrement_;

        storage::SetRightChannelVolumeMessage* volMessage = new storage::SetRightChannelVolumeMessage(rightVolume_);
        publish(volMessage);
        delete volMessage;
    }
    else
    {
        leftVolume_ += volIncrement_;

        storage::SetLeftChannelVolumeMessage* volMessage = new storage::SetLeftChannelVolumeMessage(leftVolume_);
        publish(volMessage);
        delete volMessage;
    }

    // Start hold timer
    volHoldTimer_.stop();
    volHoldTimer_.start(true);
}

void UserInterfaceTask::onNetworkStateChange_(DVTask* origin, network::WirelessNetworkStatusMessage* message)
{
    if (message->state)
    {
        networkFlashTimer_.start();
    }
    else
    {
        networkFlashTimer_.stop();
        
        netLedStatus_ = false;
        driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::NETWORK, netLedStatus_);
        publish(ledMessage);
        delete ledMessage;
    }
}

void UserInterfaceTask::onRadioStateChange_(DVTask* origin, network::RadioConnectionStatusMessage* message)
{
    radioStatus_ = message->state;
}

void UserInterfaceTask::flashNetworkLight_()
{
    netLedStatus_ = radioStatus_ || !netLedStatus_;
    
    driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::NETWORK, netLedStatus_);
    publish(ledMessage);
    delete ledMessage;
}

}

}