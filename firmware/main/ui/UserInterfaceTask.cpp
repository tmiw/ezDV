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

static std::map<audio::FreeDVMode, std::string> ModeList_ = {
    { audio::ANALOG, "  ANA" },
    { audio::FREEDV_700D, "  700D" },
    { audio::FREEDV_700E, "  700E" },
    { audio::FREEDV_1600, "  1600" },
};

UserInterfaceTask::UserInterfaceTask()
    : DVTask("UserInterfaceTask", 10, 4096, tskNO_AFFINITY, 64, pdMS_TO_TICKS(10))
    , volHoldTimer_(this, this, &UserInterfaceTask::updateVolumeCommon_, VOL_BUTTON_HOLD_TIMER_TICK_US, "VolHoldTimer")
    , networkFlashTimer_(this, this, &UserInterfaceTask::flashNetworkLight_, NET_LED_FLASH_TIMER_TICK_US, "NetworkFlashTimer")
    , timeOutTimer_(this, this, &UserInterfaceTask::stopTx_, 1000000 /* placeholder, will be set by user config */, "TxTimeoutTimer")
    , currentMode_(audio::ANALOG)
    , isTransmitting_(false)
    , isActive_(false)
    , leftVolume_(0)
    , rightVolume_(0)
    , volIncrement_(0)
    , netLedStatus_(false)
    , radioStatus_(false)
    , voiceKeyerRunning_(false)
    , voiceKeyerEnabled_(false)
    , lastBatteryLevel_(0)
    , sleepPending_(false)
    , allowHeadsetPtt_(false)
{
    registerMessageHandler(this, &UserInterfaceTask::onButtonShortPressedMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onButtonLongPressedMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onButtonReleasedMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onFreeDVSyncStateMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onNetworkStateChange_);
    registerMessageHandler(this, &UserInterfaceTask::onRadioStateChange_);
    registerMessageHandler(this, &UserInterfaceTask::onRequestTxMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onRequestRxMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onVoiceKeyerSettingsMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onVoiceKeyerCompleteMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onADCOverload_);
    registerMessageHandler(this, &UserInterfaceTask::onHeadsetButtonPressed_);
    registerMessageHandler(this, &UserInterfaceTask::onBatteryStateUpdate_);
    registerMessageHandler(this, &UserInterfaceTask::onLeftChannelVolumeMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onRightChannelVolumeMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onRequestSetFreeDVModeMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onRequestStartStopKeyerMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onGetKeyerStateMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onRadioSettingsMessage_);
    registerMessageHandler(this, &UserInterfaceTask::onIpAddressAssignedMessage_);
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

    // Disable all LEDs as we're fully up now.
    ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, false);
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
    publish(&msg);
}

void UserInterfaceTask::onTaskSleep_()
{
    isActive_ = false;

    // Enable all LEDs so the user knows we're shutting down. They'll turn off
    // once the ESP32 enters deep sleep
    ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD, true);
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
    publish(&msg);
    msg.ledState = false;
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
    publish(&msg);
    msg.led = ezdv::driver::SetLedStateMessage::LedLabel::SYNC;
    publish(&msg);
    
    // Send goodbye message to beeper. The extra spaces are to give a bit more timing
    // so the beeper doesn't step on itself.
    audio::ClearBeeperTextMessage* clearBeeperMessage = new audio::ClearBeeperTextMessage();
    publish(clearBeeperMessage);
    delete clearBeeperMessage;

    audio::SetBeeperTextMessage* beeperMessage = new audio::SetBeeperTextMessage("  73  ");
    publish(beeperMessage);
    delete beeperMessage;
}

void UserInterfaceTask::onButtonShortPressedMessage_(DVTask* origin, driver::ButtonShortPressedMessage* message)
{
    if (isActive_)
    {
        if (voiceKeyerRunning_)
        {
            // Pushing any key stops the voice keyer
            audio::RequestStartStopKeyerMessage vkRequest(false);
            post(&vkRequest);
        }

        switch(message->button)
        {
            case driver::ButtonLabel::PTT:
            {
                audio::RequestTxMessage msg;
                publish(&msg);
                break;
            }
            case driver::ButtonLabel::MODE:
            {
                // Processing will happen on button release.
                break;
            }
            case driver::ButtonLabel::VOL_UP:
                volIncrement_ = 1;
                updateVolumeCommon_(nullptr);
                break;
            case driver::ButtonLabel::VOL_DOWN:
                volIncrement_ = -1;
                updateVolumeCommon_(nullptr);
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
            sleepPending_ = true;
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
                if (!voiceKeyerRunning_)
                {
                    // Only disable TX if the keyer is not currently running.
                    // Otherwise, the keyer is responsible for triggering TX.
                    audio::RequestRxMessage msg;
                    publish(&msg);
                }
                break;
            }
            case driver::ButtonLabel::MODE:
                if (!sleepPending_)
                {
                    if (isTransmitting_ && voiceKeyerEnabled_)
                    {
                        // PTT + Mode = enable voice keyer
                        audio::RequestStartStopKeyerMessage vkRequest(true);
                        post(&vkRequest);
                    }
                    else
                    {
                        int tmpMode = (int)currentMode_ + 1;
                        if (tmpMode == audio::MAX_FREEDV_MODES)
                        {
                            tmpMode = audio::ANALOG;
                        }

                        audio::RequestSetFreeDVModeMessage request((audio::FreeDVMode)tmpMode);
                        post(&request);
                    }
                }
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
    if (isActive_)
    {
        // Enable/disable sync LED as appropriate
        driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::SYNC, message->syncState);
        publish(ledMessage);
        delete ledMessage;
    }
}

void UserInterfaceTask::onLeftChannelVolumeMessage_(DVTask* origin, storage::LeftChannelVolumeMessage* message)
{
    leftVolume_ = message->volume;
}

void UserInterfaceTask::onRightChannelVolumeMessage_(DVTask* origin, storage::RightChannelVolumeMessage* message)
{
    rightVolume_ = message->volume;
}

void UserInterfaceTask::updateVolumeCommon_(DVTimer*)
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
    if (isActive_)
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
}

void UserInterfaceTask::onRadioStateChange_(DVTask* origin, network::RadioConnectionStatusMessage* message)
{
    radioStatus_ = message->state;
}

void UserInterfaceTask::flashNetworkLight_(DVTimer*)
{
    if (isActive_)
    {
        netLedStatus_ = radioStatus_ || !netLedStatus_;
        
        driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::NETWORK, netLedStatus_);
        publish(ledMessage);
        delete ledMessage;
    }
}

void UserInterfaceTask::onRequestTxMessage_(DVTask* origin, audio::RequestTxMessage* message)
{
    startTx_();
}

void UserInterfaceTask::onRequestRxMessage_(DVTask* origin, audio::RequestRxMessage* message)
{
    stopTx_(nullptr);
}

void UserInterfaceTask::onVoiceKeyerSettingsMessage_(
    DVTask* origin, storage::VoiceKeyerSettingsMessage* message)
{
    voiceKeyerEnabled_ = message->enabled;
}

void UserInterfaceTask::onVoiceKeyerCompleteMessage_(DVTask* origin, audio::VoiceKeyerCompleteMessage* message)
{
    voiceKeyerRunning_ = false;
}

void UserInterfaceTask::stopTx_(DVTimer*)
{
    // Stop time out timer (TOT)
    timeOutTimer_.stop();
    
    isTransmitting_ = false;

    // Switch FreeDV to RX mode. Note that we need to wait for the TransmitComplete message to come back before
    // we can actually stop the TX LEDs.
    audio::FreeDVSetPTTStateMessage* pttStateMessage = new audio::FreeDVSetPTTStateMessage(false);
    publish(pttStateMessage);
    delete pttStateMessage;

    auto result = waitFor<audio::TransmitCompleteMessage>(pdMS_TO_TICKS(1000), nullptr);
    delete result;

    // Disable LED and LED NPN so the radio itself stops transmitting
    driver::SetLedStateMessage* ledMessage = new driver::SetLedStateMessage(driver::SetLedStateMessage::PTT_NPN, false);
    publish(ledMessage);
    ledMessage->led = driver::SetLedStateMessage::PTT;
    publish(ledMessage);
    delete ledMessage;
}

void UserInterfaceTask::startTx_()
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

    // Start time out timer (TOT)
    timeOutTimer_.start(true);
}

void UserInterfaceTask::onADCOverload_(DVTask* origin, driver::OverloadStateMessage* message)
{
    if (isActive_)
    {
        // We only want to light up the Overload LED if the currently active channel
        // is overloading (e.g. left channel if transmitting, right on receive)
        bool overloadLedLit = 
            (isTransmitting_ && message->leftChannel) || 
            (!isTransmitting_ && message->rightChannel);

        driver::SetLedStateMessage ledMessage(driver::SetLedStateMessage::OVERLOAD, overloadLedLit);
        publish(&ledMessage);
    }
}

void UserInterfaceTask::onHeadsetButtonPressed_(DVTask* origin, driver::HeadsetButtonPressMessage* message)
{
    if (allowHeadsetPtt_)
    {
        if (voiceKeyerRunning_)
        {
            // Pushing any key stops the voice keyer
            audio::RequestStartStopKeyerMessage vkRequest(false);
            post(&vkRequest);
        }
        else
        {
            if (!isTransmitting_)
            {
                audio::RequestTxMessage msg;
                publish(&msg);
            }
            else
            {
                audio::RequestRxMessage msg;
                publish(&msg);
            }   
        }
    }
}

void UserInterfaceTask::onBatteryStateUpdate_(DVTask* origin, driver::BatteryStateMessage* message)
{
    auto recvSoc = (int)message->soc;

    if (recvSoc < lastBatteryLevel_ && recvSoc < 10)
    {
        // We dropped to the next percentage point and we're
        // almost out of power. Emit a CW "B" to the headset
        // to let the user know.
        ESP_LOGI(CURRENT_LOG_TAG, "Battery low, letting user know");
        audio::SetBeeperTextMessage* beeperMessage = new audio::SetBeeperTextMessage("B");
        publish(beeperMessage);
        delete beeperMessage;
    }

    lastBatteryLevel_ = recvSoc;
}

void UserInterfaceTask::onRequestSetFreeDVModeMessage_(DVTask* origin, audio::RequestSetFreeDVModeMessage* message)
{
    currentMode_ = (audio::FreeDVMode)message->mode;

    audio::SetFreeDVModeMessage* setModeMessage = new audio::SetFreeDVModeMessage(currentMode_);
    publish(setModeMessage);
    delete setModeMessage;

    // Send new mode to beeper. Clear any pending beeper messages to avoid having to wait
    // a while before hearing the latest mode.
    audio::ClearBeeperTextMessage* clearBeeperMessage = new audio::ClearBeeperTextMessage();
    publish(clearBeeperMessage);
    delete clearBeeperMessage;
    
    audio::SetBeeperTextMessage* beeperMessage = new audio::SetBeeperTextMessage(ModeList_[currentMode_].c_str());
    publish(beeperMessage);
    delete beeperMessage;
}

void UserInterfaceTask::onRequestStartStopKeyerMessage_(DVTask* origin, audio::RequestStartStopKeyerMessage* message)
{
    if (voiceKeyerRunning_ != message->request)
    {
        if (message->request)
        {
            audio::StartVoiceKeyerMessage vkStartMessage;
            publish(&vkStartMessage);
        }
        else
        {
            audio::StopVoiceKeyerMessage vkStopMessage;
            publish(&vkStopMessage);
        }
    }

    voiceKeyerRunning_ = message->request;
}

void UserInterfaceTask::onGetKeyerStateMessage_(DVTask* origin, audio::GetKeyerStateMessage* message)
{
    if (voiceKeyerRunning_)
    {
        audio::StartVoiceKeyerMessage vkResponse;
        origin->post(&vkResponse);
    }
    else
    {
        audio::StopVoiceKeyerMessage vkResponse;
        origin->post(&vkResponse);
    }
}

void UserInterfaceTask::onRadioSettingsMessage_(DVTask* origin, storage::RadioSettingsMessage* message)
{
    if (message->headsetPtt)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Allowing headset button to toggle PTT.");
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Disallowing headset button from toggling PTT.");
    }

    allowHeadsetPtt_ = message->headsetPtt;

    // TOT is configured in seconds, so need to convert to microseconds first.
    timeOutTimer_.changeInterval(message->timeOutTimer * 1000000);
}

void UserInterfaceTask::onIpAddressAssignedMessage_(DVTask* origin, network::IpAddressAssignedMessage* message)
{
    if (strlen(message->ip) == 0)
    {
        // Just say "N" indicating that the network is up.
        audio::SetBeeperTextMessage beeperMsg("N");
        publish(&beeperMsg);
    }
    else
    {
        // Extract the last octet of the IP address.
        std::string ip = message->ip;
        std::string lastOctet = ip.substr(ip.rfind(".") + 1);
        int lastOctetNum = atoi(lastOctet.c_str());
        char buf[6];
        sprintf(buf, " N%03d", lastOctetNum);
        
        audio::SetBeeperTextMessage beeperMsg(buf);
        publish(&beeperMsg);
    }
}

}

}