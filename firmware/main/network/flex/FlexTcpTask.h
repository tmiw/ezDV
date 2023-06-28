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

#ifndef FLEX_TCP_TASK_H
#define FLEX_TCP_TASK_H

#include <sstream>
#include <map>
#include <functional>

#include "audio/FreeDVMessage.h"
#include "audio/VoiceKeyerMessage.h"
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

/// @brief Handles the main TCP/IP socket for Flex 6000 series radios.
class FlexTcpTask : public DVTask
{
public:
    FlexTcpTask();
    virtual ~FlexTcpTask();
        
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;
    virtual void onTaskTick_() override;
    
    virtual void onTaskSleep_(DVTask* origin, TaskSleepMessage* message);
    
private:
    std::stringstream inputBuffer_;
    DVTimer reconnectTimer_;
    int socket_;
    int sequenceNumber_;
    std::string ip_;
    int activeSlice_;
    bool isLSB_;
    int txSlice_;
    bool isTransmitting_;

    std::map<int, std::string, std::less<int>, util::PSRamAllocator<std::pair<const int, std::string> > > sliceFrequencies_;
    std::map<int, bool, std::less<int>, util::PSRamAllocator<std::pair<const int, bool> > > activeSlices_;
    
    using FilterPair_ = std::pair<int, int>; // Low/high cut in Hz.
    std::vector<FilterPair_, util::PSRamAllocator<FilterPair_> > filterWidths_;
    FilterPair_ currentWidth_;
    
    using HandlerMapFn_ = std::function<void(unsigned int rv, std::string message)>;
    std::map<int, HandlerMapFn_, std::less<int>, util::PSRamAllocator<std::pair<const int, HandlerMapFn_> > > responseHandlers_;
    
    void connect_();
    void disconnect_();
    void socketFinalCleanup_(bool reconnect);

    void initializeWaveform_();
    void createWaveform_(std::string name, std::string shortName, std::string underlyingMode);
    void cleanupWaveform_();
    
    void sendRadioCommand_(std::string command);
    void sendRadioCommand_(std::string command, std::function<void(unsigned int rv, std::string message)> fn);
    
    void processCommand_(std::string& command);
    
    void onFlexConnectRadioMessage_(DVTask* origin, FlexConnectRadioMessage* message);
    void onRequestTxMessage_(DVTask* origin, audio::RequestTxMessage* message);
    void onRequestRxMessage_(DVTask* origin, audio::RequestRxMessage* message);

    void onFreeDVModeChange_(DVTask* origin, audio::SetFreeDVModeMessage* message);
    void setFilter_(int low, int high);
    
    // Spot handling
    void onFreeDVReceivedCallsignMessage_(DVTask* origin, audio::FreeDVReceivedCallsignMessage* message);
};

}

}

}

#endif // FLEX_TCP_TASK_H