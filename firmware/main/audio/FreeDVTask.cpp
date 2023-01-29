/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2022 Mooneer Salem
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstring>

#include "FreeDVTask.h"

#include "esp_dsp.h"
#include "codec2_fdmdv.h"
#include "codec2_math.h"

#define EIGHT_KHZ_BUF_SIZE 160
#define FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP (EIGHT_KHZ_BUF_SIZE * FDMDV_OS_48)
#define CURRENT_LOG_TAG ("FreeDV")

namespace ezdv
{

namespace audio
{

FreeDVTask::FreeDVTask()
    : DVTask("FreeDVTask", 10 /* TBD */, 64000, 1, 10)
    , AudioInput(2, 2)
    , dv_(nullptr)
    , rText_(nullptr)
    , isTransmitting_(false)
    , isActive_(false)
{
    registerMessageHandler(this, &FreeDVTask::onSetFreeDVMode_);
    registerMessageHandler(this, &FreeDVTask::onSetPTTState_);
    registerMessageHandler(this, &FreeDVTask::onReportingSettingsUpdate_);
    
    inputFilterMemory_ = new short[FDMDV_OS_TAPS_48K + FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP];
    assert(inputFilterMemory_ != nullptr);
    memset(inputFilterMemory_, 0, sizeof(short) * (FDMDV_OS_TAPS_48K + FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP));
    
    outputFilterMemory_ = new short[FDMDV_OS_TAPS_48_8K + EIGHT_KHZ_BUF_SIZE];
    assert(outputFilterMemory_ != nullptr);
    memset(outputFilterMemory_, 0, sizeof(short) * (FDMDV_OS_TAPS_48_8K + EIGHT_KHZ_BUF_SIZE));
    
    inputFifo8K_ = codec2_fifo_create(2560);
    assert(inputFifo8K_ != nullptr);
    
    outputFifo8K_ = codec2_fifo_create(2560);
    assert(outputFifo8K_ != nullptr);
}

FreeDVTask::~FreeDVTask()
{
    if (dv_ != nullptr)
    {
        if (rText_ != nullptr)
        {
            reliable_text_unlink_from_freedv(rText_);
            reliable_text_destroy(rText_);
            rText_ = nullptr;
        }

        freedv_close(dv_);
        dv_ = nullptr;
    }
    
    delete[] inputFilterMemory_;
    delete[] outputFilterMemory_;
}

void FreeDVTask::onTaskStart_()
{
    isActive_ = true;
}

void FreeDVTask::onTaskWake_()
{
    onTaskStart_();
}

void FreeDVTask::onTaskSleep_()
{
    isActive_ = false;

    if (dv_ != nullptr)
    {
        if (rText_ != nullptr)
        {
            reliable_text_unlink_from_freedv(rText_);
            reliable_text_destroy(rText_);
            rText_ = nullptr;
        }

        freedv_close(dv_);
        dv_ = nullptr;
    }
}

void FreeDVTask::onTaskTick_()
{
    if (!isActive_) return;

    //ESP_LOGI(CURRENT_LOG_TAG, "timer tick");

    struct FIFO* codecInputFifo = nullptr;
    struct FIFO* codecOutputFifo = nullptr;
    if (isTransmitting_)
    {
        // Input is microphone, output is radio
        codecInputFifo = getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL);
        codecOutputFifo = getAudioOutput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL);
    }
    else
    {
        // Input is radio, output is microphone
        codecInputFifo = getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL);
        codecOutputFifo = getAudioOutput(audio::AudioInput::ChannelLabel::USER_CHANNEL);
    }

    // Downconvert to 8K and add to 8K input FIFO
    while (codec2_fifo_used(codecInputFifo) >= FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP)
    {
        short temp[EIGHT_KHZ_BUF_SIZE];
        codec2_fifo_read(codecInputFifo, &inputFilterMemory_[FDMDV_OS_TAPS_48K], FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP);
        fdmdv_48_to_8_short(temp, &inputFilterMemory_[FDMDV_OS_TAPS_48K], EIGHT_KHZ_BUF_SIZE);
        codec2_fifo_write(inputFifo8K_, temp, EIGHT_KHZ_BUF_SIZE);
    }
    
    if (dv_ == nullptr)
    {
        // Analog mode, just pipe through the audio.
        while (codec2_fifo_used(inputFifo8K_) >= EIGHT_KHZ_BUF_SIZE)
        {
            short tempOut[EIGHT_KHZ_BUF_SIZE];
            codec2_fifo_read(inputFifo8K_, tempOut, EIGHT_KHZ_BUF_SIZE);
            codec2_fifo_write(outputFifo8K_, tempOut, EIGHT_KHZ_BUF_SIZE);
        }
    }
    else
    {
        bool syncLed = false;

        if (isTransmitting_)
        {
            int numSpeechSamples = freedv_get_n_speech_samples(dv_);
            int numModemSamples = freedv_get_n_nom_modem_samples(dv_);
            
            short inputBuf[numSpeechSamples];
            short outputBuf[numModemSamples];
        
            int rv = codec2_fifo_read(inputFifo8K_, inputBuf, numSpeechSamples);
            if (rv == 0)
            {
                //auto timeBegin = esp_timer_get_time();
                freedv_tx(dv_, outputBuf, inputBuf);
                //auto timeEnd = esp_timer_get_time();
                //ESP_LOGI(CURRENT_LOG_TAG, "freedv_tx ran in %d us on %d samples and generated %d samples", (int)(timeEnd - timeBegin), numSpeechSamples, numModemSamples);
                
                codec2_fifo_write(outputFifo8K_, outputBuf, numModemSamples);
            }
        }
        else
        {
            short inputBuf[freedv_get_n_max_modem_samples(dv_)];
            short outputBuf[freedv_get_n_speech_samples(dv_)];
            int nin = freedv_nin(dv_);
        
            int rv = codec2_fifo_read(inputFifo8K_, inputBuf, nin);
            if (rv == 0)
            {
                //auto timeBegin = esp_timer_get_time();
                int nout = freedv_rx(dv_, outputBuf, inputBuf);
                //auto timeEnd = esp_timer_get_time();
                //ESP_LOGI(CURRENT_LOG_TAG, "freedv_rx ran in %lld us on %d samples and generated %d samples", timeEnd - timeBegin, nin, nout);
                codec2_fifo_write(outputFifo8K_, outputBuf, nout);
                nin = freedv_nin(dv_);
            }
        
            syncLed = freedv_get_sync(dv_) > 0;
        }

        // Broadcast current sync state
        FreeDVSyncStateMessage* message = new FreeDVSyncStateMessage(syncLed);
        publish(message);
        delete message;
    }
    
    // Upconvert back to 48K for the audio codec.
    while (codec2_fifo_used(outputFifo8K_) >= EIGHT_KHZ_BUF_SIZE)
    {
        short tempOut[FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP];
        codec2_fifo_read(outputFifo8K_, &outputFilterMemory_[FDMDV_OS_TAPS_48_8K], EIGHT_KHZ_BUF_SIZE);
        fdmdv_8_to_48_short(tempOut, &outputFilterMemory_[FDMDV_OS_TAPS_48_8K], EIGHT_KHZ_BUF_SIZE);
        codec2_fifo_write(codecOutputFifo, tempOut, FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP);
    }
}

void FreeDVTask::onSetFreeDVMode_(DVTask* origin, SetFreeDVModeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Setting FreeDV mode to %d", (int)message->mode);

    if (dv_ != nullptr)
    {
        if (rText_ != nullptr)
        {
            reliable_text_unlink_from_freedv(rText_);
            reliable_text_destroy(rText_);
            rText_ = nullptr;
        }

        freedv_close(dv_);
        dv_ = nullptr;
    }

    if (message->mode != SetFreeDVModeMessage::FreeDVMode::ANALOG)
    {
        int freedvApiMode = 0;
        switch (message->mode)
        {
            case SetFreeDVModeMessage::FREEDV_700D:
                freedvApiMode = FREEDV_MODE_700D;
                break;
            case SetFreeDVModeMessage::FREEDV_700E:
                freedvApiMode = FREEDV_MODE_700E;
                break;
            case SetFreeDVModeMessage::FREEDV_1600:
                freedvApiMode = FREEDV_MODE_1600;
                break;
            default:
                assert(0);
        }

        dv_ = freedv_open(freedvApiMode);
        assert(dv_ != nullptr);
        
        switch (freedvApiMode)
        {
            case FREEDV_MODE_700D:
                freedv_set_clip(dv_, 1);
                freedv_set_tx_bpf(dv_, 1);
                freedv_set_squelch_en(dv_, 1);
                freedv_set_snr_squelch_thresh(dv_, -2.0);  /* squelch at -2.0 dB      */
                break;
            case FREEDV_MODE_700E:
                freedv_set_clip(dv_, 1);
                freedv_set_tx_bpf(dv_, 1);
                freedv_set_squelch_en(dv_, 1);
                freedv_set_snr_squelch_thresh(dv_, 1);  /* squelch at 1.0 dB      */
                break;
            case FREEDV_MODE_1600:
                freedv_set_clip(dv_, 0);
                freedv_set_tx_bpf(dv_, 0);
                freedv_set_squelch_en(dv_, 0);
                break;
            default:
                assert(0);
                freedv_set_clip(dv_, 0);
                freedv_set_tx_bpf(dv_, 0);
                freedv_set_squelch_en(dv_, 1);
                freedv_set_snr_squelch_thresh(dv_, 0.0);  /* squelch at 0.0 dB      */
                break;
        }

        // Note: reliable_text setup is deferred until we know for sure whether
        // we have a valid callsign saved.
        storage::RequestReportingSettingsMessage requestReportingSettings;
        publish(&requestReportingSettings);
    }
}

void FreeDVTask::onSetPTTState_(DVTask* origin, FreeDVSetPTTStateMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Setting FreeDV transmit state to %d", (int)message->pttState);

    isTransmitting_ = message->pttState;
}

void FreeDVTask::onReportingSettingsUpdate_(DVTask* origin, storage::ReportingSettingsMessage* message)
{
    if (dv_ != nullptr && message->callsign != nullptr && strlen(message->callsign) > 0)
    {
        if (rText_ != nullptr)
        {
            reliable_text_unlink_from_freedv(rText_);
            reliable_text_destroy(rText_);
            rText_ = nullptr;
        }

        // Non-null callsign means we should set up reliable_text.
        rText_ = reliable_text_create();
        assert(rText_ != nullptr);

        reliable_text_set_string(rText_, message->callsign, strlen(message->callsign));
        reliable_text_use_with_freedv(rText_, dv_, OnReliableTextRx_, this);
    }
}

void FreeDVTask::OnReliableTextRx_(reliable_text_t rt, const char* txt_ptr, int length, void* state)
{
    // TBD: just output to console for now. Maybe we want to do something with the received
    // callsign one day.
    ESP_LOGI(CURRENT_LOG_TAG, "Received TX from %s", txt_ptr);

    FreeDVTask* task = (FreeDVTask*)state;
    reliable_text_reset(task->rText_);
}

}

}

// Implement required Codec2 math methods below as CMSIS doesn't work on ESP32.
extern "C"
{
    void codec2_dot_product_f32(float* left, float* right, size_t len, float* result)
    {
        dsps_dotprod_f32(left, right, result, len);
    }

    void codec2_complex_dot_product_f32(COMP* left, COMP* right, size_t len, float* resultReal, float* resultImag)
    {
        float realTimesRealResult = 0; // ac
        float realTimesImag1Result = 0; // bc
        float realTimesImag2Result = 0; // ad
        float imagTimesImagResult = 0; // bi * di
        
        dsps_dotprode_f32((float*)left, (float*)right, &realTimesRealResult, len, 2, 2);
        dsps_dotprode_f32((float*)left + 1, (float*)right, &realTimesImag1Result, len, 2, 2);
        dsps_dotprode_f32((float*)left, (float*)right + 1, &realTimesImag2Result, len, 2, 2);
        dsps_dotprode_f32((float*)left + 1, (float*)right + 1, &imagTimesImagResult, len, 2, 2);
        
        *resultReal = realTimesRealResult - imagTimesImagResult;
        *resultImag = realTimesImag1Result + realTimesImag2Result;
    }

    /* Required memory allocation wrapper for embedded platforms. For ezDV, we want to allocate as much as possible
    on external RAM. */

    void* codec2_malloc(size_t size)
    {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    }

    void* codec2_calloc(size_t nmemb, size_t size)
    {
        return heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    }

    void codec2_free(void* ptr)
    {
        heap_caps_free(ptr);
    }
}