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

#include <cstdio>
#include <cstring>

#include "FreeDVTask.h"

#include "esp_dsp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "codec2_math.h"

extern "C"
{
#include "rade_models.h"
}

#define USER_SAMPLES_PER_LOOP (USER_SAMPLE_RATE / 100)   /* 10 ms */
#define RADIO_SAMPLES_PER_LOOP (RADIO_SAMPLE_RATE / 100)
#define ANALOG_RESAMPLER_CUTOFF_HZ (3700)
#define ANALOG_RESAMPLER_TAPS (64)
#define CURRENT_LOG_TAG ("FreeDV")

namespace ezdv
{

namespace audio
{

FreeDVTask::FreeDVTask()
    : DVTask("FreeDVTask", 15, 30 * 1024, 0, 16, pdMS_TO_TICKS(10)) /* RADE V2 transmit uses about 22 KB */
    , AudioInput(2, 2)
    , models_(nullptr)
    , radeRx_(nullptr)
    , radeTx_(nullptr)
    , currentMode_(0)
    , isTransmitting_(false)
    , isEndingTransmit_(false)
    , isActive_(false)
    , analogUp_(RADIO_SAMPLE_RATE, USER_SAMPLE_RATE, ANALOG_RESAMPLER_CUTOFF_HZ, ANALOG_RESAMPLER_TAPS)
    , analogDown_(USER_SAMPLE_RATE, RADIO_SAMPLE_RATE, ANALOG_RESAMPLER_CUTOFF_HZ, ANALOG_RESAMPLER_TAPS)
#if CONFIG_EZDV_RADE_TX_SELF_TEST
    , selfTest_(SELF_TEST_WAITING)
    , selfTestStart_(0)
    , selfTestMicPos_(0)
    , selfTestModem_(nullptr)
    , selfTestModemLen_(0)
    , selfTestMic_(nullptr)
    , selfTestOut_(nullptr)
#endif // CONFIG_EZDV_RADE_TX_SELF_TEST
{
    registerMessageHandler(this, &FreeDVTask::onSetFreeDVMode_);
    registerMessageHandler(this, &FreeDVTask::onSetPTTState_);
    registerMessageHandler(this, &FreeDVTask::onRequestGetFreeDVMode_);
}

FreeDVTask::~FreeDVTask()
{
    stopRade_();
}

void FreeDVTask::onTaskStart_()
{
    isActive_ = true;
}

void FreeDVTask::onTaskSleep_()
{
    isActive_ = false;
    stopRade_();
}

void FreeDVTask::startRade_()
{
    models_ = rade_models_load();
    if (models_ == nullptr)
    {
        // Nothing to run RADE V2 with: carry on as for analog.
        ESP_LOGE(CURRENT_LOG_TAG, "could not load RADE V2's weights from the models partition");
        return;
    }
    if (isTransmitting_)
    {
        radeTx_ = new RadeTransmitter();
        assert(radeTx_ != nullptr);
    }
    else
    {
        radeRx_ = new RadeReceiver();
        assert(radeRx_ != nullptr);
    }
}

void FreeDVTask::stopRade_()
{
    delete radeRx_;
    radeRx_ = nullptr;
    delete radeTx_;
    radeTx_ = nullptr;
    if (models_ != nullptr)
    {
        rade_models_unload(models_);
        models_ = nullptr;
    }
}

#if CONFIG_EZDV_RADE_TX_SELF_TEST
extern const uint8_t selfTestMicStart_[] asm("_binary_rade_test_mic_s16_start");
extern const uint8_t selfTestMicEnd_[] asm("_binary_rade_test_mic_s16_end");

#define SELF_TEST_AFTER_BOOT_US (30 * 1000000LL)
#define SELF_TEST_FIFO_SAMPLES (3200)
#define SELF_TEST_MODEM_SAMPLES(micLen) ((micLen) / 2 + RADIO_SAMPLE_RATE) /* 8 kHz, plus a second for the end-of-over */

// The self-test's modem signal over the console, in base64 ("PCM <line>"
// ... "PCM END"), so that it can be decoded on the desktop.
static void PrintBase64_(const uint8_t* data, size_t len)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[4 * 192 / 3 + 1];
    for (size_t off = 0; off < len; off += 192)
    {
        size_t n = len - off < 192 ? len - off : 192, o = 0;
        for (size_t i = 0; i < n; i += 3)
        {
            uint32_t v = data[off + i] << 16 | (i + 1 < n ? data[off + i + 1] << 8 : 0) | (i + 2 < n ? data[off + i + 2] : 0);
            line[o++] = tab[v >> 18 & 63];
            line[o++] = tab[v >> 12 & 63];
            line[o++] = i + 1 < n ? tab[v >> 6 & 63] : '=';
            line[o++] = i + 2 < n ? tab[v & 63] : '=';
        }
        line[o] = 0;
        printf("PCM %s\n", line);
        if ((off / 192) % 64 == 63) vTaskDelay(1);
    }
    printf("PCM END\n");
}

// See EZDV_RADE_TX_SELF_TEST. Returns true while it has the tick.
bool FreeDVTask::selfTestTick_()
{
    const short* mic = (const short*)selfTestMicStart_;
    size_t micLen = (selfTestMicEnd_ - selfTestMicStart_) / sizeof(short);

    if (selfTest_ == SELF_TEST_WAITING)
    {
        if (radeRx_ == nullptr || isTransmitting_ || esp_timer_get_time() < SELF_TEST_AFTER_BOOT_US)
        {
            return false;
        }
        ESP_LOGI(CURRENT_LOG_TAG, "self-test: transmitting %.1f s of mic audio into memory", micLen / (float)USER_SAMPLE_RATE);
        delete radeRx_;
        radeRx_ = nullptr;
        radeTx_ = new RadeTransmitter();
        assert(radeTx_ != nullptr);
        selfTestModem_ = (short*)heap_caps_malloc(SELF_TEST_MODEM_SAMPLES(micLen) * sizeof(short), MALLOC_CAP_SPIRAM);
        assert(selfTestModem_ != nullptr);
        selfTestMic_ = codec2_fifo_create(SELF_TEST_FIFO_SAMPLES);
        selfTestOut_ = codec2_fifo_create(SELF_TEST_FIFO_SAMPLES);
        assert(selfTestMic_ != nullptr && selfTestOut_ != nullptr);
        selfTestStart_ = esp_timer_get_time();
        selfTest_ = SELF_TEST_TRANSMITTING;
    }
    if (selfTest_ != SELF_TEST_TRANSMITTING)
    {
        return false;
    }

    // The mic at real time; the modem signal into memory.
    size_t due = (esp_timer_get_time() - selfTestStart_) * USER_SAMPLE_RATE / 1000000;
    while (selfTestMicPos_ < due && selfTestMicPos_ + USER_SAMPLES_PER_LOOP <= micLen && codec2_fifo_free(selfTestMic_) >= USER_SAMPLES_PER_LOOP)
    {
        codec2_fifo_write(selfTestMic_, (short*)&mic[selfTestMicPos_], USER_SAMPLES_PER_LOOP);
        selfTestMicPos_ += USER_SAMPLES_PER_LOOP;
    }
    if (selfTestMicPos_ + USER_SAMPLES_PER_LOOP > micLen)
    {
        radeTx_->finish();
    }
    radeTx_->process(selfTestMic_, selfTestOut_);
    int n = codec2_fifo_used(selfTestOut_);
    if (n > 0 && selfTestModemLen_ + n <= SELF_TEST_MODEM_SAMPLES(micLen))
    {
        codec2_fifo_read(selfTestOut_, &selfTestModem_[selfTestModemLen_], n);
        selfTestModemLen_ += n;
    }

    if (radeTx_->done())
    {
        ESP_LOGI(CURRENT_LOG_TAG, "self-test: %.1f s of modem signal in %.1f s; now receiving it", selfTestModemLen_ / (float)RADIO_SAMPLE_RATE,
                 (esp_timer_get_time() - selfTestStart_) / 1e6);
        delete radeTx_;
        radeTx_ = nullptr;
        codec2_fifo_destroy(selfTestMic_);
        codec2_fifo_destroy(selfTestOut_);
        PrintBase64_((const uint8_t*)selfTestModem_, selfTestModemLen_ * sizeof(short));
        RadeReceiver::SetTestSignal(selfTestModem_, selfTestModemLen_);
        radeRx_ = new RadeReceiver();
        assert(radeRx_ != nullptr);
        selfTest_ = SELF_TEST_DONE;
    }
    return true;
}
#endif // CONFIG_EZDV_RADE_TX_SELF_TEST

void FreeDVTask::onTaskTick_()
{
    if (!isActive_) return;

#if CONFIG_EZDV_RADE_TX_SELF_TEST
    if (selfTestTick_()) return;
#endif // CONFIG_EZDV_RADE_TX_SELF_TEST

    bool syncLed = false;

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

    if (radeTx_ != nullptr)
    {
        radeTx_->process(codecInputFifo, codecOutputFifo);
        if (radeTx_->done())
        {
            // The end-of-over is out: back to receiving. Only one of the
            // two at a time, as both want the spare internal RAM.
            delete radeTx_;
            radeTx_ = nullptr;
            radeRx_ = new RadeReceiver();
            assert(radeRx_ != nullptr);

            TransmitCompleteMessage message;
            publish(&message);

            isEndingTransmit_ = false;
            isTransmitting_ = false;
        }
    }
    else if (radeRx_ != nullptr)
    {
        radeRx_->process(codecInputFifo, codecOutputFifo);
        syncLed = radeRx_->isSynced();
    }
    else
    {
        // Analog mode, just pipe through the audio (resampled: the radio's
        // is 8 kHz and the user's 16 kHz).
        Resampler& resampler = isTransmitting_ ? analogDown_ : analogUp_;
        int numIn = isTransmitting_ ? USER_SAMPLES_PER_LOOP : RADIO_SAMPLES_PER_LOOP;
        short inputBuf[USER_SAMPLES_PER_LOOP];
        short outputBuf[USER_SAMPLES_PER_LOOP];

        while (!(isTransmitting_ && isEndingTransmit_) && 
               codec2_fifo_free(codecOutputFifo) >= USER_SAMPLES_PER_LOOP &&
               codec2_fifo_read(codecInputFifo, inputBuf, numIn) == 0)
        {
            int numOut = resampler.process(inputBuf, numIn, outputBuf);
            codec2_fifo_write(codecOutputFifo, outputBuf, numOut);
        }

        if (isTransmitting_ && isEndingTransmit_)
        {
            // We've finished processing everything that's left, end TX now.
            TransmitCompleteMessage message;
            publish(&message);

            isEndingTransmit_ = false;
            isTransmitting_ = false;
        }
    }

    // Broadcast current sync state
    FreeDVSyncStateMessage* message = new FreeDVSyncStateMessage(syncLed);
    publish(message);
    delete message;
}

void FreeDVTask::onSetFreeDVMode_(DVTask* origin, SetFreeDVModeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Setting FreeDV mode to %d", (int)message->mode);
    currentMode_ = (int)message->mode;

    stopRade_();
    if (message->mode == FreeDVMode::RADE_V2)
    {
        startRade_();
    }
}

void FreeDVTask::onSetPTTState_(DVTask* origin, FreeDVSetPTTStateMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Setting FreeDV transmit state to %d", (int)message->pttState);

    if (isTransmitting_ && !message->pttState)
    {
        // Delay ending TX until we've processed what's remaining: for RADE
        // V2, until the end-of-over has gone out.
        isEndingTransmit_ = true;
        if (radeTx_ != nullptr)
        {
            radeTx_->finish();
        }
    }
    else if (!isTransmitting_ && message->pttState)
    {
        isEndingTransmit_ = false;
        isTransmitting_ = true;
        if (radeRx_ != nullptr)
        {
            delete radeRx_;
            radeRx_ = nullptr;
            radeTx_ = new RadeTransmitter();
            assert(radeTx_ != nullptr);
        }

        // Nothing reads the mic while receiving, so it holds up to a FIFO's
        // worth of audio from before PTT: drop it rather than send it late.
        struct FIFO* micFifo = getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL);
        short discard[USER_SAMPLES_PER_LOOP];
        while (micFifo != nullptr && codec2_fifo_read(micFifo, discard, USER_SAMPLES_PER_LOOP) == 0)
        {
            // empty
        }
    }
    else if (!message->pttState)
    {
        TransmitCompleteMessage message;
        publish(&message);
    }
}

void FreeDVTask::onRequestGetFreeDVMode_(DVTask* origin, RequestGetFreeDVModeMessage* message)
{
    SetFreeDVModeMessage msg((FreeDVMode)currentMode_);
    origin->post(&msg);
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
