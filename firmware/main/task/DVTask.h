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
 
#ifndef DV_TASK_H
#define DV_TASK_H

#include <string>
#include <map>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "DVTaskControlMessage.h"

namespace ezdv
{

namespace task
{

/// @brief Represents a task in the application.
class DVTask
{
public:
    /// @brief Constructs a new task for the application
    /// @param taskName The friendly name of the task (used for debugging).
    /// @param taskPriority The task's priority.
    /// @param taskStackSize The task's stack size.
    /// @param pinnedCoreId  The core that the task should be pinned to (tskNO_AFFINITY to disable).
    /// @param taskQueueSize The maximum number of events that can be queued up at a time.
    DVTask(std::string taskName, UBaseType_t taskPriority, uint32_t taskStackSize, BaseType_t pinnedCoreId, int32_t taskQueueSize);

    /// @brief Cleans up after the task.
    virtual ~DVTask();

    /// @brief Commands the task to perform startup actions.
    void start();

    /// @brief Commands the task to perform wakeup actions.
    void wake();

    /// @brief Commands the task to perform sleep actions.
    void sleep();

    /// @brief Posts a message to own event queue.
    /// @param message The message to post to the task.
    void post(DVTaskMessage* message);

    /// @brief Posts a message to own event queue (from ISR). May not return.
    /// @param message The message to post to the task.
    void postISR(DVTaskMessage* message);

    /// @brief Posts a message to destination's event queue.
    /// @param destination The destination task to send the event to.
    /// @param message The message to send.
    void sendTo(DVTask* destination, DVTaskMessage* message);

    /// @brief Publishes a message to all tasks.
    /// @param message The message to publish.
    void publish(DVTaskMessage* message);

    /// @brief Registers a new message handler.
    /// @tparam MessageType The type that the handler expects.
    /// @param handler The message handler.
    template<typename MessageType>
    void registerMessageHandler(std::function<void(DVTask*, MessageType*)> handler);

    /// @brief Registers a new message handler.
    /// @tparam MessageType The message type the handler expects.
    /// @tparam ObjType The type that will be handling the message.
    /// @param handler The message handler.
    template<typename MessageType, typename ObjType>
    void registerMessageHandler(ObjType* taskObj, void(ObjType::*handler)(DVTask*, MessageType*));

    /// @brief Static initializer, required before using DVTask.
    static void Initialize();
protected:
    virtual void onTaskStart_(DVTask* origin, TaskStartMessage* message) = 0;
    virtual void onTaskWake_(DVTask* origin, TaskWakeMessage* message) = 0;
    virtual void onTaskSleep_(DVTask* origin, TaskSleepMessage* message) = 0;

    /// @brief Task to unconditionally execute each time through the loop. Optional.
    virtual void onTaskTick_();
    
private:
    using EventHandlerFn = void(*)(void *event_handler_arg, DVEventBaseType event_base, int32_t event_id, void *event_data);
    using EventIdentifierPair = std::pair<DVEventBaseType, uint32_t>;
    using EventMap = std::multimap<EventIdentifierPair, std::pair<EventHandlerFn, void*>>;
    using PublishMap = std::multimap<EventIdentifierPair, DVTask*>;

    // Structure to help encode messages for queuing.
    struct MessageEntry
    {
        DVEventBaseType eventBase;
        int32_t eventId;

        DVTask* origin;
        uint32_t size;
        char messageStart; // Placeholder to help write to correct memory location.
    };

    TaskHandle_t taskObject_;
    EventMap eventRegistrationMap_;

    QueueHandle_t taskQueue_;

    MessageEntry* createMessageEntry_(DVTask* origin, DVTaskMessage* message);

    void threadEntry_();
    void postHelper_(MessageEntry* entry);
    
    static PublishMap SubscriberTasksByMessageType_;
    static SemaphoreHandle_t SubscriberTasksByMessageTypeSemaphore_;

    static void ThreadEntry_(DVTask* thisObj);

    template<typename MessageType>
    static void HandleEvent_(void *event_handler_arg, DVEventBaseType event_base, int32_t event_id, void *event_data);
};

template<typename MessageType>
void DVTask::registerMessageHandler(std::function<void(DVTask*, MessageType*)> handler)
{
    std::function<void(DVTask*, MessageType*)>* fnPtr = new std::function<void(DVTask*, MessageType*)>(handler);

    // Register task specific handler.
    MessageType tmpMessage;
    eventRegistrationMap_.emplace(
        std::make_pair(tmpMessage.getEventBase(), tmpMessage.getEventType()),
        std::make_pair((EventHandlerFn)&HandleEvent_<MessageType>, fnPtr)
    );

    // Register for use by publish.
    xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(100));
    SubscriberTasksByMessageType_.emplace(
        std::make_pair(tmpMessage.getEventBase(), tmpMessage.getEventType()), this
    );
    xSemaphoreGive(SubscriberTasksByMessageTypeSemaphore_);
}

template<typename MessageType, typename ObjType>
void DVTask::registerMessageHandler(ObjType* taskObj, void(ObjType::*handler)(DVTask*, MessageType*))
{
    auto fn = 
        [taskObj, handler](DVTask* origin, MessageType* message)
        {
            (*taskObj.*handler)(origin, message);
        };

    registerMessageHandler<MessageType>(fn);
}

template<typename MessageType>
void DVTask::HandleEvent_(void *event_handler_arg, DVEventBaseType event_base, int32_t event_id, void *event_data)
{
    std::function<void(DVTask*, MessageType*)>* fnPtr = (std::function<void(DVTask*, MessageType*)>*)event_handler_arg;
    
    MessageEntry* entry = *(MessageEntry**)event_data;
    MessageType* message = (MessageType*)&entry->messageStart;
    (*fnPtr)(entry->origin, message);
}

}

}

#endif // DV_TASK_H