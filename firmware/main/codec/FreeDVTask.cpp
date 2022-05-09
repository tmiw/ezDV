#include "FreeDVTask.h"
#include "../ui/Messaging.h"

#define CURRENT_LOG_TAG "FreeDVTask"

namespace sm1000neo::codec   
{
    void FreeDVTask::event(const smooth::core::timer::TimerExpiredEvent& event)
    {
        bool syncLed = false;
        
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
            
            syncLed = freedv_get_sync(dv_) > 0;
        }
        
        // Send UI message to update sync LED if changed
        if (syncLed != sync_)
        {
            sync_ = syncLed;
            
            sm1000neo::ui::UserInterfaceControlMessage uiMessage;
            uiMessage.action = sm1000neo::ui::UserInterfaceControlMessage::UPDATE_SYNC;
            uiMessage.value = syncLed;
            sm1000neo::util::NamedQueue::Send(UI_CONTROL_PIPE_NAME, uiMessage); 
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
    
    void FreeDVTask::event(const sm1000neo::codec::FreeDVChangeModeMessage& event)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Mode changing to %d", event.newMode);
        
        freedv_close(dv_);
        dv_ = freedv_open(event.newMode);
        assert(dv_ != nullptr);
        
        resetFifos_();
    }
    
    void FreeDVTask::event(const sm1000neo::codec::FreeDVChangePTTMessage& event)
    {
        if (event.pttEnabled != isTransmitting_)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "PTT changing to %d", event.pttEnabled);
            
            isTransmitting_ = event.pttEnabled;
            resetFifos_();
        }
    }
    
    void FreeDVTask::init()
    {
        dv_ = freedv_open(FREEDV_MODE_700D);
        assert(dv_ != nullptr);
        
        codecTimer_ = smooth::core::timer::Timer::create(
            2, timerExpiredQueue_, true,
            std::chrono::milliseconds(20));
        codecTimer_->start();
    }
    
    void FreeDVTask::resetFifos_()
    {
        if (inputFifo_ != nullptr)
        {
            codec2_fifo_destroy(inputFifo_);
        }
        
        if (outputFifo_ != nullptr)
        {
            codec2_fifo_destroy(outputFifo_);
        }
        
        inputFifo_ = codec2_fifo_create(MAX_CODEC2_SAMPLES_IN_FIFO);
        assert(inputFifo_ != nullptr);
        
        outputFifo_ = codec2_fifo_create(MAX_CODEC2_SAMPLES_IN_FIFO);
        assert(outputFifo_ != nullptr);
    }
}