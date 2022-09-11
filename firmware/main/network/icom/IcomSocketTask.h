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

#ifndef ICOM_SOCKET_TASK_H
#define ICOM_SOCKET_TASK_H

#include <string>
#include "task/DVTask.h"
#include "audio/AudioInput.h"
#include "IcomMessage.h"

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class IcomStateMachine;

class IcomSocketTask : public DVTask, public ezdv::audio::AudioInput
{
public:
    enum SocketType
    {
        CONTROL_SOCKET,
        CIV_SOCKET,
        AUDIO_SOCKET,
    };

    IcomSocketTask(SocketType socketType);
    virtual ~IcomSocketTask();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;
    
private:
    SocketType socketType_;
    IcomStateMachine* stateMachine_;
    std::string ip_;
    
    void onIcomConnectRadioMessage_(DVTask* origin, IcomConnectRadioMessage* message);
    void onIcomCIVAudioConnectionInfo_(DVTask* origin, IcomCIVAudioConnectionInfo* message);
    
    static std::string GetTaskName_(SocketType socketType);
};

}

}

}

#endif // ICOM_SOCKET_TASK_H