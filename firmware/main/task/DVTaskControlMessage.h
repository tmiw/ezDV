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
#include "esp_event.h"

extern "C"
{
    ESP_EVENT_DECLARE_BASE(DV_TASK_CONTROL_MESSAGE);
}

namespace ezdv
{

namespace task
{

enum DVTaskControlMessageTypes
{
    TASK_START = 1,
    TASK_WAKE = 2,
    TASK_SLEEP = 3,
    TASK_SHUTDOWN = 4, // Not currently used, just here for the future
};

class TaskStartMessage : public DVTaskMessageBase<TASK_START>
{
public:
    TaskStartMessage()
        : DVTaskMessageBase<TASK_START>(DV_TASK_CONTROL_MESSAGE) { }
    virtual ~TaskStartMessage() = default;
};

class TaskWakeMessage : public DVTaskMessageBase<TASK_WAKE>
{
public:
    TaskWakeMessage()
        : DVTaskMessageBase<TASK_WAKE>(DV_TASK_CONTROL_MESSAGE) { }
    virtual ~TaskWakeMessage() = default;
};

class TaskSleepMessage : public DVTaskMessageBase<TASK_SLEEP>
{
public:
    TaskSleepMessage()
        : DVTaskMessageBase<TASK_SLEEP>(DV_TASK_CONTROL_MESSAGE) { }
    virtual ~TaskSleepMessage() = default;
};

}

}

#endif // DV_TASK_MESSAGE_H