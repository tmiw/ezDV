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

#ifndef NETWORK_MESSAGE_H
#define NETWORK_MESSAGE_H

#include <cstring>

#include "task/DVTaskMessage.h"
#include "esp_wifi.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(NETWORK_MESSAGE);
}

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

enum NetworkMessageTypes
{
    WIRELESS_NETWORK_STATUS = 1,
    RADIO_CONNECTION_STATUS = 2,
    START_FILE_UPLOAD = 3,
    FILE_UPLOAD_DATA = 4,
    START_FIRMWARE_UPLOAD = 5,
    FIRMWARE_UPLOAD_DATA = 6,
    WIFI_NETWORK_LIST = 7,
    WIFI_SCAN_START = 8,
    WIFI_SCAN_STOP = 9,
    IP_ASSIGNED = 10,
};

template<uint32_t MSG_ID>
class NetworkMessageCommon : public DVTaskMessageBase<MSG_ID, NetworkMessageCommon<MSG_ID>>
{
public:
    NetworkMessageCommon(bool stateProvided = false)
        : DVTaskMessageBase<MSG_ID, NetworkMessageCommon<MSG_ID>>(NETWORK_MESSAGE)
        , state(stateProvided)
        {}
    virtual ~NetworkMessageCommon() = default;

    bool state;
};

using WirelessNetworkStatusMessage = NetworkMessageCommon<WIRELESS_NETWORK_STATUS>;
using RadioConnectionStatusMessage = NetworkMessageCommon<RADIO_CONNECTION_STATUS>;

class StartFileUploadMessage : public DVTaskMessageBase<START_FILE_UPLOAD, StartFileUploadMessage>
{
public:
    StartFileUploadMessage(int lengthProvided = 0)
        : DVTaskMessageBase<START_FILE_UPLOAD, StartFileUploadMessage>(NETWORK_MESSAGE)
        , length(lengthProvided)
        {}
    virtual ~StartFileUploadMessage() = default;

    int length;
};

class FileUploadDataMessage : public DVTaskMessageBase<FILE_UPLOAD_DATA, FileUploadDataMessage>
{
public:
    FileUploadDataMessage(char* bufProvided = nullptr, int lengthProvided = 0)
        : DVTaskMessageBase<FILE_UPLOAD_DATA, FileUploadDataMessage>(NETWORK_MESSAGE)
        , buf(bufProvided)
        , length(lengthProvided)
        {}
    virtual ~FileUploadDataMessage() = default;

    char *buf;
    int length;
};

class StartFirmwareUploadMessage : public DVTaskMessageBase<START_FIRMWARE_UPLOAD, StartFirmwareUploadMessage>
{
public:
    StartFirmwareUploadMessage()
        : DVTaskMessageBase<START_FIRMWARE_UPLOAD, StartFirmwareUploadMessage>(NETWORK_MESSAGE)
        {}
    virtual ~StartFirmwareUploadMessage() = default;
};

class FirmwareUploadDataMessage : public DVTaskMessageBase<FIRMWARE_UPLOAD_DATA, FirmwareUploadDataMessage>
{
public:
    FirmwareUploadDataMessage(char* bufProvided = nullptr, int lengthProvided = 0)
        : DVTaskMessageBase<FIRMWARE_UPLOAD_DATA, FirmwareUploadDataMessage>(NETWORK_MESSAGE)
        , buf(bufProvided)
        , length(lengthProvided)
        {}
    virtual ~FirmwareUploadDataMessage() = default;

    char *buf;
    int length;
};

class WifiNetworkListMessage : public DVTaskMessageBase<WIFI_NETWORK_LIST, WifiNetworkListMessage>
{
public:
    WifiNetworkListMessage(uint16_t numRecordsProvided = 0, wifi_ap_record_t* recordsProvided = nullptr)
        : DVTaskMessageBase<WIFI_NETWORK_LIST, WifiNetworkListMessage>(NETWORK_MESSAGE)
        , numRecords(numRecordsProvided)
        , records(recordsProvided)
        {}
    virtual ~WifiNetworkListMessage() = default;

    uint16_t numRecords;

    // Note: ownership transfers to receiving component, which is responsible for
    // deleting the memory associated with this object.
    wifi_ap_record_t* records;
};

template<uint32_t MSG_ID>
class EnableDisableMessageCommon : public DVTaskMessageBase<MSG_ID, EnableDisableMessageCommon<MSG_ID>>
{
    public:
        EnableDisableMessageCommon()
            : DVTaskMessageBase<MSG_ID, EnableDisableMessageCommon<MSG_ID>>(NETWORK_MESSAGE)
            {}
        virtual ~EnableDisableMessageCommon() = default;
};

using StartWifiScanMessage = EnableDisableMessageCommon<WIFI_SCAN_START>;
using StopWifiScanMessage = EnableDisableMessageCommon<WIFI_SCAN_STOP>;

class IpAddressAssignedMessage : public DVTaskMessageBase<IP_ASSIGNED, IpAddressAssignedMessage>
{
public:
    enum { MAX_STR_SIZE = 32 };
    
    IpAddressAssignedMessage(char* ipProvided = nullptr)
        : DVTaskMessageBase<IP_ASSIGNED, IpAddressAssignedMessage>(NETWORK_MESSAGE)
    {
        memset(ip, 0, MAX_STR_SIZE);
        
        if (ipProvided != nullptr)
        {
            strncpy(ip, ipProvided, MAX_STR_SIZE - 1);
        }
    }
    
    virtual ~IpAddressAssignedMessage() = default;
    
    char ip[MAX_STR_SIZE];
};

}

}

#endif // NETWORK_MESSAGE_H