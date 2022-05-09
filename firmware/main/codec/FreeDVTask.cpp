#include "FreeDVTask.h"

#define CURRENT_LOG_TAG "FreeDVTask"

namespace sm1000neo::codec   
{
    void FreeDVTask::event(const smooth::core::timer::TimerExpiredEvent& event)
    {        
        if (isTransmitting_)
        {
            int numSpeechSamples = freedv_get_n_speech_samples(dv_);
            int numModemSamples = freedv_get_n_nom_modem_samples(dv_);
            short inputBuf[numSpeechSamples];
            short outputBuf[numModemSamples];
            
            while(codec2_fifo_read(inputFifo_, inputBuf, numSpeechSamples) == 0)
            {
                freedv_tx(dv_, outputBuf, inputBuf);
                codec2_fifo_write(outputFifo_, outputBuf, numModemSamples);
            }
        }
        else
        {
            short inputBuf[freedv_get_n_max_modem_samples(dv_)];
            short outputBuf[freedv_get_n_speech_samples(dv_)];
            int nin = freedv_nin(dv_);
            
            while(codec2_fifo_read(inputFifo_, inputBuf, nin) == 0)
            {
                int nout = freedv_rx(dv_, outputBuf, inputBuf);
                codec2_fifo_write(outputFifo_, outputBuf, nout);
                nin = freedv_nin(dv_);
            }
        }
        
        sm1000neo::audio::AudioDataMessage result;
        result.channel = isTransmitting_ ? RADIO_CHANNEL : USER_CHANNEL;
        while(codec2_fifo_read(outputFifo_, result.audioData, NUM_SAMPLES_PER_AUDIO_MESSAGE) == 0)
        {
            sm1000neo::util::NamedQueue::Send(AUDIO_OUT_PIPE_NAME, result);
        }
    }
    
    void FreeDVTask::event(const sm1000neo::audio::AudioDataMessage& event)
    {
        // We just put data into the FIFO here. The timer is what actually
        // processes it.
        bool isAcceptingChannelForInput =
            (isTransmitting_ && event.channel == USER_CHANNEL) ||
            (!isTransmitting_ && event.channel == RADIO_CHANNEL);
        
        if (isAcceptingChannelForInput)
        {
            codec2_fifo_write(inputFifo_, const_cast<short*>(event.audioData), NUM_SAMPLES_PER_AUDIO_MESSAGE);
        }
    }
    
    void FreeDVTask::init()
    {
        // TBD: mode change
        dv_ = freedv_open(FREEDV_MODE_700D);
        assert(dv_ != nullptr);
        
        codecTimer_ = smooth::core::timer::Timer::create(
            2, timerExpiredQueue_, true,
            std::chrono::milliseconds(20));
        codecTimer_->start();
    }
}