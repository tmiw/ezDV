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

#ifndef ICOM_MESSAGE_H
#define ICOM_MESSAGE_H

#include <cstring>
#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(ICOM_MESSAGE);
}

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

enum IcomMessageTypes
{
    CIV_AUDIO_CONN_INFO = 1,
    CONNECT_RADIO = 2,
};

class IcomCIVAudioConnectionInfo : public DVTaskMessageBase<CIV_AUDIO_CONN_INFO, IcomCIVAudioConnectionInfo>
{
public:
    IcomCIVAudioConnectionInfo(
        int localCivPortProvided = 0, 
        int remoteCivPortProvided = 0, 
        int civSocketProvided = 0,
        int localAudioPortProvided = 0, 
        int remoteAudioPortProvided = 0,
        int audioSocketProvided = 0)
        : DVTaskMessageBase<CIV_AUDIO_CONN_INFO, IcomCIVAudioConnectionInfo>(ICOM_MESSAGE)
        , localCivPort(localCivPortProvided)
        , remoteCivPort(remoteCivPortProvided)
        , civSocket(civSocketProvided)
        , localAudioPort(localAudioPortProvided)
        , remoteAudioPort(remoteAudioPortProvided)
        , audioSocket(audioSocketProvided)
        {}
    virtual ~IcomCIVAudioConnectionInfo() = default;

    int localCivPort;
    int remoteCivPort;
    int civSocket;
    int localAudioPort;
    int remoteAudioPort; 
    int audioSocket;
};

class IcomConnectRadioMessage : public DVTaskMessageBase<CONNECT_RADIO, IcomConnectRadioMessage>
{
public:
    static const int STR_SIZE = 32;
    
    IcomConnectRadioMessage(
        char* ipProvided = nullptr,
        int portProvided = 0,
        char* usernameProvided = nullptr,
        char* passwordProvided = nullptr)
        : DVTaskMessageBase<CONNECT_RADIO, IcomConnectRadioMessage>(ICOM_MESSAGE)
        , port(portProvided)
    {
        memset(ip, 0, STR_SIZE);
        memset(username, 0, STR_SIZE);
        memset(password, 0, STR_SIZE);
        
        if (ipProvided != nullptr)
        {
            strncpy(ip, ipProvided, STR_SIZE - 1);
        }
        
        if (usernameProvided != nullptr)
        {
            strncpy(username, usernameProvided, STR_SIZE - 1);
        }
        
        if (passwordProvided != nullptr)
        {
            strncpy(password, passwordProvided, STR_SIZE - 1);
        }
        
        ip[STR_SIZE - 1] = 0;
        username[STR_SIZE - 1] = 0;
        password[STR_SIZE - 1] = 0;
    }
    virtual ~IcomConnectRadioMessage() = default;

    char ip[STR_SIZE];
    int port;
    char username[STR_SIZE];
    char password[STR_SIZE];
};

}

}

}

#endif // ICOM_MESSAGE_H