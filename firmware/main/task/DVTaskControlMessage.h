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

#ifndef DV_TASK_CONTROL_MESSAGE_H
#define DV_TASK_CONTROL_MESSAGE_H

#include <cstdint>
#include "DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(DV_TASK_CONTROL_MESSAGE);
}

namespace ezdv
{

namespace task
{

enum DVTaskControlMessageTypes
{
    TASK_START = 1,
    TASK_SLEEP = 2,

    TASK_STARTED = 3,
    TASK_ASLEEP = 4,
    
    TASK_QUEUE_MSG = 5, // special message so that we can defer sending control messages until waitFor executes
                        // or when current handler finishes
};

template<uint32_t MSG_ID>
class TaskControlCommon : public DVTaskMessageBase<MSG_ID, TaskControlCommon<MSG_ID>>
{
public:
    TaskControlCommon()
        : DVTaskMessageBase<MSG_ID, TaskControlCommon<MSG_ID>>(DV_TASK_CONTROL_MESSAGE) { }
    virtual ~TaskControlCommon() = default;
};

using TaskStartMessage = TaskControlCommon<TASK_START>;
using TaskSleepMessage = TaskControlCommon<TASK_SLEEP>;

using TaskStartedMessage = TaskControlCommon<TASK_STARTED>;
using TaskAsleepMessage = TaskControlCommon<TASK_ASLEEP>;

}

}

#endif // DV_TASK_MESSAGE_H