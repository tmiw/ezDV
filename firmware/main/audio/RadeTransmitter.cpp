/*
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2026 Mooneer Salem
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

#include <cassert>
#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_timer.h"

#include "RadeTransmitter.h"

extern "C"
{
#include "lpcnet_private.h"
#include "nnet_q8.h"

// From q8_layers.c.
extern Q8Layer pitchdnn_arrays_q8_layers[];
extern const int pitchdnn_arrays_q8_count;
}

#define CURRENT_LOG_TAG ("RadeTransmitter")

#define QUEUED_STEPS (4)              /* 160 ms of features */
#define MODEM_OUTPUT_SAMPLES (8000)   /* 1 s at 8 kHz: room for the end-of-over and silence */
#define ENCODE_TASK_STACK (16 * 1024)
#define ENCODE_TASK_PRIORITY (14)
#define ENCODE_TASK_CORE (1)
#define ENCODES_PER_REPORT (250)      /* 10 s */
#define MIC_SAMPLES_PER_FRAME (LPCNET_FRAME_SIZE)  /* 10 ms at 16 kHz */

#define SCALE (16383.0f)              /* as freedv-backend's RADETransmitStep */
#define SILENCE_AFTER_EOO (1600)      /* 200 ms, as freedv-backend: so that the radio sends all of the end-of-over */

// Internal RAM left for everything else: as for RadeReceiver, more while
// ezDV is still starting up (Wi-Fi needs it then).
#define STARTUP_INTERNAL_RAM_FLOOR (72 * 1024)
#define SETTLED_INTERNAL_RAM_FLOOR (16 * 1024)
#define SETTLED_AFTER_BOOT_US (20 * 1000000LL)

#define LOG_RAM(step) ESP_LOGI(CURRENT_LOG_TAG, "internal RAM free after %s: %d", step, (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL))

namespace ezdv
{

namespace audio
{

namespace
{

bool All_(const char* name)
{
    (void)name;
    return true;
}

}

RadeTransmitter::RadeTransmitter()
    : steps_(nullptr)
    , modemOutput_(nullptr)
    , encodeTaskHandle_(nullptr)
    , encodeTaskDone_(nullptr)
    , stopping_(false)
    , eooQueued_(false)
    , eooWritten_(false)
    , finishing_(false)
    , done_(false)
    , enc_(nullptr)
    , tx_(nullptr)
    , numFrames_(0)
{
    size_t floor = esp_timer_get_time() > SETTLED_AFTER_BOOT_US ? SETTLED_INTERNAL_RAM_FLOOR : STARTUP_INTERNAL_RAM_FLOOR;
    ESP_LOGI(CURRENT_LOG_TAG, "starting; internal RAM free: %d (largest block %d)", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    // Internal RAM, most important first: the encode task's stack and
    // control block (which can't be in PSRAM), then PitchDNN (about 15% of
    // core 0 less than from PSRAM) and feature extraction's state.
    bool stackInternal, tcbInternal;
    StackType_t* stack = (StackType_t*)memory_.alloc(ENCODE_TASK_STACK, true, floor, &stackInternal);
    StaticTask_t* tcb = (StaticTask_t*)memory_.alloc(sizeof(StaticTask_t), true, floor, &tcbInternal);
    size_t placed = memory_.placeInternal(pitchdnn_arrays_q8_layers, pitchdnn_arrays_q8_count, All_, floor);
    ESP_LOGI(CURRENT_LOG_TAG, "%d KB of PitchDNN's weights in internal RAM", (int)(placed / 1024));
    bool encInternal;
    enc_ = (LPCNetEncState*)memory_.alloc(sizeof(LPCNetEncState), true, floor, &encInternal);
    ESP_LOGI(CURRENT_LOG_TAG, "feature extraction state (%d bytes) in %s", (int)sizeof(LPCNetEncState), encInternal ? "internal RAM" : "PSRAM");
    LOG_RAM("placing weights");

    // The models are copied out of the tables above here, so this has to
    // come after placeInternal().
    lpcnet_encoder_init(enc_);
    tx_ = (rade_tx_v2_state*)memory_.alloc(sizeof(rade_tx_v2_state), false, 0);
    int rv = rade_tx_v2_init(tx_, 1);
    assert(rv == 0);
    LOG_RAM("initialising");

    StaticQueue_t* queueControl = (StaticQueue_t*)memory_.alloc(sizeof(StaticQueue_t), false, 0);
    uint8_t* queueStorage = (uint8_t*)memory_.alloc(QUEUED_STEPS * sizeof(features_), false, 0);
    steps_ = xQueueCreateStatic(QUEUED_STEPS, sizeof(features_), queueStorage, queueControl);
    assert(steps_ != nullptr);
    modemOutput_ = codec2_fifo_create(MODEM_OUTPUT_SAMPLES);
    assert(modemOutput_ != nullptr);
    encodeTaskDone_ = xSemaphoreCreateBinary();
    assert(encodeTaskDone_ != nullptr);

    if (stackInternal && tcbInternal)
    {
        encodeTaskHandle_ = xTaskCreateStaticPinnedToCore(&EncodeTaskEntry_, "RadeTxTask", ENCODE_TASK_STACK, this, ENCODE_TASK_PRIORITY, stack, tcb, ENCODE_TASK_CORE);
    }
    else
    {
        // Carry on, sending silence.
        ESP_LOGE(CURRENT_LOG_TAG, "could not start the encode task (no internal RAM for its %d byte stack)", ENCODE_TASK_STACK);
    }

    ESP_LOGI(CURRENT_LOG_TAG, "started; internal RAM free: %d (largest block %d)", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

RadeTransmitter::~RadeTransmitter()
{
    stopping_ = true;
    if (encodeTaskHandle_ != nullptr)
    {
        xSemaphoreTake(encodeTaskDone_, portMAX_DELAY);

        // Its stack and control block are ours to free, so delete it here
        // rather than leave it to the idle task.
        vTaskDelete(encodeTaskHandle_);
    }
    vSemaphoreDelete(encodeTaskDone_);
    vQueueDelete(steps_);
    codec2_fifo_destroy(modemOutput_);

    // memory_ frees the rest and puts the layer tables back.
}

void RadeTransmitter::process(struct FIFO* input, struct FIFO* output)
{
    // Features for every 10 ms of mic audio; every four frames, to the
    // encode task. Once finishing, the mic is left alone.
    short mic[MIC_SAMPLES_PER_FRAME];
    while (!finishing_ && codec2_fifo_read(input, mic, MIC_SAMPLES_PER_FRAME) == 0)
    {
        float pcm[LPCNET_FRAME_SIZE];
        float* f = &features_[numFrames_ * NB_TOTAL_FEATURES];
        uint32_t t = esp_cpu_get_cycle_count();
        for (int i = 0; i < MIC_SAMPLES_PER_FRAME; i++)
        {
            pcm[i] = mic[i];
        }
        lpcnet_compute_single_frame_features_float(enc_, pcm, f, 0);
        featureCycles_ += esp_cpu_get_cycle_count() - t;
        if (++numFrames_ == RADE_V2_FRAMES_PER_STEP)
        {
            numFrames_ = 0;
            if (xQueueSend(steps_, features_, 0) != pdTRUE)
            {
                ESP_LOGW(CURRENT_LOG_TAG, "the encoder isn't keeping up: 40 ms of speech dropped");
            }
        }
    }
    freeDVTaskStackUnused_ = uxTaskGetStackHighWaterMark(nullptr);

    // The modem signal, as it's ready and there's room for it.
    short buf[160];
    for (;;)
    {
        int n = codec2_fifo_used(modemOutput_);
        int room = codec2_fifo_free(output);
        n = n < room ? n : room;
        n = n < (int)(sizeof(buf) / sizeof(buf[0])) ? n : (int)(sizeof(buf) / sizeof(buf[0]));
        if (n <= 0 || codec2_fifo_read(modemOutput_, buf, n) != 0)
        {
            break;
        }
        codec2_fifo_write(output, buf, n);
    }

    if (finishing_ && !eooQueued_)
    {
        // After whatever's still queued for the encoder (a part-filled step
        // of features is dropped: at most 30 ms).
        features_[0] = NAN;
        if (xQueueSend(steps_, features_, 0) == pdTRUE)
        {
            eooQueued_ = true;
        }
    }
    if ((eooWritten_ && codec2_fifo_used(modemOutput_) == 0) || (finishing_ && encodeTaskHandle_ == nullptr))
    {
        done_ = true;
    }
}

void RadeTransmitter::finish()
{
    finishing_ = true;
}

void RadeTransmitter::EncodeTaskEntry_(void* param)
{
    ((RadeTransmitter*)param)->encodeTask_();
}

void RadeTransmitter::encodeTask_()
{
    int encodes = 0;
    int dropped = 0;
    int64_t reportTime = esp_timer_get_time();
    uint64_t encodeCycles = 0;
    uint32_t encodeWorst = 0;

    while (!stopping_)
    {
        if (xQueueReceive(steps_, encodeFeatures_, pdMS_TO_TICKS(20)) != pdTRUE)
        {
            continue;
        }

        int n;
        bool eoo = std::isnan(encodeFeatures_[0]);
        uint32_t t = esp_cpu_get_cycle_count();
        if (eoo)
        {
            n = rade_tx_v2_eoo(tx_, encodeIq_);
        }
        else
        {
            n = rade_tx_v2_process(tx_, encodeIq_, encodeFeatures_);
        }
        t = esp_cpu_get_cycle_count() - t;
        encodeCycles += t;
        if (t > encodeWorst) encodeWorst = t;

        // An SSB radio only needs the real part.
        for (int i = 0; i < n; i++)
        {
            encodeOut_[i] = (short)lrintf(fmaxf(-32767.0f, fminf(32767.0f, encodeIq_[i].real * SCALE)));
        }
        if (codec2_fifo_write(modemOutput_, encodeOut_, n) != 0 && (dropped++ % 100) == 0)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "the radio isn't taking the modem signal (%d blocks dropped)", dropped);
        }
        if (eoo)
        {
            memset(encodeOut_, 0, sizeof(encodeOut_));
            for (int left = SILENCE_AFTER_EOO; left > 0; left -= RADE_V2_NEOO)
            {
                codec2_fifo_write(modemOutput_, encodeOut_, left < RADE_V2_NEOO ? left : RADE_V2_NEOO);
            }
            eooWritten_ = true;
        }

        if (++encodes % ENCODES_PER_REPORT == 0)
        {
            int64_t now = esp_timer_get_time();
            float secs = (now - reportTime) / 1e6f;
            float cycles = 240e6f * secs; // of one core
            uint64_t fc = featureCycles_.exchange(0);
            ESP_LOGI(CURRENT_LOG_TAG, "over %.1f s: encoder %.0f%% of core 1 (worst %.1f ms of 40), feature extraction %.0f%% of core 0",
                     (double)secs, (double)(100 * encodeCycles / cycles), (double)(encodeWorst / 240e3f), (double)(100 * fc / cycles));
            ESP_LOGI(CURRENT_LOG_TAG, "unused stack: RadeTxTask %d of %d bytes, FreeDVTask %d bytes; internal RAM free: %d (lowest %d)",
                     (int)uxTaskGetStackHighWaterMark(nullptr), ENCODE_TASK_STACK, (int)freeDVTaskStackUnused_,
                     (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
            reportTime = now;
            encodeCycles = 0;
            encodeWorst = 0;
        }
    }

    xSemaphoreGive(encodeTaskDone_);
    vTaskSuspend(nullptr); // the destructor deletes this task
}

}

}
