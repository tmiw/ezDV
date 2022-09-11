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

#include "IcomSocketTask.h"
#include "IcomAudioStateMachine.h"
#include "IcomControlStateMachine.h"
#include "IcomCIVStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

IcomSocketTask::IcomSocketTask(SocketType socketType)
    : DVTask(GetTaskName_(socketType), 10 /* TBD */, 12000, tskNO_AFFINITY, 100, pdMS_TO_TICKS(20))
    , socketType_(socketType)
{
    switch(socketType)
    {
        case CONTROL_SOCKET:
            stateMachine_ = new IcomControlStateMachine(this);
            break;
        case CIV_SOCKET:
            stateMachine_ = new IcomCIVStateMachine(this);
            break;
        case AUDIO_SOCKET:
            stateMachine_ = new IcomAudioStateMachine(this);
            break;
        default:
            assert(0);
    }
    
    assert(stateMachine_ != nullptr);
    
    registerMessageHandler(this, &IcomSocketTask::onIcomConnectRadioMessage_);
    registerMessageHandler(this, &IcomSocketTask::onIcomCIVAudioConnectionInfo_);
}

IcomSocketTask::~IcomSocketTask()
{
    delete stateMachine_;
}

void IcomSocketTask::onTaskStart_()
{
    // empty, must wait for outside to tell us to connect
}

void IcomSocketTask::onTaskWake_()
{
    // empty, must wait for outside to tell us to connect
}

void IcomSocketTask::onTaskSleep_()
{
    // TBD -- disconnect logic
}

void IcomSocketTask::onTaskTick_()
{
    stateMachine_->readPendingPackets();
}

void IcomSocketTask::onIcomConnectRadioMessage_(DVTask* origin, IcomConnectRadioMessage* message)
{
    if (socketType_ == CONTROL_SOCKET)
    {
        stateMachine_->start(message->ip, message->port, message->username, message->password);
    }
    else
    {
        // Save IP For later.
        ip_ = message->ip;
    }
}

void IcomSocketTask::onIcomCIVAudioConnectionInfo_(DVTask* origin, IcomCIVAudioConnectionInfo* message)
{
    if (socketType_ == AUDIO_SOCKET)
    {
        ESP_LOGI("XXX", "starting audio socket");
        stateMachine_->start(ip_, message->remoteAudioPort, "", "", message->localAudioPort);
    }
    else if (socketType_ == CIV_SOCKET)
    {
        ESP_LOGI("XXX", "starting CIV socket");
        stateMachine_->start(ip_, message->remoteCivPort, "", "", message->localCivPort);
    }
}

std::string IcomSocketTask::GetTaskName_(SocketType socketType)
{
    std::string prefix = "IcomSocketTask/";
    
    switch(socketType)
    {
        case CONTROL_SOCKET:
            prefix += "Control";
            break;
        case CIV_SOCKET:
            prefix += "CIV";
            break;
        case AUDIO_SOCKET:
            prefix += "Audio";
            break;
        default:
            assert(0);
    }
    
    return prefix;
}

}

}

}