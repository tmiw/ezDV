#ifndef CODEC__FREEDV_TASK_H
#define CODEC__FREEDV_TASK_H

#include "smooth/core/Task.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "../audio/Messaging.h"
#include "codec2_fifo.h"
#include "freedv_api.h"

#define MAX_CODEC2_SAMPLES_IN_FIFO (2000)

namespace sm1000neo::codec
{
    class FreeDVTask : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>,
        public smooth::core::ipc::IEventListener<sm1000neo::audio::AudioDataMessage>
    {
    public:
        FreeDVTask()
            : smooth::core::Task("FreeDVTask", 32767, 10, std::chrono::milliseconds(1))
            , isTransmitting_(false)
            , inputFifo_(nullptr)
            , outputFifo_(nullptr)
            , dv_(nullptr)
            , timerExpiredQueue_(smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(5, *this, *this))
            , audioDataInputQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::audio::AudioDataMessage>::create(25, *this, *this))
        {
            inputFifo_ = codec2_fifo_create(MAX_CODEC2_SAMPLES_IN_FIFO);
            assert(inputFifo_ != nullptr);
            
            outputFifo_ = codec2_fifo_create(MAX_CODEC2_SAMPLES_IN_FIFO);
            assert(outputFifo_ != nullptr);
            
            // Register input channel for use by other tasks.
            sm1000neo::util::NamedQueue::Add(FREEDV_AUDIO_IN_PIPE_NAME, audioDataInputQueue_);
        }
        
        void event(const smooth::core::timer::TimerExpiredEvent& event) override;
        void event(const sm1000neo::audio::AudioDataMessage& event) override;
        
    protected:
        virtual void init();
        
    private:
        bool isTransmitting_;
        struct FIFO* inputFifo_;
        struct FIFO* outputFifo_;
        struct freedv* dv_;
        
        smooth::core::timer::TimerOwner codecTimer_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> timerExpiredQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::audio::AudioDataMessage>> audioDataInputQueue_;
    };
}

#endif // CODEC__FREEDV_TASK_H