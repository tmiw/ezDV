#ifndef CODEC__FREEDV_TASK_H
#define CODEC__FREEDV_TASK_H

#include "smooth/core/Task.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "Messaging.h"
#include "../audio/Messaging.h"
#include "codec2_fifo.h"
#include "freedv_api.h"

#define MAX_CODEC2_SAMPLES_IN_FIFO (2000)

namespace sm1000neo::codec
{
    class FreeDVTask : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>,
        public smooth::core::ipc::IEventListener<sm1000neo::audio::AudioDataMessage>,
        public smooth::core::ipc::IEventListener<sm1000neo::codec::FreeDVChangeModeMessage>,
        public smooth::core::ipc::IEventListener<sm1000neo::codec::FreeDVChangePTTMessage>
    {
    public:
        FreeDVTask()
            : smooth::core::Task("FreeDVTask", 48000, 10, std::chrono::milliseconds(1))
            , isTransmitting_(false)
            , inputFifo_(nullptr)
            , outputFifo_(nullptr)
            , dv_(nullptr)
            , sync_(false)
            , timerExpiredQueue_(smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(5, *this, *this))
            , audioDataInputQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::audio::AudioDataMessage>::create(25, *this, *this))
            , changeModeInputQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangeModeMessage>::create(2, *this, *this))
            , pttInputQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangePTTMessage>::create(2, *this, *this))
        {
            resetFifos_();
            
            // Register input channels for use by other tasks.
            sm1000neo::util::NamedQueue::Add(FREEDV_AUDIO_IN_PIPE_NAME, audioDataInputQueue_);
            sm1000neo::util::NamedQueue::Add(FREEDV_CONTROL_PIPE_NAME, changeModeInputQueue_);
            sm1000neo::util::NamedQueue::Add(FREEDV_PTT_PIPE_NAME, pttInputQueue_);            
        }
        
        void event(const smooth::core::timer::TimerExpiredEvent& event) override;
        void event(const sm1000neo::audio::AudioDataMessage& event) override;
        void event(const sm1000neo::codec::FreeDVChangeModeMessage& event) override;
        void event(const sm1000neo::codec::FreeDVChangePTTMessage& event) override;
        
    protected:
        virtual void init();
        
    private:
        bool isTransmitting_;
        struct FIFO* inputFifo_;
        struct FIFO* outputFifo_;
        struct freedv* dv_;
        bool sync_;
        
        smooth::core::timer::TimerOwner codecTimer_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> timerExpiredQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::audio::AudioDataMessage>> audioDataInputQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangeModeMessage>> changeModeInputQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangePTTMessage>> pttInputQueue_;
        
        void resetFifos_();
    };
}

#endif // CODEC__FREEDV_TASK_H