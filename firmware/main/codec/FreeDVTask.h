#ifndef CODEC__FREEDV_TASK_H
#define CODEC__FREEDV_TASK_H

#include "smooth/core/Task.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "Messaging.h"
#include "../audio/Constants.h"
#include "codec2_fifo.h"
#include "freedv_api.h"

#define MAX_CODEC2_SAMPLES_IN_FIFO (2560)

namespace ezdv::codec
{
    class FreeDVTask : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<ezdv::codec::FreeDVChangeModeMessage>,
        public smooth::core::ipc::IEventListener<ezdv::codec::FreeDVChangePTTMessage>
    {
    public:
        enum Source { TLV320, IC705 };
        
        FreeDVTask()
            : smooth::core::Task("FreeDVTask", 48000, 10, std::chrono::milliseconds(20), 1)
            , inputFifo_(nullptr)
            , outputFifo_(nullptr)
            , isTransmitting_(false)
            , dv_(nullptr)
            , sync_(false)
            , wifiConnected_(false)
            , changeModeInputQueue_(smooth::core::ipc::TaskEventQueue<ezdv::codec::FreeDVChangeModeMessage>::create(2, *this, *this))
            , pttInputQueue_(smooth::core::ipc::TaskEventQueue<ezdv::codec::FreeDVChangePTTMessage>::create(2, *this, *this))
        {
            resetFifos_();
            
            // Register input channels for use by other tasks.
            ezdv::util::NamedQueue::Add(FREEDV_CONTROL_PIPE_NAME, changeModeInputQueue_);
            ezdv::util::NamedQueue::Add(FREEDV_PTT_PIPE_NAME, pttInputQueue_);            
        }
        
        virtual void tick() override;
        void event(const ezdv::codec::FreeDVChangeModeMessage& event) override;
        void event(const ezdv::codec::FreeDVChangePTTMessage& event) override;
        
        static FreeDVTask& ThisTask()
        {
            return Task_;
        } 
        
        void enqueueAudio(ezdv::audio::ChannelLabel channel, Source source, short* audioData, size_t length);
    protected:
        virtual void init();
        
    private:
        static FreeDVTask Task_;
        
        std::mutex fifoMutex_;
        struct FIFO* inputFifo_;
        struct FIFO* outputFifo_;
        
        bool isTransmitting_;
        struct freedv* dv_;
        bool sync_;
        bool wifiConnected_;
        
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<ezdv::codec::FreeDVChangeModeMessage>> changeModeInputQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<ezdv::codec::FreeDVChangePTTMessage>> pttInputQueue_;
        
        void resetFifos_();
        void setSquelch_(int mode);
    };
}

#endif // CODEC__FREEDV_TASK_H