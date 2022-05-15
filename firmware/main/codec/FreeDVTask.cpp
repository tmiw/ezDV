#include "FreeDVTask.h"
#include "../ui/Messaging.h"
#include "esp_log.h"
#include "../audio/TLV320.h"

#define CURRENT_LOG_TAG "FreeDVTask"

namespace sm1000neo::codec   
{
    FreeDVTask FreeDVTask::Task_;
    
    void FreeDVTask::enqueueAudio(sm1000neo::audio::ChannelLabel channel, short* audioData, size_t length)
    {    
        bool isAcceptingChannelForInput =
            (isTransmitting_ && channel == sm1000neo::audio::ChannelLabel::USER_CHANNEL) ||
            (!isTransmitting_ && channel == sm1000neo::audio::ChannelLabel::RADIO_CHANNEL);
    
        if (isAcceptingChannelForInput)
        {
            {
                std::unique_lock<std::mutex> lock(fifoMutex_);
                codec2_fifo_write(inputFifo_, audioData, length);
            }
            
            short audioDataOut[length];
            memset(audioDataOut, 0, length * sizeof(short));
            sm1000neo::audio::ChannelLabel channel = 
                isTransmitting_ ? 
                sm1000neo::audio::ChannelLabel::RADIO_CHANNEL : 
                sm1000neo::audio::ChannelLabel::USER_CHANNEL;
            
            {
                std::unique_lock<std::mutex> lock(fifoMutex_);
                codec2_fifo_read(outputFifo_, audioDataOut, length);
            }
            sm1000neo::audio::TLV320& task = sm1000neo::audio::TLV320::ThisTask();
            task.enqueueAudio(channel, audioDataOut, length);
        }
    }
    
    void FreeDVTask::tick()
    {
        // Note: we don't lock on the mutex in here because the mutex is only to make
        // sure the fifos don't fall out from under the caller of EnqueueAudio().
        // Thus, it only needs to be locked there and when we actually reset the fifos;
        // all other fifo operations are thread safe (assuming 1 reader and 1 writer).
        bool syncLed = false;
        
        if (isTransmitting_)
        {
            int numSpeechSamples = freedv_get_n_speech_samples(dv_);
            int numModemSamples = freedv_get_n_nom_modem_samples(dv_);
            short inputBuf[numSpeechSamples];
            short outputBuf[numModemSamples];
            
            int rv = codec2_fifo_read(inputFifo_, inputBuf, numSpeechSamples);
            if (rv == 0)
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
            
            int rv = codec2_fifo_read(inputFifo_, inputBuf, nin);
            if (rv == 0)
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
    }
    
    void FreeDVTask::event(const sm1000neo::codec::FreeDVChangeModeMessage& event)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Mode changing to %d", event.newMode);
        
        freedv_close(dv_);
        dv_ = freedv_open(event.newMode);
        assert(dv_ != nullptr);
        
        setSquelch_(event.newMode);
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
        
        setSquelch_(FREEDV_MODE_700D);
    }
    
    void FreeDVTask::resetFifos_()
    {
        std::unique_lock<std::mutex> lock(fifoMutex_);
        
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
    
    void FreeDVTask::setSquelch_(int mode)
    {
        // ESP32 doesn't like clipping and BPF for some reason. TBD
        freedv_set_clip(dv_, 0);
        freedv_set_tx_bpf(dv_, 0);
        
        switch (mode)
        {
            case FREEDV_MODE_700D:
                freedv_set_snr_squelch_thresh(dv_, -2.0);  /* squelch at -2.0 dB      */
                freedv_set_squelch_en(dv_, 1);
                break;
            case FREEDV_MODE_700E:
                // Note: clipping is req'd for 700E but the ESP32 can't use it right now.
                /*freedv_set_clip(dv_, 1);
                freedv_set_tx_bpf(dv_, 1);*/
            default:
                freedv_set_snr_squelch_thresh(dv_, 0.0);  /* squelch at 0.0 dB      */
                freedv_set_squelch_en(dv_, 1);
                break;
        }
    }
}