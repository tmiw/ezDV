#ifndef CODEC__FREEDV_TASK_H
#define CODEC__FREEDV_TASK_H

#include "smooth/core/Task.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "Messaging.h"
#include "../audio/Messaging.h"
#include "codec2_fifo.h"
#include "freedv_api.h"

#define MAX_CODEC2_SAMPLES_IN_FIFO (2560)

namespace sm1000neo::codec
{
    class FreeDVTask : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<sm1000neo::codec::FreeDVChangeModeMessage>,
        public smooth::core::ipc::IEventListener<sm1000neo::codec::FreeDVChangePTTMessage>
    {
    public:
        FreeDVTask()
            : smooth::core::Task("FreeDVTask", 48000, 10, std::chrono::milliseconds(20), 1)
            , isTransmitting_(false)
            , dv_(nullptr)
            , sync_(false)
            , changeModeInputQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangeModeMessage>::create(2, *this, *this))
            , pttInputQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangePTTMessage>::create(2, *this, *this))
        {
            resetFifos_();
            
            // Register input channels for use by other tasks.
            sm1000neo::util::NamedQueue::Add(FREEDV_CONTROL_PIPE_NAME, changeModeInputQueue_);
            sm1000neo::util::NamedQueue::Add(FREEDV_PTT_PIPE_NAME, pttInputQueue_);            
        }
        
        virtual void tick() override;
        void event(const sm1000neo::codec::FreeDVChangeModeMessage& event) override;
        void event(const sm1000neo::codec::FreeDVChangePTTMessage& event) override;
        
        static FreeDVTask& ThisTask()
        {
            static FreeDVTask task;
            return task;
        } 
        
        void EnqueueAudio(sm1000neo::audio::AudioDataMessage::ChannelLabel channel, short* audioData, size_t length);
    protected:
        virtual void init();
        
    private:
        static std::mutex FifoMutex_;
        static struct FIFO* InputFifo_;
        static struct FIFO* OutputFifo_;
        
        bool isTransmitting_;
        struct freedv* dv_;
        bool sync_;
        
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangeModeMessage>> changeModeInputQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::codec::FreeDVChangePTTMessage>> pttInputQueue_;
        
        void resetFifos_();
    };
}

#endif // CODEC__FREEDV_TASK_H