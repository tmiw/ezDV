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

}

}

}

#endif // ICOM_MESSAGE_H