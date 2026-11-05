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
#include <cstddef>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_timer.h"

#include "RadeReceiver.h"

extern "C"
{
#include "fargan.h"
#include "lpcnet.h"
#include "nnet_q8.h"
#include "rade_rx_v2.h"

// From q8_layers.c.
extern Q8Layer fargan_arrays_q8_layers[];
extern const int fargan_arrays_q8_count;
}

#define CURRENT_LOG_TAG ("RadeReceiver")

#define DEMOD_INPUT_SAMPLES (2400) /* 300 ms at 8 kHz */
#define MAX_QUEUED_LATENTS (8)     /* 320 ms of latents */
#define MAX_QUEUED_FRAMES (32)     /* 320 ms of features */
#define RECEIVE_TASK_STACK (16 * 1024) /* about 13.5 KB used: the decoder's conv layers take 1792 inputs (7 KB) */
#define DECODES_PER_REPORT (250)   /* 10 s of signal */
#define RECEIVE_TASK_PRIORITY (14)
#define RECEIVE_TASK_CORE (1)

#if CONFIG_EZDV_RADE_TEST_SIGNAL
extern const uint8_t testSignalStart_[] asm("_binary_rade_test_signal_s16_start");
extern const uint8_t testSignalEnd_[] asm("_binary_rade_test_signal_s16_end");
#endif // CONFIG_EZDV_RADE_TEST_SIGNAL

namespace ezdv
{

namespace audio
{

namespace
{

// Internal RAM that the receiver leaves for everything else. The 64 KB that
// ezDV reserves for Wi-Fi, DMA and the like (SPIRAM_MALLOC_RESERVE_INTERNAL)
// is only kept from malloc(): heap_caps_malloc() can take it, so this has to
// leave it alone itself.
#define INTERNAL_RAM_FLOOR (72 * 1024)
#define LOG_RAM(step) ESP_LOGI(CURRENT_LOG_TAG, "internal RAM free after %s: %d", step, (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL))

// Once ezDV has settled (Wi-Fi up, every task started), what start-up needed
// internal RAM for is known, so more of FARGAN's signal network can go there.
#define SETTLED_AFTER_BOOT_US (20 * 1000000LL)
#define SETTLED_INTERNAL_RAM_FLOOR (16 * 1024)

// FARGAN's signal network runs 400 times a second, so its weights, biases
// and scales go in internal RAM, where the decoder's weights streaming
// through the cache can't evict them.
bool IsSigNet_(const char* name)
{
    return strncmp(name, "sig_net_", 8) == 0;
}

// Copies what it can of FARGAN's signal network still in PSRAM to internal
// RAM. fargan_init() took copies of the layers, so those are updated too.
// Call from the task that runs FARGAN.
void MoveSigNetToInternalRam_(FARGANState* st, RadeMemory& memory)
{
    LinearLayer* copies = (LinearLayer*)&st->model;
    const int numCopies = sizeof(st->model) / sizeof(LinearLayer);
    size_t moved = 0, internal = 0;
    for (int i = 0; i < fargan_arrays_q8_count; i++)
    {
        Q8Layer* q = &fargan_arrays_q8_layers[i];
        if (!IsSigNet_(q->name))
        {
            continue;
        }

        LinearLayer* l = &q->layer;
        size_t vec = sizeof(float) * l->nb_outputs;
        struct { const void** table; size_t bytes; size_t offset; } arrays[] = {
            { (const void**)&l->weights, q->weight_bytes, offsetof(LinearLayer, weights) },
            { (const void**)&l->float_weights, sizeof(float) * l->nb_inputs * l->nb_outputs, offsetof(LinearLayer, float_weights) },
            { (const void**)&l->bias, vec, offsetof(LinearLayer, bias) },
            { (const void**)&l->scale, vec, offsetof(LinearLayer, scale) },
        };
        for (auto& a : arrays)
        {
            const void* old = *a.table;
            if (old == nullptr || a.bytes == 0)
            {
                continue;
            }
            const void* p = esp_ptr_internal(old) ? nullptr : memory.copyToInternal(old, a.bytes, SETTLED_INTERNAL_RAM_FLOOR);
            if (p == nullptr)
            {
                internal += esp_ptr_internal(old) ? a.bytes : 0;
                continue;
            }
            *a.table = p;
            int updated = 0;
            for (int j = 0; j < numCopies; j++)
            {
                const void** field = (const void**)((char*)&copies[j] + a.offset);
                if (*field == old)
                {
                    *field = p;
                    updated++;
                }
            }
            if (updated == 0)
            {
                ESP_LOGE(CURRENT_LOG_TAG, "moving %s went wrong (none of FARGAN's layers used it)", q->name);
            }
            moved += a.bytes;
            internal += a.bytes;
        }
    }
    ESP_LOGI(CURRENT_LOG_TAG, "moved %d KB of FARGAN's weights to internal RAM (%d KB there now); internal RAM free: %d",
             (int)(moved / 1024), (int)(internal / 1024), (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

}

#if CONFIG_EZDV_RADE_TEST_SIGNAL
const short* RadeReceiver::TestSignal_ = nullptr;
size_t RadeReceiver::TestSignalLen_ = 0;

void RadeReceiver::SetTestSignal(const short* samples, size_t length)
{
    TestSignal_ = samples;
    TestSignalLen_ = length;
}
#endif // CONFIG_EZDV_RADE_TEST_SIGNAL

RadeReceiver::RadeReceiver()
    : demodInput_(nullptr)
    , features_(nullptr)
    , receiveTaskHandle_(nullptr)
    , receiveTaskDone_(nullptr)
    , stopping_(false)
    , synced_(false)
    , rx_(nullptr)
    , fargan_(nullptr)
#if CONFIG_EZDV_RADE_TEST_SIGNAL
    , testSignalPos_(0)
#endif // CONFIG_EZDV_RADE_TEST_SIGNAL
{
    ESP_LOGI(CURRENT_LOG_TAG, "starting; internal RAM free: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Internal RAM, most important first: the receive task's stack and
    // control block (which can't be in PSRAM), FARGAN's state, then
    // (PlaceWeights_()) FARGAN's signal network weights.
    bool stackInternal, tcbInternal;
    StackType_t* stack = (StackType_t*)memory_.alloc(RECEIVE_TASK_STACK, true, INTERNAL_RAM_FLOOR, &stackInternal);
    StaticTask_t* tcb = (StaticTask_t*)memory_.alloc(sizeof(StaticTask_t), true, INTERNAL_RAM_FLOOR, &tcbInternal);
    fargan_ = memory_.alloc(sizeof(FARGANState), true, INTERNAL_RAM_FLOOR);
    StaticQueue_t* queueControl = (StaticQueue_t*)memory_.alloc(sizeof(StaticQueue_t), false, 0);
    uint8_t* queueStorage = (uint8_t*)memory_.alloc(MAX_QUEUED_FRAMES * sizeof(float) * NB_TOTAL_FEATURES, false, 0);
    StaticQueue_t* latentsQueueControl = (StaticQueue_t*)memory_.alloc(sizeof(StaticQueue_t), false, 0);
    uint8_t* latentsQueueStorage = (uint8_t*)memory_.alloc(MAX_QUEUED_LATENTS * sizeof(float) * RADE_V2_LATENT_DIM, false, 0);
    LOG_RAM("stack and FARGAN state");

    size_t placed = memory_.placeInternal(fargan_arrays_q8_layers, fargan_arrays_q8_count, IsSigNet_, INTERNAL_RAM_FLOOR);
    ESP_LOGI(CURRENT_LOG_TAG, "%d KB of FARGAN's weights in internal RAM", (int)(placed / 1024));

    // The models are copied out of the tables above here, so this has to
    // come after placeInternal().
    rx_ = memory_.alloc(sizeof(rade_rx_v2_state), false, 0);
    memset(rx_, 0, sizeof(rade_rx_v2_state));
    int rv = rade_rx_v2_init((rade_rx_v2_state*)rx_, 1);
    assert(rv == 0);
    LOG_RAM("rade_rx_v2_init");

    memset(fargan_, 0, sizeof(FARGANState));
    float zeros[FARGAN_CONT_SAMPLES] = {0};
    float warmup[5 * NB_TOTAL_FEATURES] = {0};
    fargan_init((FARGANState*)fargan_);
    fargan_cont((FARGANState*)fargan_, zeros, warmup);
    LOG_RAM("fargan_cont");

    demodInput_ = codec2_fifo_create(DEMOD_INPUT_SAMPLES);
    assert(demodInput_ != nullptr);
    LOG_RAM("the input FIFO");
    // Static, so that its storage can come from PSRAM if need be.
    features_ = xQueueCreateStatic(MAX_QUEUED_FRAMES, sizeof(float) * NB_TOTAL_FEATURES, queueStorage, queueControl);
    assert(features_ != nullptr);
    latents_ = xQueueCreateStatic(MAX_QUEUED_LATENTS, sizeof(float) * RADE_V2_LATENT_DIM, latentsQueueStorage, latentsQueueControl);
    assert(latents_ != nullptr);
    LOG_RAM("the queue");
    receiveTaskDone_ = xSemaphoreCreateBinary();
    assert(receiveTaskDone_ != nullptr);

    if (stackInternal && tcbInternal)
    {
        receiveTaskHandle_ = xTaskCreateStaticPinnedToCore(&ReceiveTaskEntry_, "RadeRxTask", RECEIVE_TASK_STACK, this, RECEIVE_TASK_PRIORITY, stack, tcb, RECEIVE_TASK_CORE);
    }
    else
    {
        // Carry on, silently.
        ESP_LOGE(CURRENT_LOG_TAG, "could not start the receive task (no internal RAM for its %d byte stack)", RECEIVE_TASK_STACK);
    }

    ESP_LOGI(CURRENT_LOG_TAG, "started; internal RAM free: %d (largest block %d)", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

RadeReceiver::~RadeReceiver()
{
    stopping_ = true;
    if (receiveTaskHandle_ != nullptr)
    {
        xTaskNotifyGive(receiveTaskHandle_);
        xSemaphoreTake(receiveTaskDone_, portMAX_DELAY);

        // Its stack and control block are ours to free, so delete it here
        // rather than leave it to the idle task.
        vTaskDelete(receiveTaskHandle_);
    }
    vSemaphoreDelete(receiveTaskDone_);

    vQueueDelete(latents_);
    vQueueDelete(features_);
    codec2_fifo_destroy(demodInput_);

    // memory_ frees the rest and puts FARGAN's layer table back.
}

void RadeReceiver::process(struct FIFO* input, struct FIFO* output)
{
    // Radio audio to the demodulator, in what size pieces it needs. If it
    // can't keep up, the oldest audio stays queued and the newest is dropped.
    short buf[FARGAN_FRAME_SIZE];
    int samplesIn = 0;
    while (codec2_fifo_read(input, buf, FARGAN_FRAME_SIZE / 2) == 0)
    {
#if CONFIG_EZDV_RADE_TEST_SIGNAL
        // Replace what the radio sent with the next part of the recording.
        const short* testSignal = TestSignal_ != nullptr ? TestSignal_ : (const short*)testSignalStart_;
        size_t testSignalLen = TestSignal_ != nullptr ? TestSignalLen_ : (testSignalEnd_ - testSignalStart_) / sizeof(short);
        for (int i = 0; i < FARGAN_FRAME_SIZE / 2; i++)
        {
            buf[i] = testSignal[testSignalPos_];
            testSignalPos_ = (testSignalPos_ + 1) % testSignalLen;
        }
#endif // CONFIG_EZDV_RADE_TEST_SIGNAL
        codec2_fifo_write(demodInput_, buf, FARGAN_FRAME_SIZE / 2);
        samplesIn += FARGAN_FRAME_SIZE / 2;
    }

    // Demodulate, and hand the latents to the receive task's decoder.
    rade_rx_v2_state* rx = (rade_rx_v2_state*)rx_;
    uint32_t t = esp_cpu_get_cycle_count();
    int nin = rade_rx_v2_nin(rx);
    while (codec2_fifo_read(demodInput_, rxIn_, nin) == 0)
    {
        for (int i = 0; i < nin; i++)
        {
            rxIq_[i].real = rxIn_[i] / 32767.0f;
            rxIq_[i].imag = 0;
        }
        int rv = rade_rx_v2_demod(rx, demodLatents_, rxIq_);
        synced_ = rx->state == RADE_RX_V2_SYNC;
        if ((rv & 1) && xQueueSend(latents_, demodLatents_, 0) != pdTRUE && (droppedLatents_++ % 100) == 0)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "the decoder isn't keeping up (%d latents dropped)", (int)droppedLatents_);
        }
        nin = rade_rx_v2_nin(rx);
    }
    demodCycles_ += esp_cpu_get_cycle_count() - t;

    // Speech from whatever features are ready: each 10 ms frame is 160
    // samples at 16 kHz.
    freeDVTaskStackUnused_ = uxTaskGetStackHighWaterMark(nullptr);
    float features[NB_TOTAL_FEATURES];
    while (codec2_fifo_free(output) >= FARGAN_FRAME_SIZE && xQueueReceive(features_, features, 0) == pdTRUE)
    {
        float pcm[FARGAN_FRAME_SIZE];
        short out[FARGAN_FRAME_SIZE];
        t = esp_cpu_get_cycle_count();
        fargan_synthesize((FARGANState*)fargan_, pcm, features);
        for (int i = 0; i < FARGAN_FRAME_SIZE; i++)
        {
            out[i] = (short)floorf(.5f + fminf(32767.f, fmaxf(-32767.f, 32768.f * pcm[i])));
        }
        farganCycles_ += esp_cpu_get_cycle_count() - t;
        farganFrames_++;
        codec2_fifo_write(output, out, FARGAN_FRAME_SIZE);
    }

    if (!movedWeights_ && esp_timer_get_time() > SETTLED_AFTER_BOOT_US)
    {
        MoveSigNetToInternalRam_((FARGANState*)fargan_, memory_);
        movedWeights_ = true;
    }

    // Out of sync: silence, at the rate audio comes in (16 kHz out for
    // every 8 kHz in).
    if (!synced_ && uxQueueMessagesWaiting(features_) == 0)
    {
        memset(buf, 0, sizeof(buf));
        while (samplesIn > 0 && codec2_fifo_free(output) >= FARGAN_FRAME_SIZE)
        {
            codec2_fifo_write(output, buf, FARGAN_FRAME_SIZE);
            samplesIn -= FARGAN_FRAME_SIZE / 2;
        }
    }
}

void RadeReceiver::ReceiveTaskEntry_(void* param)
{
    ((RadeReceiver*)param)->receiveTask_();
}

void RadeReceiver::receiveTask_()
{
    rade_rx_v2_state* rx = (rade_rx_v2_state*)rx_;
    float* features = rxFeatures_;
    int dropped = 0;
    int decodes = 0;
    int64_t reportTime = esp_timer_get_time();
    uint64_t decodeCycles = 0;
    uint32_t decodeWorst = 0;

    while (!stopping_)
    {
        if (xQueueReceive(latents_, decodeLatents_, pdMS_TO_TICKS(20)) != pdTRUE)
        {
            continue;
        }

        uint32_t t = esp_cpu_get_cycle_count();
        rade_rx_v2_decode(rx, features, decodeLatents_);
        t = esp_cpu_get_cycle_count() - t;
        decodeCycles += t;
        if (t > decodeWorst) decodeWorst = t;
        for (int f = 0; f < RADE_V2_FRAMES_PER_STEP; f++)
        {
            if (xQueueSend(features_, &features[f * NB_TOTAL_FEATURES], 0) != pdTRUE && (dropped++ % 100) == 0)
            {
                ESP_LOGW(CURRENT_LOG_TAG, "FARGAN isn't keeping up (%d frames dropped)", dropped);
            }
        }

        if (++decodes % DECODES_PER_REPORT == 0)
        {
            int64_t now = esp_timer_get_time();
            float secs = (now - reportTime) / 1e6f;
            float signalSecs = DECODES_PER_REPORT * RADE_V2_FRAMES_PER_STEP * 0.01f;
            float cycles = 240e6f * secs; // of one core
            uint64_t dc = demodCycles_.exchange(0);
            uint64_t fc = farganCycles_.exchange(0);
            ESP_LOGI(CURRENT_LOG_TAG, "over %.1f s: %.1f s of signal decoded (%.0f%% of real time); decoder %.0f%% of core 1 (worst %.1f ms), "
                     "demodulator %.0f%% and FARGAN %.0f%% of core 0; %d latents and %d frames dropped",
                     (double)secs, (double)signalSecs, (double)(100 * signalSecs / secs), (double)(100 * decodeCycles / cycles),
                     (double)(decodeWorst / 240e3f), (double)(100 * dc / cycles), (double)(100 * fc / cycles), (int)droppedLatents_, dropped);
            ESP_LOGI(CURRENT_LOG_TAG, "unused stack: RadeRxTask %d of %d bytes, FreeDVTask %d bytes; internal RAM free: %d (lowest %d)",
                     (int)uxTaskGetStackHighWaterMark(nullptr), RECEIVE_TASK_STACK, (int)freeDVTaskStackUnused_,
                     (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
            reportTime = now;
            decodeCycles = 0;
            decodeWorst = 0;
        }
    }

    xSemaphoreGive(receiveTaskDone_);
    vTaskSuspend(nullptr); // the destructor deletes this task
}

}

}
