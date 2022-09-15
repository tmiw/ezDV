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
enum WifiSecurityMode { NONE, WEP, WPA, WPA2, WPA_AND_WPA2, WPA3, WPA2_AND_WPA3 };

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
        
        strncpy(ssid, ssidProvided, MAX_STR_SIZE - 1);
        strncpy(password, passwordProvided, MAX_STR_SIZE - 1);
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
    
    RadioSettingsMessageCommon(bool enabledProvided = false, char* hostProvided = "", int portProvided = 0, char* usernameProvided = "", char* passwordProvided = "")
        : DVTaskMessageBase<TYPE_ID, RadioSettingsMessageCommon<TYPE_ID>>(SETTINGS_MESSAGE) 
        , enabled(enabledProvided)
        , port(portProvided)
    { 
        memset(host, 0, MAX_STR_SIZE);
        memset(username, 0, MAX_STR_SIZE);
        memset(password, 0, MAX_STR_SIZE);
        
        strncpy(host, hostProvided, MAX_STR_SIZE - 1);
        strncpy(username, usernameProvided, MAX_STR_SIZE - 1);
        strncpy(password, passwordProvided, MAX_STR_SIZE - 1);
    }
    
    virtual ~RadioSettingsMessageCommon() = default;

    bool enabled;
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

}

}

#endif // SETTINGS_MESSAGE_H