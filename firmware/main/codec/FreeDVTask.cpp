#include "FreeDVTask.h"
#include "../ui/Messaging.h"
#include "esp_log.h"
#include "../audio/TLV320.h"

#define CURRENT_LOG_TAG "FreeDVTask"

namespace sm1000neo::codec   
{
    std::mutex FreeDVTask::FifoMutex_;
    struct FIFO* FreeDVTask::InputFifo_ = nullptr;
    struct FIFO* FreeDVTask::OutputFifo_ = nullptr;
    
    void FreeDVTask::EnqueueAudio(sm1000neo::audio::AudioDataMessage::ChannelLabel channel, short* audioData, size_t length)
    {    
        bool isAcceptingChannelForInput =
            (isTransmitting_ && channel == USER_CHANNEL) ||
            (!isTransmitting_ && channel == RADIO_CHANNEL);
    
        if (isAcceptingChannelForInput)
        {
            //ESP_LOGI(CURRENT_LOG_TAG, "Accepted inbound audio");
        
            {
                std::unique_lock<std::mutex> lock(FifoMutex_);
                codec2_fifo_write(InputFifo_, audioData, length);
            }
            
            short audioData[length];
            sm1000neo::audio::AudioDataMessage::ChannelLabel channel = isTransmitting_ ? RADIO_CHANNEL : USER_CHANNEL;
            
            int rv = 0;
            {
                std::unique_lock<std::mutex> lock(FifoMutex_);
                rv = codec2_fifo_read(OutputFifo_, audioData, length);
            }
            
            if (rv == 0)
            {
                sm1000neo::audio::TLV320::ThisTask().EnqueueAudio(channel, audioData, length);
            }
        }
    }
    
    void FreeDVTask::tick()
    {
        bool syncLed = false;
        
        if (isTransmitting_)
        {
            int numSpeechSamples = freedv_get_n_speech_samples(dv_);
            int numModemSamples = freedv_get_n_nom_modem_samples(dv_);
            short inputBuf[numSpeechSamples];
            short outputBuf[numModemSamples];
            
            int rv = 0;
            {
                std::unique_lock<std::mutex> lock(FifoMutex_);
                rv = codec2_fifo_read(InputFifo_, inputBuf, numSpeechSamples);
            }
            if (rv == 0)
            {
                auto timeBegin = esp_timer_get_time();
                freedv_tx(dv_, outputBuf, inputBuf);
                auto timeEnd = esp_timer_get_time();
                ESP_LOGI(CURRENT_LOG_TAG, "freedv_tx ran in %lld us", timeEnd - timeBegin);
                {
                    std::unique_lock<std::mutex> lock(FifoMutex_);
                    //ESP_LOGI(CURRENT_LOG_TAG, "output FIFO has %d samples before tx", codec2_fifo_used(OutputFifo_));
                    codec2_fifo_write(OutputFifo_, outputBuf, numModemSamples);
                    //ESP_LOGI(CURRENT_LOG_TAG, "output FIFO has %d samples after tx", codec2_fifo_used(OutputFifo_));
                }
            }
        }
        else
        {
            short inputBuf[freedv_get_n_max_modem_samples(dv_)];
            short outputBuf[freedv_get_n_speech_samples(dv_)];
            int nin = freedv_nin(dv_);
            
            int rv = 0;
            {
                std::unique_lock<std::mutex> lock(FifoMutex_);
                rv = codec2_fifo_read(InputFifo_, inputBuf, nin);
            }
            
            if (rv == 0)
            {
                int nout = freedv_rx(dv_, outputBuf, inputBuf);
                {
                    std::unique_lock<std::mutex> lock(FifoMutex_);
                    codec2_fifo_write(OutputFifo_, outputBuf, nout);
                    nin = freedv_nin(dv_);
                }           
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
        
        // ESP32 doesn't like clipping and BPF for some reason. TBD
        freedv_set_clip(dv_, 0);
        freedv_set_tx_bpf(dv_, 0);
        
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
        
        // ESP32 doesn't like clipping and BPF for some reason. TBD
        freedv_set_clip(dv_, 0);
        freedv_set_tx_bpf(dv_, 0);
    }
    
    void FreeDVTask::resetFifos_()
    {
        std::unique_lock<std::mutex> lock(FifoMutex_);
        
        if (InputFifo_ != nullptr)
        {
            codec2_fifo_destroy(InputFifo_);
        }
        
        if (OutputFifo_ != nullptr)
        {
            codec2_fifo_destroy(OutputFifo_);
        }
        
        InputFifo_ = codec2_fifo_create(MAX_CODEC2_SAMPLES_IN_FIFO);
        assert(InputFifo_ != nullptr);
        
        OutputFifo_ = codec2_fifo_create(MAX_CODEC2_SAMPLES_IN_FIFO);
        assert(OutputFifo_ != nullptr);
    }
}