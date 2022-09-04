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
    // Create task's event loop.
    esp_event_loop_args_t createArgs = {
        .queue_size = taskQueueSize,
        .task_name = nullptr,
        .task_priority = 0,
        .task_stack_size = 0,
        .task_core_id = 0
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&createArgs, &taskEventLoop_));

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
    entry->size = size;
    entry->origin = origin;

    return entry;
}

void DVTask::post(DVTaskMessage* message)
{
    MessageEntry* entry = createMessageEntry_(nullptr, message);
    postHelper_(message->getEventBase(), message->getEventType(), entry);
}

void DVTask::postISR(DVTaskMessage* message)
{
    MessageEntry* entry = createMessageEntry_(nullptr, message);
    BaseType_t taskUnblocked = pdFALSE;

    ESP_ERROR_CHECK(
        esp_event_isr_post_to(
            taskEventLoop_, message->getEventBase(), message->getEventType(), &entry, sizeof(MessageEntry*),
            &taskUnblocked)
    );

    if (taskUnblocked != pdFALSE)
    {
        portYIELD_FROM_ISR();
    }
}

void DVTask::sendTo(DVTask* destination, DVTaskMessage* message)
{
    MessageEntry* entry = createMessageEntry_(this, message);
    destination->postHelper_(message->getEventBase(), message->getEventType(), entry);
}

void DVTask::publish(DVTaskMessage* message)
{
    auto messagePair = std::make_pair(message->getEventBase(), message->getEventType());

    xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(100));

    for (auto& taskPair : SubscriberTasksByMessageType_)
    {
        if (messagePair == taskPair.first)
        {
            taskPair.second->post(message);
        }
    }
    xSemaphoreGive(SubscriberTasksByMessageTypeSemaphore_);
}

void DVTask::postHelper_(esp_event_base_t event_base, int32_t event_id, MessageEntry* entry)
{
    ESP_ERROR_CHECK(esp_event_post_to(taskEventLoop_, event_base, event_id, &entry, sizeof(MessageEntry*), pdMS_TO_TICKS(2000)));
}

void DVTask::threadEntry_()
{
    // Run in an infinite loop, continually waiting for messages
    // and processing them.
    for (;;)
    {
        ESP_ERROR_CHECK(esp_event_loop_run(taskEventLoop_, pdMS_TO_TICKS(20)));
    }
}

void DVTask::ThreadEntry_(DVTask* thisObj)
{
    thisObj->threadEntry_();
}

}

}