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

#include <cstdlib>
#include <cstring>
#include <inttypes.h>

#include "esp_event.h"
#include "esp_log.h"
#include "DVTask.h"

namespace ezdv
{

namespace task
{

DVTask::PublishMap DVTask::SubscriberTasksByMessageType_;
SemaphoreHandle_t DVTask::SubscriberTasksByMessageTypeSemaphore_;

void DVTask::Initialize()
{
    SubscriberTasksByMessageTypeSemaphore_ = xSemaphoreCreateBinary();
    assert(SubscriberTasksByMessageTypeSemaphore_ != nullptr);
}

DVTask::DVTask(std::string taskName, UBaseType_t taskPriority, uint32_t taskStackSize, BaseType_t pinnedCoreId, int32_t taskQueueSize)
{
    // Create task event queue
    taskQueue_ = xQueueCreate(taskQueueSize, sizeof(MessageEntry*));
    assert(taskQueue_ != nullptr);

    // Register task start/wake/sleep handlers.
    registerMessageHandler(this, &DVTask::onTaskStart_);
    registerMessageHandler(this, &DVTask::onTaskWake_);
    registerMessageHandler(this, &DVTask::onTaskSleep_);

    auto returnValue = 
        xTaskCreatePinnedToCore((TaskFunction_t)&ThreadEntry_, taskName.c_str(), taskStackSize, this, taskPriority, &taskObject_, pinnedCoreId);
    assert(returnValue == pdPASS);
}

DVTask::~DVTask()
{
    // We don't currently support killing ourselves.
    assert(0);
}

void DVTask::start()
{
    post(new TaskStartMessage());
}

void DVTask::wake()
{
    post(new TaskWakeMessage());
}

void DVTask::sleep()
{
    post(new TaskSleepMessage());
}

DVTask::MessageEntry* DVTask::createMessageEntry_(DVTask* origin, DVTaskMessage* message)
{
    // Create object that's big enough to hold the passed-in message.
    uint32_t size = message->getSize() + sizeof(MessageEntry);
    char* messageEntryBuf = new char[size];
    assert(messageEntryBuf != nullptr);

    // Copy the message over to the object.
    MessageEntry* entry = (MessageEntry*)messageEntryBuf;
    memcpy(&entry->messageStart, message, message->getSize());

    // Fill in remaining data fields.
    entry->eventBase = message->getEventBase();
    entry->eventId = message->getEventType();
    entry->size = size;
    entry->origin = origin;

    return entry;
}

void DVTask::post(DVTaskMessage* message)
{
    MessageEntry* entry = createMessageEntry_(nullptr, message);
    postHelper_(entry);
}

void DVTask::postISR(DVTaskMessage* message)
{
    MessageEntry* entry = createMessageEntry_(nullptr, message);
    BaseType_t taskUnblocked = pdFALSE;

    xQueueSendToBackFromISR(taskQueue_, &entry, &taskUnblocked);

    if (taskUnblocked != pdFALSE)
    {
        portYIELD_FROM_ISR();
    }
}

void DVTask::sendTo(DVTask* destination, DVTaskMessage* message)
{
    MessageEntry* entry = createMessageEntry_(this, message);
    destination->postHelper_(entry);
}

void DVTask::publish(DVTaskMessage* message)
{
    auto messagePair = std::make_pair(message->getEventBase(), message->getEventType());

    xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(20));

    for (auto& taskPair : SubscriberTasksByMessageType_)
    {
        if (messagePair == taskPair.first)
        {
            taskPair.second->post(message);
        }
    }
    xSemaphoreGive(SubscriberTasksByMessageTypeSemaphore_);
}

void DVTask::postHelper_(MessageEntry* entry)
{
    auto rv = xQueueSendToBack(taskQueue_, &entry, pdMS_TO_TICKS(20));
    assert(rv == pdTRUE);
}

void DVTask::threadEntry_()
{
    // Run in an infinite loop, continually waiting for messages
    // and processing them.
    for (;;)
    {
        MessageEntry* entry = nullptr;
        while (xQueueReceive(taskQueue_, &entry, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            auto iterPair = eventRegistrationMap_.equal_range(std::make_pair(entry->eventBase, entry->eventId));
            EventMap::iterator iter = iterPair.first;
            while (iter != iterPair.second)
            {
                (*iter->second.first)(iter->second.second, entry->eventBase, entry->eventId, &entry);
                iter++;
            }

            // Deallocate message now that we're done with it.
            delete entry;
        }
    }
}

void DVTask::ThreadEntry_(DVTask* thisObj)
{
    thisObj->threadEntry_();
}

}

}