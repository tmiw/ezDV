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

#include "task/DVTaskMessage.h"

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

}

}

#endif // LED_MESSAGE_H