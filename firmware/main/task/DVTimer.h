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

extern "C"
{
    ESP_EVENT_DECLARE_BASE(DV_TASK_TIMER_MESSAGE);
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
    
    DVTimer(DVTask* owner, TimerHandlerFn fn, uint64_t intervalInMicroseconds);
    virtual ~DVTimer();
    
    void start(bool once = false);
    void stop();
    
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
    
    DVTask* owner_;
    TimerHandlerFn fn_;
    uint64_t intervalInMicroseconds_;
    bool running_;
    esp_timer_handle_t timerHandle_;
    
    void onTimerFire_(DVTask* origin, TimerFireMessage* message);
    
    static void OnESPTimerFire_(void* ptr);
};

}

}

#endif // DV_TASK_TIMER_H