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

#ifndef SETTINGS_MESSAGE_H
#define SETTINGS_MESSAGE_H

#include <cstring>

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(SETTINGS_MESSAGE);
}

namespace ezdv
{

namespace storage
{

using namespace ezdv::task;

enum SettingsMessageTypes
{
    LEFT_CHANNEL_VOLUME = 1,
    RIGHT_CHANNEL_VOLUME = 2,
    SET_LEFT_CHANNEL_VOLUME = 3,
    SET_RIGHT_CHANNEL_VOLUME = 4,
    
    WIFI_SETTINGS = 5,
    SET_WIFI_SETTINGS = 6,
    REQUEST_WIFI_SETTINGS = 7,
    WIFI_SETTINGS_SAVED = 8,
    
    RADIO_SETTINGS = 9,
    SET_RADIO_SETTINGS = 10,
    REQUEST_RADIO_SETTINGS = 11,
    RADIO_SETTINGS_SAVED = 12,

    VOICE_KEYER_SETTINGS = 13,
    SET_VOICE_KEYER_SETTINGS = 14,
    REQUEST_VOICE_KEYER_SETTINGS = 15,
    VOICE_KEYER_SETTINGS_SAVED = 16,

    REPORTING_SETTINGS = 17,
    SET_REPORTING_SETTINGS = 18,
    REQUEST_REPORTING_SETTINGS = 19,
    REPORTING_SETTINGS_SAVED = 20,
    
    LED_BRIGHTNESS_SETTINGS = 21,
    SET_LED_BRIGHTNESS_SETTINGS = 22,
    REQUEST_LED_BRIGHTNESS_SETTINGS = 23,
    LED_BRIGHTNESS_SETTINGS_SAVED = 24,
    
    REQUEST_VOLUME_SETTINGS = 25,
};

template<uint32_t TYPE_ID>
class VolumeMessageCommon : public DVTaskMessageBase<TYPE_ID, VolumeMessageCommon<TYPE_ID>>
{
public:
    VolumeMessageCommon(int8_t volProvided = 0)
        : DVTaskMessageBase<TYPE_ID, VolumeMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
        , volume(volProvided) { }
    virtual ~VolumeMessageCommon() = default;

    int8_t volume;
};

enum WifiMode { ACCESS_POINT, CLIENT };
enum WifiSecurityMode { NONE, WEP, WPA, WPA2, WPA_AND_WPA2, /*WPA3, WPA2_AND_WPA3*/ };

template<uint32_t TYPE_ID>
class WifiSettingsMessageCommon : public DVTaskMessageBase<TYPE_ID, WifiSettingsMessageCommon<TYPE_ID>>
{
public:
    enum { MAX_STR_SIZE = 32 };
    
    WifiSettingsMessageCommon(bool enabledProvided = false, WifiMode modeProvided = ACCESS_POINT, WifiSecurityMode securityProvided = NONE, int channelProvided = 0, char* ssidProvided = "", char* passwordProvided = "")
        : DVTaskMessageBase<TYPE_ID, WifiSettingsMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
        , enabled(enabledProvided)
        , mode(modeProvided)
        , security(securityProvided)
        , channel(channelProvided)
    { 
        memset(ssid, 0, MAX_STR_SIZE);
        memset(password, 0, MAX_STR_SIZE);
        
        if (ssidProvided != nullptr)
        {
            strncpy(ssid, ssidProvided, MAX_STR_SIZE - 1);
        }
        if (passwordProvided != nullptr)
        {
            strncpy(password, passwordProvided, MAX_STR_SIZE - 1);
        }
    }
    
    virtual ~WifiSettingsMessageCommon() = default;

    bool enabled;
    WifiMode mode;
    WifiSecurityMode security; // ignored if not access point
    int channel; // ignored if not access point
    char ssid[MAX_STR_SIZE];
    char password[MAX_STR_SIZE];
};

template<uint32_t TYPE_ID>
class RadioSettingsMessageCommon : public DVTaskMessageBase<TYPE_ID, RadioSettingsMessageCommon<TYPE_ID>>
{
public:
    enum { MAX_STR_SIZE = 32 };
    
    RadioSettingsMessageCommon(bool headsetPttProvided = false, int timeOutTimerProvided = 0, bool enabledProvided = false, int typeProvided = 0, char* hostProvided = "", int portProvided = 0, char* usernameProvided = "", char* passwordProvided = "")
        : DVTaskMessageBase<TYPE_ID, RadioSettingsMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
        , headsetPtt(headsetPttProvided)
        , timeOutTimer(timeOutTimerProvided)
        , enabled(enabledProvided)
        , type(typeProvided)
        , port(portProvided)
    { 
        memset(host, 0, MAX_STR_SIZE);
        memset(username, 0, MAX_STR_SIZE);
        memset(password, 0, MAX_STR_SIZE);

        if (hostProvided != nullptr)
        {
            strncpy(host, hostProvided, MAX_STR_SIZE - 1);
        }
        if (usernameProvided != nullptr)
        {
            strncpy(username, usernameProvided, MAX_STR_SIZE - 1);
        }
        if (passwordProvided != nullptr)
        {
            strncpy(password, passwordProvided, MAX_STR_SIZE - 1);
        }
    }
    
    virtual ~RadioSettingsMessageCommon() = default;

    bool headsetPtt;
    int timeOutTimer;
    bool enabled;
    int type;
    int port;
    char host[MAX_STR_SIZE];
    char username[MAX_STR_SIZE];
    char password[MAX_STR_SIZE];
};

using LeftChannelVolumeMessage = VolumeMessageCommon<LEFT_CHANNEL_VOLUME>;
using RightChannelVolumeMessage = VolumeMessageCommon<RIGHT_CHANNEL_VOLUME>;
using SetLeftChannelVolumeMessage = VolumeMessageCommon<SET_LEFT_CHANNEL_VOLUME>;
using SetRightChannelVolumeMessage = VolumeMessageCommon<SET_RIGHT_CHANNEL_VOLUME>;

using WifiSettingsMessage = WifiSettingsMessageCommon<WIFI_SETTINGS>;
using SetWifiSettingsMessage = WifiSettingsMessageCommon<SET_WIFI_SETTINGS>;

using RadioSettingsMessage = RadioSettingsMessageCommon<RADIO_SETTINGS>;
using SetRadioSettingsMessage = RadioSettingsMessageCommon<SET_RADIO_SETTINGS>;

template<uint32_t TYPE_ID>
class VoiceKeyerSettingsMessageCommon : public DVTaskMessageBase<TYPE_ID, VoiceKeyerSettingsMessageCommon<TYPE_ID>>
{
public:
    VoiceKeyerSettingsMessageCommon(bool enabledProvided = false, int timesToTransmitProvided = 0, int secondsToWaitProvided = 0)
        : DVTaskMessageBase<TYPE_ID, VoiceKeyerSettingsMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
        , enabled(enabledProvided)
        , timesToTransmit(timesToTransmitProvided)
        , secondsToWait(secondsToWaitProvided)
    { 
        // empty
    }
    
    virtual ~VoiceKeyerSettingsMessageCommon() = default;

    bool enabled;
    int timesToTransmit;
    int secondsToWait;
};

using VoiceKeyerSettingsMessage = VoiceKeyerSettingsMessageCommon<VOICE_KEYER_SETTINGS>;
using SetVoiceKeyerSettingsMessage = VoiceKeyerSettingsMessageCommon<SET_VOICE_KEYER_SETTINGS>;

template<uint32_t TYPE_ID>
class ReportingSettingsMessageCommon : public DVTaskMessageBase<TYPE_ID, ReportingSettingsMessageCommon<TYPE_ID>>
{
public:
    enum { MAX_STR_SIZE = 16 };

    ReportingSettingsMessageCommon(char* callsignProvided = "", char* gridSquareProvided = "")
        : DVTaskMessageBase<TYPE_ID, ReportingSettingsMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
    { 
        memset(callsign, 0, sizeof(callsign));
        if (callsignProvided != nullptr)
        {
            strncpy(callsign, callsignProvided, sizeof(callsign) - 1);
        }

        memset(gridSquare, 0, sizeof(gridSquare));
        if (gridSquareProvided != nullptr)
        {
            strncpy(gridSquare, gridSquareProvided, sizeof(gridSquare) - 1);
        }
    }
    
    virtual ~ReportingSettingsMessageCommon() = default;

    char callsign[MAX_STR_SIZE];
    char gridSquare[MAX_STR_SIZE];
};

using ReportingSettingsMessage = ReportingSettingsMessageCommon<REPORTING_SETTINGS>;
using SetReportingSettingsMessage = ReportingSettingsMessageCommon<SET_REPORTING_SETTINGS>;

template<uint32_t TYPE_ID>
class LedBrightnessMessageCommon : public DVTaskMessageBase<TYPE_ID, LedBrightnessMessageCommon<TYPE_ID>>
{
public:
    LedBrightnessMessageCommon(int dutyCycleProvided = 0)
        : DVTaskMessageBase<TYPE_ID, LedBrightnessMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
        , dutyCycle(dutyCycleProvided) { }
    virtual ~LedBrightnessMessageCommon() = default;

    int dutyCycle;
};

using LedBrightnessSettingsMessage = LedBrightnessMessageCommon<LED_BRIGHTNESS_SETTINGS>;
using SetLedBrightnessSettingsMessage = LedBrightnessMessageCommon<SET_LED_BRIGHTNESS_SETTINGS>;

template<uint32_t TYPE_ID>
class RequesSettingsMessageCommon : public DVTaskMessageBase<TYPE_ID, RequesSettingsMessageCommon<TYPE_ID>>
{
public:
    RequesSettingsMessageCommon()
        : DVTaskMessageBase<TYPE_ID, RequesSettingsMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
    { }
    virtual ~RequesSettingsMessageCommon() = default;
};

using RequestWifiSettingsMessage = RequesSettingsMessageCommon<REQUEST_WIFI_SETTINGS>;
using WifiSettingsSavedMessage = RequesSettingsMessageCommon<WIFI_SETTINGS_SAVED>;

using RequestRadioSettingsMessage = RequesSettingsMessageCommon<REQUEST_RADIO_SETTINGS>;
using RadioSettingsSavedMessage = RequesSettingsMessageCommon<RADIO_SETTINGS_SAVED>;

using RequestVoiceKeyerSettingsMessage = RequesSettingsMessageCommon<REQUEST_VOICE_KEYER_SETTINGS>;
using VoiceKeyerSettingsSavedMessage = RequesSettingsMessageCommon<VOICE_KEYER_SETTINGS_SAVED>;

using RequestReportingSettingsMessage = RequesSettingsMessageCommon<REQUEST_REPORTING_SETTINGS>;
using ReportingSettingsSavedMessage = RequesSettingsMessageCommon<REPORTING_SETTINGS_SAVED>;

using RequestLedBrightnessSettingsMessage = RequesSettingsMessageCommon<REQUEST_LED_BRIGHTNESS_SETTINGS>;
using LedBrightnessSettingsSavedMessage = RequesSettingsMessageCommon<LED_BRIGHTNESS_SETTINGS_SAVED>;

using RequestVolumeSettingsMessage = RequesSettingsMessageCommon<REQUEST_VOLUME_SETTINGS>;

}

}

#endif // SETTINGS_MESSAGE_H