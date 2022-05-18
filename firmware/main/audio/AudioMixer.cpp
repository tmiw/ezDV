#include "AudioMixer.h"
#include "TLV320.h"

namespace ezdv::audio
{
    AudioMixer AudioMixer::Task_;
    
    void AudioMixer::tick()
    {
        // Process on a sample by sample basis
        short bufLeft;
        short bufRight;
        int ctr = I2S_NUM_SAMPLES_PER_INTERVAL;
        while (--ctr > 0 && (codec2_fifo_used(leftChannelFifo_) > 0 || codec2_fifo_used(rightChannelFifo_) > 0))
        {
            bufLeft = 0;
            bufRight = 0;
            
            codec2_fifo_read(leftChannelFifo_, &bufLeft, 1);
            codec2_fifo_read(rightChannelFifo_, &bufRight, 1);
            
            // See https://dsp.stackexchange.com/questions/3581/algorithms-to-mix-audio-signals-without-clipping
            // for more info. This is basically (1/sqrt(2)) * (a + b)
            float addedSample = 0.707106 * (bufLeft + bufRight);
            short resultShort = (short)addedSample;
            
            // TBD -- support other than the user channel.
            TLV320::ThisTask().enqueueAudio(ezdv::audio::ChannelLabel::USER_CHANNEL, &resultShort, 1);
        }
    }
    
    void AudioMixer::enqueueAudio(ezdv::audio::ChannelLabel channel, short* audioData, size_t length)
    {
        if (channel == LEFT_CHANNEL)
        {
            codec2_fifo_write(leftChannelFifo_, audioData, length);
        }
        else
        {
            codec2_fifo_write(rightChannelFifo_, audioData, length);
        }
    }
}