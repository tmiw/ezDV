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

#ifndef REPORTING_MESSAGE_H
#define REPORTING_MESSAGE_H

#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(REPORTING_MESSAGE);
}

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

enum ReportingMessageTypes
{
    ENABLE_REPORTING = 1,
    DISABLE_REPORTING = 2,
    REPORT_FREQUENCY_CHANGE = 3,
};

// These don't require arguments.
template<uint32_t MSG_ID>
class ReportingZeroArgMessageCommon : public DVTaskMessageBase<MSG_ID, ReportingZeroArgMessageCommon<MSG_ID>>
{
public:
    ReportingZeroArgMessageCommon()
        : DVTaskMessageBase<MSG_ID, ReportingZeroArgMessageCommon<MSG_ID>>(REPORTING_MESSAGE)
        {}
    virtual ~ReportingZeroArgMessageCommon() = default;
};

using EnableReportingMessage = ReportingZeroArgMessageCommon<ENABLE_REPORTING>;
using DisableReportingMessage = ReportingZeroArgMessageCommon<DISABLE_REPORTING>;

class ReportFrequencyChangeMessage : public DVTaskMessageBase<REPORT_FREQUENCY_CHANGE, ReportFrequencyChangeMessage>
{
public:
    ReportFrequencyChangeMessage(uint64_t frequencyHzProvided = 0)
        : DVTaskMessageBase<REPORT_FREQUENCY_CHANGE, ReportFrequencyChangeMessage>(REPORTING_MESSAGE)
        , frequencyHz(frequencyHzProvided)
        {}

    virtual ~ReportFrequencyChangeMessage() = default;

    uint64_t frequencyHz;
};

}

}

#endif // REPORTING_MESSAGE