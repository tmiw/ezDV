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
#include <deque>
#include <functional>

#include "esp_log.h"

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
    using MessageHandlerHandle = void*;
    
    /// @brief Constructs a new task for the application
    /// @param taskName The friendly name of the task (used for debugging).
    /// @param taskPriority The task's priority.
    /// @param taskStackSize The task's stack size.
    /// @param pinnedCoreId  The core that the task should be pinned to (tskNO_AFFINITY to disable).
    /// @param taskQueueSize The maximum number of events that can be queued up at a time.
    /// @param taskTick The amount of time to wait for messages before running "tick" method.
    DVTask(std::string taskName, UBaseType_t taskPriority, uint32_t taskStackSize, BaseType_t pinnedCoreId, int32_t taskQueueSize, TickType_t taskTick = pdMS_TO_TICKS(20));

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
    MessageHandlerHandle registerMessageHandler(std::function<void(DVTask*, MessageType*)> handler);

    /// @brief Registers a new message handler.
    /// @tparam MessageType The message type the handler expects.
    /// @tparam ObjType The type that will be handling the message.
    /// @param handler The message handler.
    template<typename MessageType, typename ObjType>
    MessageHandlerHandle registerMessageHandler(ObjType* taskObj, void(ObjType::*handler)(DVTask*, MessageType*));

    /// @brief Unregisteers a message handler.
    /// @param handler The message handler.
    void unregisterMessageHandler(MessageHandlerHandle handler);
    
    /// @brief Waits until we receive a message of type ResultMessageType.
    /// @tparam ResultMessageType The type of message we're expecting.
    /// @param ticksToWait The maximum amount of time to wait.
    /// @param origin The origin of the message if available.
    /// @return Received message or nullptr if timed out. 
    template<typename ResultMessageType>
    ResultMessageType* waitFor(TickType_t ticksToWait, DVTask** origin);

    /// @brief Determines whether the task is awake.
    /// @return true if the task is awake, false otherwise.
    bool isAwake() const { return taskObject_ != nullptr; }

    /// @brief Static initializer, required before using DVTask.
    static void Initialize();
protected:
    virtual void onTaskStart_(DVTask* origin, TaskStartMessage* message);
    virtual void onTaskWake_(DVTask* origin, TaskWakeMessage* message);
    virtual void onTaskSleep_(DVTask* origin, TaskSleepMessage* message);
    
    virtual void onTaskStart_() = 0;
    virtual void onTaskWake_() = 0;
    virtual void onTaskSleep_() = 0;

    /// @brief Task to unconditionally execute each time through the loop. Optional.
    virtual void onTaskTick_();
    
    /// @brief Commands the task to perform startup actions.
    /// @param taskToStart The task to start.
    /// @param ticksToWait The maximum amount of time to wait.
    void start(DVTask* taskToStart, TickType_t ticksToWait = 0);

    /// @brief Commands the task to perform wakeup actions.
    /// @param taskToWake The task to wake.
    /// @param ticksToWait The maximum amount of time to wait.
    void wake(DVTask* taskToWake, TickType_t ticksToWait = 0);

    /// @brief Commands the task to perform sleep actions.
    /// @param taskToSleep The task to sleep.
    /// @param ticksToWait The maximum amount of time to wait.
    void sleep(DVTask* taskToSleep, TickType_t ticksToWait = 0);
    
private:
    // Non-template base class to help handle std::function cleanup
    class FnPtrStorage
    {
    public:
        FnPtrStorage();
        virtual ~FnPtrStorage() = default;
    };

    using EventHandlerFn = void(*)(void *event_handler_arg, DVEventBaseType event_base, int32_t event_id, void *event_data);
    using EventIdentifierPair = std::pair<DVEventBaseType, int32_t>;
    using EventMap = std::multimap<EventIdentifierPair, std::pair<EventHandlerFn, FnPtrStorage*>>;
    using PublishMap = std::multimap<EventIdentifierPair, DVTask*>;

    template<typename MessageType>
    class MessageFnPtrStorage
    {
    public:
        MessageFnPtrStorage(std::function<void(DVTask*, MessageType*)>* ptrProvided)
            : ptr(ptrProvided)
            {}

        virtual ~MessageFnPtrStorage()
        {
            delete ptr;
        }

        std::function<void(DVTask*, MessageType*)>* ptr;
    };

    // Structure to help encode messages for queuing.
    struct MessageEntry
    {
        DVEventBaseType eventBase;
        int32_t eventId;

        DVTask* origin;
        uint32_t size;
        char messageStart; // Placeholder to help write to correct memory location.
    };
    
    class TaskQueueMessage : public DVTaskMessageBase<DVTaskControlMessageTypes::TASK_QUEUE_MSG, TaskQueueMessage>
    {
    public:
        TaskQueueMessage(DVTask* destinationProvided = nullptr, MessageEntry* entryProvided = nullptr)
            : DVTaskMessageBase<DVTaskControlMessageTypes::TASK_QUEUE_MSG, TaskQueueMessage>(DV_TASK_CONTROL_MESSAGE)
            , destination(destinationProvided)
            , entry(entryProvided)
        {
            // empty
        }
    
        virtual ~TaskQueueMessage() = default;
    
        DVTask* destination;
        MessageEntry* entry;
    };

    std::string taskName_;

    TaskHandle_t taskObject_;
    EventMap eventRegistrationMap_;

    int32_t taskQueueSize_;
    int32_t taskStackSize_;
    UBaseType_t taskPriority_;
    BaseType_t pinnedCoreId_;
    QueueHandle_t taskQueue_;
    
    TickType_t taskTick_;

    MessageEntry* createMessageEntry_(DVTask* origin, DVTaskMessage* message);

    void threadEntry_();
    void postHelper_(MessageEntry* entry);
    
    void startTask_();

    template<typename ControlMessageType>
    void waitForOurs_(DVTask* taskToWaitFor, TickType_t ticksToWait);
    
    void singleMessagingLoop_(int64_t ticksRemaining);
    
    void onTaskQueueMessage_(DVTask* origin, TaskQueueMessage* message);
    
    static PublishMap SubscriberTasksByMessageType_;
    static SemaphoreHandle_t SubscriberTasksByMessageTypeSemaphore_;

    static void ThreadEntry_(DVTask* thisObj);

    template<typename MessageType>
    static void HandleEvent_(void *event_handler_arg, DVEventBaseType event_base, int32_t event_id, void *event_data);
};

template<typename MessageType>
DVTask::MessageHandlerHandle DVTask::registerMessageHandler(std::function<void(DVTask*, MessageType*)> handler)
{    
    std::function<void(DVTask*, MessageType*)>* fnPtr = new std::function<void(DVTask*, MessageType*)>(handler);
    assert(fnPtr != nullptr);

    MessageFnPtrStorage<MessageType>* fnPtrStorage = new MessageFnPtrStorage<MessageType>(fnPtr);
    assert(fnPtrStorage != nullptr);

    // Register task specific handler.
    MessageType tmpMessage;
    auto key = std::make_pair(tmpMessage.getEventBase(), tmpMessage.getEventType());
    auto val = std::make_pair((EventHandlerFn)&HandleEvent_<MessageType>, (FnPtrStorage*)fnPtrStorage);
    eventRegistrationMap_.insert(
        std::make_pair(key, val)        
    );

    // Register for use by publish.
    xSemaphoreTake(SubscriberTasksByMessageTypeSemaphore_, pdMS_TO_TICKS(100));
    SubscriberTasksByMessageType_.insert(
        std::make_pair(key, this)
    );
    xSemaphoreGive(SubscriberTasksByMessageTypeSemaphore_);
    
    return fnPtrStorage;
}

template<typename MessageType, typename ObjType>
DVTask::MessageHandlerHandle DVTask::registerMessageHandler(ObjType* taskObj, void(ObjType::*handler)(DVTask*, MessageType*))
{
    auto fn = 
        [taskObj, handler](DVTask* origin, MessageType* message)
        {
            (*taskObj.*handler)(origin, message);
        };

    return registerMessageHandler<MessageType>(fn);
}

template<typename MessageType>
void DVTask::HandleEvent_(void *event_handler_arg, DVEventBaseType event_base, int32_t event_id, void *event_data)
{
    MessageFnPtrStorage<MessageType>* fnPtrStorage = (MessageFnPtrStorage<MessageType>*)event_handler_arg;
    std::function<void(DVTask*, MessageType*)>* fnPtr = fnPtrStorage->ptr;
    
    MessageEntry* entry = *(MessageEntry**)event_data;
    MessageType* message = (MessageType*)&entry->messageStart;
    (*fnPtr)(entry->origin, message);
}

template<typename ResultMessageType>
ResultMessageType* DVTask::waitFor(TickType_t ticksToWait, DVTask** origin)
{
    ResultMessageType* result = nullptr;

    std::function<void(DVTask*, ResultMessageType*)> tempHandler = [&](DVTask* handlerOrigin, ResultMessageType* message)
    {
        // Make copy of message for the caller.
        result = new ResultMessageType();
        assert(result != nullptr);
        memcpy((void*)result, (void*)message, message->getSize());

        if (origin)
        {
            *origin = handlerOrigin;
        }
    };
    auto registrationHandle = registerMessageHandler<ResultMessageType>(tempHandler);

    // Receive inbound messages for up to ticksToWait ticks.
    // For each received message in that interval:
    //     1. If the received message is the one we want, stop.
    //     2. Otherwise, hold onto it so we can reinject it into the queue at the end.
    int64_t tickDiff = ticksToWait;
    while (tickDiff >= 0)
    {
        TickType_t beginTicks = xTaskGetTickCount();
        singleMessagingLoop_(tickDiff);
        tickDiff -= xTaskGetTickCount() - beginTicks;

        if (result != nullptr)
        {
            break;
        }
    }

    // Unsubscribe from the requested result message.
    unregisterMessageHandler(registrationHandle);

    return result;
}

template<typename ControlMessageType>
void DVTask::waitForOurs_(DVTask* taskToWaitFor, TickType_t ticksToWait)
{
    int64_t tickDiff = ticksToWait;
    bool found = false;

    while (tickDiff >= 0)
    {
        TickType_t beginTicks = xTaskGetTickCount();
        DVTask* origin = nullptr;
        
        auto message = waitFor<ControlMessageType>((TickType_t)tickDiff, &origin);
        if (message != nullptr)
        {
            delete message; // message isn't needed, just the origin
        }
        
        tickDiff -= xTaskGetTickCount() - beginTicks;

        if (taskToWaitFor == origin)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        ESP_LOGE("DVTask", "Was waiting for %s but timed out", taskToWaitFor->taskName_.c_str());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    assert(found);
}

}

}

#endif // DV_TASK_H