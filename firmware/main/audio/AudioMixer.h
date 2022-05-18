#ifndef AUDIO__AUDIO_MIXER_H
#define AUDIO__AUDIO_MIXER_H

#include "Constants.h"
#include "codec2_fifo.h"
#include "smooth/core/Task.h"

namespace ezdv::audio
{
    class AudioMixer : 
        public smooth::core::Task
    {
    public:
        AudioMixer()
            : smooth::core::Task("AudioMixer", 2048, 10, std::chrono::milliseconds(I2S_TIMER_INTERVAL_MS))
        {
            // Create input FIFOs so we can mix both both channels during the tick.
            leftChannelFifo_ = codec2_fifo_create(I2S_NUM_SAMPLES_PER_INTERVAL * 10);
            assert(leftChannelFifo_ != nullptr);
            rightChannelFifo_ = codec2_fifo_create(I2S_NUM_SAMPLES_PER_INTERVAL * 10);
            assert(rightChannelFifo_ != nullptr);
        }
        
        virtual void tick() override;
        
        static AudioMixer& ThisTask()
        {
            return Task_;
        }
        
        void enqueueAudio(ezdv::audio::ChannelLabel channel, short* audioData, size_t length);
        
    private:
        static AudioMixer Task_;
        
        struct FIFO* leftChannelFifo_;
        struct FIFO* rightChannelFifo_;
    };
}

#endif // AUDIO__AUDIO_MIXER_H