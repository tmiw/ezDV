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

DVTask::DVTask(const char* taskName, UBaseType_t taskPriority, uint32_t taskStackSize, BaseType_t pinnedCoreId, int32_t taskQueueSize, TickType_t taskTick)
    : taskName_(taskName)
    , taskObject_(nullptr)
    , taskQueueSize_(taskQueueSize)
    , taskStackSize_(taskStackSize)
    , taskPriority_(taskPriority)
    , pinnedCoreId_(pinnedCoreId)
    , taskQueue_(nullptr)
    , taskTick_(taskTick)
{
    // Register task start/wake/sleep handlers.
    registerMessageHandler(this, &DVTask::onTaskStart_);
    registerMessageHandler(this, &DVTask::onTaskSleep_);
    registerMessageHandler(this, &DVTask::onTaskQueueMessage_);
}

DVTask::~DVTask()
{
    assert(taskObject_ == nullptr);

    while (eventRegistrationMap_.size() > 0)
    {
        auto iter = eventRegistrationMap_.begin();
        unregisterMessageHandler(iter->second.second);
    }
}

void DVTask::start()
{
    startTask_();
    
    TaskStartMessage message;
    post(&message);
}

void DVTask::sleep()
{
    TaskSleepMessage message;
    post(&message);
}

void DVTask::start(DVTask* taskToStart, TickType_t ticksToWait)
{
    TaskStartMessage startMessage;
    
    taskToStart->startTask_();
    
    if (ticksToWait > 0)
    {
        auto entry = createMessageEntry_(this, &startMessage);
        TaskQueueMessage message(taskToStart, entry);
        post(&message);
        
        waitForOurs_<TaskStartedMessage>(taskToStart, ticksToWait);
    }
    else
    {
        post(&startMessage);
    }
}

void DVTask::sleep(DVTask* taskToSleep, TickType_t ticksToWait)
{
    if (taskToSleep != nullptr && taskToSleep->isAwake())
    {
        TaskSleepMessage sleepMessage;
            
        if (ticksToWait > 0)
        {
            auto entry = createMessageEntry_(this, &sleepMessage);
            TaskQueueMessage message(taskToSleep, entry);
            post(&message);
            
            waitForOurs_<TaskAsleepMessage>(taskToSleep, ticksToWait);
        }
        else
        {
            post(&sleepMessage);
        }
    }
}

void DVTask::unregisterMessageHandler(MessageHandlerHandle handler)
{
    FnPtrStorage* handlerPtr = (FnPtrStorage*)handler;
    
    // Unregister task specific handler.
    auto iter = eventRegistrationMap_.begin();
    EventIdentifierPair key;
    for (; iter != eventRegistrationMap_.end(); iter++)
    {
        if (iter->second.second == handlerPtr)
        {
            key = iter->first;
            eventRegistrationMap_.erase(iter);
            delete handlerPtr;
            break;
        }
    }
    
    // Unregister for use by publish.
    xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(100));
    if (eventRegistrationMap_.count(key) == 0)
    {
        auto iter = SubscriberTasksByMessageType_.equal_range(key);
        for (auto i = iter.first; i != iter.second; i++)
        {
            if (i->second == this)
            {
                SubscriberTasksByMessageType_.erase(i);
                break;
            }
        }
    }
    xSemaphoreGive(SubscriberTasksByMessageTypeSemaphore_);
}

void DVTask::startTask_()
{
    // Create task event queue
    taskQueue_ = xQueueCreate(taskQueueSize_, sizeof(MessageEntry*));
    assert(taskQueue_ != nullptr);

    auto returnValue = 
        xTaskCreatePinnedToCore((TaskFunction_t)&ThreadEntry_, taskName_, taskStackSize_, this, taskPriority_, &taskObject_, pinnedCoreId_);
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
    if (taskQueue_ && isAwake())
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

void DVTask::postTimer(DVTaskMessage* message)
{
    if (taskQueue_ && isAwake())
    {
        MessageEntry* entry = createMessageEntry_(nullptr, message);
        auto rv = xQueueSendToFront(taskQueue_, &entry, pdMS_TO_TICKS(100));
        if (rv == errQUEUE_FULL)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Task %s has a full queue! (maximum: %ld)", taskName_, taskQueueSize_);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        assert(rv != errQUEUE_FULL);
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
    auto rv = xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(100));
    if (rv != pdTRUE)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Could not get task list in time for %s to publish", taskName_);
        vTaskDelay(pdMS_TO_TICKS(100)); // flush debug output before crashing
        assert(rv == pdTRUE);
    }

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

void DVTask::onTaskStart_(DVTask* origin, TaskStartMessage* message)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    onTaskStart_();

    ESP_LOGI(CURRENT_LOG_TAG, "Task %s started", taskName_);

    TaskStartedMessage result;
    publish(&result);
}

void DVTask::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    onTaskSleep_();

    ESP_LOGI(CURRENT_LOG_TAG, "Task %s asleep", taskName_);

    // Process all remaining messages in message queue in case
    // there are actions that need to be performed during shutdown.
    MessageEntry* entry = nullptr;
    while (taskQueue_ != nullptr && xQueuePeek(taskQueue_, &entry, 0) == pdTRUE)
    {
        singleMessagingLoop_(0);
    }

    taskObject_ = nullptr;

    vQueueDelete(taskQueue_);
    taskQueue_ = nullptr;

    TaskAsleepMessage result;
    publish(&result);

    // Remove ourselves from FreeRTOS.
    vTaskDelete(nullptr);
}

void DVTask::postHelper_(MessageEntry* entry)
{
    if (taskQueue_ && isAwake())
    {
        auto rv = xQueueSendToBack(taskQueue_, &entry, pdMS_TO_TICKS(100));
        if (rv == errQUEUE_FULL)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Task %s has a full queue! (maximum: %ld)", taskName_, taskQueueSize_);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        assert(rv != errQUEUE_FULL);
    }
    else
    {
        // Task isn't awake, no use keeping the entry around
        char* entryPtr = (char*)entry;
        delete[] entryPtr;
    }
}

void DVTask::singleMessagingLoop_(int64_t ticksRemaining)
{
    MessageEntry* entry = nullptr;

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
}

void DVTask::threadEntry_()
{    
    UBaseType_t stackWaterMark = INT_MAX;
    
    // Run in an infinite loop, continually waiting for messages
    // and processing them.
    for (;;)
    {
        if (taskTick_ == portMAX_DELAY)
        {
            singleMessagingLoop_(portMAX_DELAY);
        }
        else
        {
            int64_t ticksRemaining = taskTick_;
            while (ticksRemaining > 0)
            {
                auto tasksBegin = xTaskGetTickCount();
                singleMessagingLoop_(ticksRemaining);
                ticksRemaining -= xTaskGetTickCount() - tasksBegin;
            }

            onTaskTick_();
        }
        
        UBaseType_t newStackWaterMark = uxTaskGetStackHighWaterMark(nullptr);
        if (newStackWaterMark < stackWaterMark)
        {
            stackWaterMark = newStackWaterMark;
            ESP_LOGI(taskName_, "New stack high water mark of %d", newStackWaterMark);
        }
    }
}

void DVTask::onTaskQueueMessage_(DVTask* origin, TaskQueueMessage* message)
{
    message->destination->postHelper_(message->entry);
}

void DVTask::ThreadEntry_(DVTask* thisObj)
{
    thisObj->threadEntry_();
}

}

}
