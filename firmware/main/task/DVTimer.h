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

#ifndef DV_TASK_TIMER_H
#define DV_TASK_TIMER_H

#include <functional>
#include <inttypes.h>

#include "DVTask.h"

#include "esp_timer.h"
#include "esp_log.h"

// Shared across all files as this is actually used pretty often.
#define MS_TO_US(ms) (ms * 1000)

extern "C"
{
    DV_EVENT_DECLARE_BASE(DV_TASK_TIMER_MESSAGE);
}

namespace ezdv
{

namespace task
{

/// @brief Represents a timer in the application.
class DVTimer
{
public:
    using TimerHandlerFn = std::function<void(DVTimer*)>;
    
    DVTimer(DVTask* owner, TimerHandlerFn fn, uint64_t intervalInMicroseconds, const char* timerName);

    template<typename ClassObj>
    DVTimer(DVTask* owner, void (ClassObj::*fn)(DVTimer*), uint64_t intervalInMicroseconds, const char* timerName);

    virtual ~DVTimer();
    
    void start(bool once = false);
    void stop();

    void changeInterval(uint64_t intervalInMicroseconds);
    
private:
    class TimerFireMessage : public DVTaskMessageBase<1, TimerFireMessage>
    {
    public:
        TimerFireMessage(DVTimer* timerProvided = nullptr)
            : DVTaskMessageBase<1, TimerFireMessage>(DV_TASK_TIMER_MESSAGE)
            , timer(timerProvided) { }
        virtual ~TimerFireMessage() = default;
        
        DVTimer* timer;
    };

    class TimerHandler
    {
    public:
        virtual ~TimerHandler() = default;

        virtual void call(DVTimer* timerObj) = 0;

    protected:
        TimerHandler() = default; // cannot be created by non-children
    };

    class TimerHandlerFnForwarder : public TimerHandler
    {
    public:
        TimerHandlerFnForwarder(std::function<void(DVTimer*)> fn)
            : fn_(fn)
        {
            // empty
        }

        virtual ~TimerHandlerFnForwarder() = default;

        virtual void call(DVTimer* timerObj) override
        {
            fn_(timerObj);
        }

    private:
        std::function<void(DVTimer*)> fn_;
    };

    template<typename ClassObj>
    class TimerHandlerFowarder : public TimerHandler
    {
    public:
        TimerHandlerFowarder(ClassObj* classObj, void (ClassObj::*fn)(DVTimer*))
            : classObj_(classObj)
            , fn_(fn)
        {
            // empty
        }

        virtual ~TimerHandlerFowarder() = default;

        virtual void call(DVTimer* timerObj) override
        {
            (classObj_->*fn_)(timerObj);
        }

    private:
        ClassObj* classObj_;
        void (ClassObj::*fn_)(DVTimer*);
    };
    
    DVTask* owner_;
    TimerHandler* fn_;
    uint64_t intervalInMicroseconds_;
    bool running_;
    bool once_;
    esp_timer_handle_t timerHandle_;
    
    void onTimerFire_(DVTask* origin, TimerFireMessage* message);
    
    static void OnESPTimerFire_(void* ptr);
};

template<typename ClassObj>
DVTimer::DVTimer(DVTask* owner, void (ClassObj::*fn)(DVTimer*), uint64_t intervalInMicroseconds, const char* timerName)
    : owner_(owner)
    , intervalInMicroseconds_(intervalInMicroseconds)
    , running_(false)
    , once_(false)
{
    fn_ = new TimerHandlerFowarder<ClassObj>((ClassObj*)owner, fn);
    assert(fn_ != nullptr);

    esp_timer_create_args_t args = {
        .callback = &OnESPTimerFire_,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "DVTimer",
        .skip_unhandled_events = true,
    };
    
    if (timerName != nullptr)
    {
        args.name = timerName;
    }
    
    ESP_ERROR_CHECK(
        esp_timer_create(
            &args, &timerHandle_
        )
    );
    
    owner->registerMessageHandler(this, &DVTimer::onTimerFire_);
}

}

}

#endif // DV_TASK_TIMER_H