#ifndef UI__USER_INTERFACE_TASK_H
#define UI__USER_INTERFACE_TASK_H

#include <vector>
#include "driver/gpio.h"
#include "smooth/core/Task.h"
#include "util/NamedQueue.h"
#include "Messaging.h"
#include "smooth/core/ipc/ISRTaskEventQueue.h"
#include "smooth/core/io/InterruptInput.h"
#include "smooth/core/io/Output.h"
#include "smooth/core/timer/Timer.h"
#include "audio/Messaging.h"

#define GPIO_PTT_BUTTON GPIO_NUM_4
#define GPIO_MODE_BUTTON GPIO_NUM_5 // PTT button doesn't work on v0.1 @ GPIO 4
#define GPIO_VOL_UP_BUTTON GPIO_NUM_6
#define GPIO_VOL_DOWN_BUTTON GPIO_NUM_7

#define GPIO_SYNC_LED GPIO_NUM_1
#define GPIO_OVL_LED GPIO_NUM_2
#define GPIO_PTT_LED GPIO_NUM_41
#define GPIO_PTT_NPN GPIO_NUM_21 /* bridges GND and PTT together at the 3.5mm jack */
#define GPIO_NET_LED GPIO_NUM_42

// Beeper constants. This is based on 1 dit per 60/(50 * wpm) = 60/(50*15) = 0.08s
#define CW_TIME_UNIT_MS 80
#define DIT_SIZE 1
#define DAH_SIZE 3
#define SPACE_BETWEEN_DITS 1
#define SPACE_BETWEEN_CHARS 3
#define SPACE_BETWEEN_WORDS 7
#define CW_SIDETONE_FREQ_HZ ((float)600.0)
#define SAMPLE_RATE_RECIP 0.000125 /* 8000 Hz */

namespace ezdv::ui
{
    class UserInterfaceTask : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<ezdv::ui::UserInterfaceControlMessage>,
        public smooth::core::ipc::IEventListener<smooth::core::io::InterruptInputEvent>,
        public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>
    {
    public:
        UserInterfaceTask()
            : smooth::core::Task("UserInterfaceTask", 4096, 10, std::chrono::milliseconds(1))
            , uiInputQueue_(smooth::core::ipc::TaskEventQueue<ezdv::ui::UserInterfaceControlMessage>::create(5, *this, *this))
            , uiButtonQueue_(smooth::core::ipc::ISRTaskEventQueue<smooth::core::io::InterruptInputEvent, 5>::create(*this, *this))
            , pttButton_(uiButtonQueue_, GPIO_PTT_BUTTON, false, false, GPIO_INTR_ANYEDGE)
            , volUpButton_(uiButtonQueue_, GPIO_VOL_UP_BUTTON, false, false, GPIO_INTR_ANYEDGE)
            , volDownButton_(uiButtonQueue_, GPIO_VOL_DOWN_BUTTON, false, false, GPIO_INTR_ANYEDGE)
            , changeModeButton_(uiButtonQueue_, GPIO_MODE_BUTTON, false, false, GPIO_INTR_POSEDGE)
            , syncLed_(GPIO_SYNC_LED, true, false, false)
            , overloadLed_(GPIO_OVL_LED, true, false, false)
            , pttLed_(GPIO_PTT_LED, true, false, false)
            , pttNPN_(GPIO_PTT_NPN, true, false, false)
            , netLed_(GPIO_NET_LED, true, false, false)
            , timerEventQueue_(smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(3, *this, *this))
            , sineCounter_(0)
            , currentFDVMode_(0)
            , inPOST_(true)
            , inPTT_(false)
        {
            // Turn on all LEDs until we're done initializing.
            syncLed_.set(true);
            overloadLed_.set(true);
            pttLed_.set(true);
            netLed_.set(true);
            
            // Register input channel for use by other tasks.
            ezdv::util::NamedQueue::Add(UI_CONTROL_PIPE_NAME, uiInputQueue_);
            
            // Queue up initial announcement string
            stringToBeeperScript_(" V1");
            
            changeFDVMode_(currentFDVMode_);
        }
        
        void event(const ezdv::ui::UserInterfaceControlMessage& event) override;
        void event(const smooth::core::io::InterruptInputEvent& event) override;
        void event(const smooth::core::timer::TimerExpiredEvent& event) override;
    protected:
        virtual void init();
        
    private:        
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<ezdv::ui::UserInterfaceControlMessage>> uiInputQueue_;
        std::shared_ptr<smooth::core::ipc::IISRTaskEventQueue<smooth::core::io::InterruptInputEvent>> uiButtonQueue_;
        
        smooth::core::io::InterruptInput pttButton_;
        smooth::core::io::InterruptInput volUpButton_;
        smooth::core::io::InterruptInput volDownButton_;
        smooth::core::io::InterruptInput changeModeButton_;
        
        smooth::core::io::Output syncLed_;
        smooth::core::io::Output overloadLed_;
        smooth::core::io::Output pttLed_;
        smooth::core::io::Output pttNPN_;
        smooth::core::io::Output netLed_;
        
        std::vector<bool> beeperList_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> timerEventQueue_;
        smooth::core::timer::TimerOwner beeperTimer_;
        smooth::core::timer::TimerOwner volBtnTimer_;
        int sineCounter_;
        int currentFDVMode_;
        bool inPOST_;
        bool inPTT_;
        ezdv::audio::ChangeVolumeMessage volMessage_;
        
        void stringToBeeperScript_(std::string str);
        void charToBeeperScript_(char ch);
        void changeFDVMode_(int mode);
    };
}

#endif // UI__USER_INTERFACE_TASK_H