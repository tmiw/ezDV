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
#include "network/NetworkMessage.h"

namespace ezdv
{

namespace network
{

namespace icom
{

IcomSocketTask::IcomSocketTask(SocketType socketType)
    : DVTask(GetTaskName_(socketType), 10 /* TBD */, 16384, tskNO_AFFINITY, 1024, pdMS_TO_TICKS(20))
    , ezdv::audio::AudioInput(1, 1)
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
    registerMessageHandler(this, &IcomSocketTask::onRadioDisconnectedMessage_);
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
    // empty
}

void IcomSocketTask::onTaskSleep_()
{
    // empty
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
    if (message->remoteCivPort == 0 && message->remoteAudioPort == 0)
    {
        if (socketType_ == AUDIO_SOCKET)
        {
            // Report connection termination
            ezdv::network::RadioConnectionStatusMessage response(false);
            publish(&response);
        }

        // Transition to idle state
        if (socketType_ == AUDIO_SOCKET || socketType_ == CIV_SOCKET)
        {
            stateMachine_->reset();
        }
    }
    else
    {
        if (socketType_ == AUDIO_SOCKET)
        {
            // Report successful connection
            ezdv::network::RadioConnectionStatusMessage response(true);
            publish(&response);
            
            stateMachine_->start(ip_, message->remoteAudioPort, "", "", message->localAudioPort);
        }
        else if (socketType_ == CIV_SOCKET)
        {
            stateMachine_->start(ip_, message->remoteCivPort, "", "", message->localCivPort);
        }
    }
}

void IcomSocketTask::onRadioDisconnectedMessage_(DVTask* origin, DisconnectedRadioMessage* message)
{
    // Call task sleep actions to trigger reporting of task finishing sleep.
    DVTask::onTaskSleep_(nullptr, nullptr);
}

void IcomSocketTask::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    // Transition to the null state. This should trigger state-specific cleanup.
    if (stateMachine_->getCurrentState() == nullptr)
    {
        DVTask::onTaskSleep_(nullptr, nullptr);
    }
    else
    {
        stateMachine_->reset();
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
