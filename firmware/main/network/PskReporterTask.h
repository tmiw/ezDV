/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2024 Mooneer Salem
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

#ifndef PSK_REPORTER_TASK_H
#define PSK_REPORTER_TASK_H

#include <vector>

#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "ReportingMessage.h"
#include "audio/FreeDVMessage.h"
#include "storage/SettingsMessage.h"

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

/// @brief Handles reporting to PSK Reporter.
class PskReporterTask : public DVTask
{
public:
    PskReporterTask();
    virtual ~PskReporterTask();
        
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;
    
private:
    struct SenderRecord
    {
        std::string callsign;
        uint64_t frequency;
        char snr;
        std::string mode;
        char infoSource;
        int flowTimeSeconds;
        
        SenderRecord(std::string callsign, uint64_t frequency, char snr);
        
        int recordSize();    
        void encode(char* buf);
    };

    DVTimer udpSendTimer_;
    bool reportingEnabled_;
    std::string callsign_;
    std::string gridSquare_;
    uint64_t frequencyHz_;
    bool forceReporting_;
    int reportingRefCount_;
    std::vector<SenderRecord> recordList_;
    unsigned int currentSequenceNumber_;
    unsigned int randomIdentifier_;
    std::string decodingSoftware_;

    void onReportingSettingsMessage_(DVTask* origin, storage::ReportingSettingsMessage* message);
    void onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message);
    void onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message);
    void onFreeDVCallsignReceivedMessage_(DVTask* origin, audio::FreeDVReceivedCallsignMessage* message);
    void onReportFrequencyChangeMessage_(DVTask* origin, ReportFrequencyChangeMessage* message);

    void sendPskReporterRecords_(DVTimer*);
    void startConnection_();
    void stopConnection_();

    int getRxDataSize_();    
    int getTxDataSize_();  
    void encodeReceiverRecord_(char* buf);
    void encodeSenderRecords_(char* buf);
};

}

}

#endif // PSK_REPORTER_TASK_H