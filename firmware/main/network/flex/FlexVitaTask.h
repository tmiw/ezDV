/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
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

#ifndef FLEX_VITA_TASK_H
#define FLEX_VITA_TASK_H

#include <ctime>
#include <deque>
#include <sys/socket.h>

#include "audio/AudioInput.h"
#include "network/NetworkMessage.h"
#include "network/ReportingMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "util/PSRamAllocator.h"

#include "FlexMessage.h"

namespace ezdv
{

namespace network
{
    
namespace flex
{

using namespace ezdv::task;

/// @brief Handles the VITA UDP socket for Flex 6000 series radios.
class FlexVitaTask : public DVTask, public audio::AudioInput
{
public:
    enum { VITA_PORT = 4992 }; // Hardcoding VITA port because we can only handle one slice at a time.
    
    FlexVitaTask();
    virtual ~FlexVitaTask();
        
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;
    virtual void onTaskTick_() override;
    
private:
    struct sockaddr_in radioAddress_;
    std::deque<float, util::PSRamAllocator<float> > inputVector_;
    DVTimer packetReadTimer_;
    int socket_;
    std::string ip_;
    uint32_t rxStreamId_;
    uint32_t txStreamId_;
    uint32_t audioSeqNum_;
    time_t currentTime_;
    int timeFracSeq_;
    bool audioEnabled_;
    
    // Resampler buffers
    float* downsamplerInBuf_;
    short* downsamplerOutBuf_;
    short* upsamplerInBuf_;
    float* upsamplerOutBuf_;
    
    void openSocket_();
    void disconnect_();
    
    void readPendingPackets_();
    
    void generateVitaPackets_(audio::AudioInput::ChannelLabel channel, uint32_t streamId);
    
    void onFlexConnectRadioMessage_(DVTask* origin, FlexConnectRadioMessage* message);
    void onReceiveVitaMessage_(DVTask* origin, ReceiveVitaMessage* message);
    void onSendVitaMessage_(DVTask* origin, SendVitaMessage* message);

    // Listen to EnableReportingMessage and DisableReportingMessage
    // so that we can actually start sending audio to SmartSDR.
    // This is so that users don't end up with old audio being sent to
    // them as soon as they choose FDVU/FDVL.
    void onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message);
    void onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message);
};

}

}

}

#endif // FLEX_TCP_TASK_H