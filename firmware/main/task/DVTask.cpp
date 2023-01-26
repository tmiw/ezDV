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

#include "esp_log.h"
#include "DVTask.h"

#define CURRENT_LOG_TAG ("DVTask")

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

DVTask::DVTask(std::string taskName, UBaseType_t taskPriority, uint32_t taskStackSize, BaseType_t pinnedCoreId, int32_t taskQueueSize, TickType_t taskTick)
    : taskName_(taskName)
    , taskQueueSize_(taskQueueSize)
    , taskStackSize_(taskStackSize)
    , taskPriority_(taskPriority)
    , pinnedCoreId_(pinnedCoreId)
    , taskQueue_(nullptr)
    , taskTick_(taskTick)
{
    // empty
}

DVTask::~DVTask()
{
    // We don't currently support killing ourselves.
    assert(0);
}

void DVTask::start()
{
    startTask_();
    
    TaskStartMessage message;
    post(&message);
}

void DVTask::wake()
{
    startTask_();
    
    TaskWakeMessage message;
    post(&message);
}

void DVTask::sleep()
{
    TaskSleepMessage message;
    post(&message);
}

void DVTask::startTask_()
{
    // Create task event queue
    taskQueue_ = xQueueCreate(taskQueueSize_, sizeof(MessageEntry*));
    assert(taskQueue_ != nullptr);

    // Register task start/wake/sleep handlers.
    registerMessageHandler(this, &DVTask::onTaskStart_);
    registerMessageHandler(this, &DVTask::onTaskWake_);
    registerMessageHandler(this, &DVTask::onTaskSleep_);

    auto returnValue = 
        xTaskCreatePinnedToCore((TaskFunction_t)&ThreadEntry_, taskName_.c_str(), taskStackSize_, this, taskPriority_, &taskObject_, pinnedCoreId_);
    assert(returnValue == pdPASS);
}

void DVTask::onTaskTick_()
{
    // optional, default doesn't do anything
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
    if (taskQueue_)
    {
        MessageEntry* entry = createMessageEntry_(nullptr, message);
        BaseType_t taskUnblocked = pdFALSE;

        xQueueSendToBackFromISR(taskQueue_, &entry, &taskUnblocked);

        if (taskUnblocked != pdFALSE)
        {
            portYIELD_FROM_ISR();
        }
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

    std::vector<DVTask*> tasksToPostTo;

    // Get the list of tasks to post to first so we don't deadlock.
    auto rv = xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(20));
    assert(rv == pdTRUE);

    for (auto& taskPair : SubscriberTasksByMessageType_)
    {
        if (messagePair == taskPair.first)
        {
            tasksToPostTo.push_back(taskPair.second);
        }
    }
    xSemaphoreGive(SubscriberTasksByMessageTypeSemaphore_);

    // Now that we have the list of tasks, we can take our time
    // posting the messages.
    for (auto& task : tasksToPostTo)
    {
        MessageEntry* entry = createMessageEntry_(this, message);
        task->postHelper_(entry);
    }

}

void DVTask::waitForStart(DVTask* taskToWaitFor, TickType_t ticksToWait)
{
    waitForOurs_<TaskStartedMessage>(taskToWaitFor, ticksToWait);
}

void DVTask::waitForSleep(DVTask* taskToWaitFor, TickType_t ticksToWait)
{
    waitForOurs_<TaskAsleepMessage>(taskToWaitFor, ticksToWait);
}

void DVTask::waitForAwake(DVTask* taskToWaitFor, TickType_t ticksToWait)
{
    waitForOurs_<TaskAwakeMessage>(taskToWaitFor, ticksToWait);
}

void DVTask::onTaskStart_(DVTask* origin, TaskStartMessage* message)
{
    // XXX - Slight delay in case main app is waiting for us.
    // Otherwise very fast starting/stopping tasks will finish before
    // the main app can wait, meaning that the app will never get the 
    // completion message.
    vTaskDelay(pdMS_TO_TICKS(10));

    onTaskStart_();

    ESP_LOGI(CURRENT_LOG_TAG, "Task %s started", taskName_.c_str());

    TaskStartedMessage result;
    publish(&result);
}

void DVTask::onTaskWake_(DVTask* origin, TaskWakeMessage* message)
{
    // XXX - Slight delay in case main app is waiting for us.
    // Otherwise very fast starting/stopping tasks will finish before
    // the main app can wait, meaning that the app will never get the 
    // completion message.
    vTaskDelay(pdMS_TO_TICKS(10));

    onTaskWake_();

    ESP_LOGI(CURRENT_LOG_TAG, "Task %s awake", taskName_.c_str());

    TaskAwakeMessage result;
    publish(&result);
}

void DVTask::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    // XXX - Slight delay in case main app is waiting for us.
    // Otherwise very fast starting/stopping tasks will finish before
    // the main app can wait, meaning that the app will never get the 
    // completion message.
    vTaskDelay(pdMS_TO_TICKS(10));

    onTaskSleep_();

    ESP_LOGI(CURRENT_LOG_TAG, "Task %s asleep", taskName_.c_str());

    TaskAsleepMessage result;
    publish(&result);
}

void DVTask::postHelper_(MessageEntry* entry)
{
    if (taskQueue_)
    {
        auto rv = xQueueSendToBack(taskQueue_, &entry, pdMS_TO_TICKS(100));
        assert(rv != errQUEUE_FULL);
    }
}

void DVTask::threadEntry_()
{
    UBaseType_t stackWaterMark = INT_MAX;
    
    // Run in an infinite loop, continually waiting for messages
    // and processing them.
    for (;;)
    {
        MessageEntry* entry = nullptr;
        
        int64_t ticksRemaining = taskTick_;
        while (ticksRemaining > 0)
        {
            auto tasksBegin = xTaskGetTickCount();
            
            if (xQueueReceive(taskQueue_, &entry, ticksRemaining) == pdTRUE)
            {
                //ESP_LOGI(taskName_.c_str(), "Received message %s:%ld", entry->eventBase, entry->eventId);
                auto iterPair = eventRegistrationMap_.equal_range(std::make_pair(entry->eventBase, entry->eventId));
                EventMap::iterator iter = iterPair.first;
                while (iter != iterPair.second)
                {
                    (*iter->second.first)(iter->second.second, entry->eventBase, entry->eventId, &entry);
                    iter++;
                }

                // Deallocate message now that we're done with it.
                char* entryPtr = (char*)entry;
                delete[] entryPtr;
            }
            
            ticksRemaining -= xTaskGetTickCount() - tasksBegin;
        }

        onTaskTick_();
        
        UBaseType_t newStackWaterMark = uxTaskGetStackHighWaterMark(nullptr);
        if (newStackWaterMark < stackWaterMark)
        {
            stackWaterMark = newStackWaterMark;
            //ESP_LOGI(taskName_.c_str(), "New stack high water mark of %d", newStackWaterMark);
        }
    }
}

void DVTask::ThreadEntry_(DVTask* thisObj)
{
    thisObj->threadEntry_();
}

}

}
