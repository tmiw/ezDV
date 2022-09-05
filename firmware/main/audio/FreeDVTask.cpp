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

#include "esp_dsp.h"
#include "codec2_math.h"
#include "freedv_api.h"

#include "FreeDVTask.h"

#define FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP 160
#define CURRENT_LOG_TAG ("FreeDV")

namespace ezdv
{

namespace audio
{

FreeDVTask::FreeDVTask()
    : DVTask("FreeDVTask", 10 /* TBD */, 48000, 1, 10)
    , AudioInput(2, 2)
    , dv_(nullptr)
    , isTransmitting_(false)
    , isActive_(false)
{
    registerMessageHandler(this, &FreeDVTask::onSetFreeDVMode_);
    registerMessageHandler(this, &FreeDVTask::onSetPTTState_);
}

FreeDVTask::~FreeDVTask()
{
    if (dv_ != nullptr)
    {
        freedv_close(dv_);
        dv_ = nullptr;
    }
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

    if (dv_ == nullptr)
    {
        // Analog mode, just pipe through the audio.
        short inputBuf[FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP];
        memset(inputBuf, 0, sizeof(inputBuf));
        
        while (codec2_fifo_used(codecInputFifo) >= FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP)
        {
            codec2_fifo_read(codecInputFifo, inputBuf, FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP);
            codec2_fifo_write(codecOutputFifo, inputBuf, FREEDV_ANALOG_NUM_SAMPLES_PER_LOOP);
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
        
            int rv = codec2_fifo_read(codecInputFifo, inputBuf, numSpeechSamples);
            if (rv == 0)
            {
                auto timeBegin = esp_timer_get_time();
                freedv_tx(dv_, outputBuf, inputBuf);
                auto timeEnd = esp_timer_get_time();
                ESP_LOGI(CURRENT_LOG_TAG, "freedv_tx ran in %d us on %d samples and generated %d samples", (int)(timeEnd - timeBegin), numSpeechSamples, numModemSamples);
                codec2_fifo_write(codecOutputFifo, outputBuf, numModemSamples);
            }
        }
        else
        {
            short inputBuf[freedv_get_n_max_modem_samples(dv_)];
            short outputBuf[freedv_get_n_speech_samples(dv_)];
            int nin = freedv_nin(dv_);
        
            int rv = codec2_fifo_read(codecInputFifo, inputBuf, nin);
            if (rv == 0)
            {
                auto timeBegin = esp_timer_get_time();
                int nout = freedv_rx(dv_, outputBuf, inputBuf);
                auto timeEnd = esp_timer_get_time();
                //ESP_LOGI(CURRENT_LOG_TAG, "freedv_rx ran in %lld us on %d samples and generated %d samples", timeEnd - timeBegin, nin, nout);
                codec2_fifo_write(codecOutputFifo, outputBuf, nout);
                nin = freedv_nin(dv_);
            }
        
            syncLed = freedv_get_sync(dv_) > 0;
        }

        // Broadcast current sync state
        FreeDVSyncStateMessage* message = new FreeDVSyncStateMessage(syncLed);
        publish(message);
        delete message;
    }
}

void FreeDVTask::onSetFreeDVMode_(DVTask* origin, SetFreeDVModeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Setting FreeDV mode to %d", (int)message->mode);

    if (dv_ != nullptr)
    {
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
    }
}

void FreeDVTask::onSetPTTState_(DVTask* origin, FreeDVSetPTTStateMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Setting FreeDV transmit state to %d", (int)message->pttState);

    isTransmitting_ = message->pttState;

    struct FIFO* leftInputFifo = getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL);
    struct FIFO* rightInputFifo = getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL);

    // Reset fifos by just reading all the contained data.
    // TBD -- there really should be a reset function in codec2
    int bytesUsed = codec2_fifo_used(leftInputFifo);
    if (bytesUsed > 0)
    {
        short* tmp = new short[bytesUsed];
        assert(tmp != nullptr);
        codec2_fifo_read(leftInputFifo, tmp, bytesUsed);
        delete tmp;
    }

    bytesUsed = codec2_fifo_used(rightInputFifo);
    if (bytesUsed > 0)
    {
        short* tmp = new short[bytesUsed];
        assert(tmp != nullptr);
        codec2_fifo_read(rightInputFifo, tmp, bytesUsed);
        delete tmp;
    }

}

}

}

// Implement required Codec2 math methods below as CMSIS doesn't work on ESP32.

void codec2_dot_product_f32(float* left, float* right, size_t len, float* result)
{
    dsps_dotprod_f32(left, right, result, len);
}

void codec2_complex_dot_product_f32(float* left, float* right, size_t len, float* resultReal, float* resultImag)
{
    // Complex number math: (a + bi)(c + di) = ac + bci + adi + (bi)(di) = ac - bd + (bc + ad)i
    float realTimesRealResult = 0; // ac
    float realTimesImag1Result = 0; // bci
    float realTimesImag2Result = 0; // adi
    float imagTimesImagResult = 0; // bi * di
    
    dsps_dotprode_f32(left, right, &realTimesRealResult, len, 0, 0);
    dsps_dotprode_f32(left, right, &realTimesImag1Result, len, 1, 0);
    dsps_dotprode_f32(left, right, &realTimesImag2Result, len, 0, 1);
    dsps_dotprode_f32(left, right, &imagTimesImagResult, len, 1, 1);
    
    *resultReal = realTimesRealResult - imagTimesImagResult;
    *resultImag = realTimesImag1Result + realTimesImag2Result;
}