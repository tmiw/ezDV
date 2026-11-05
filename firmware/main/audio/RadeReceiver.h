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

#ifndef RADE_RECEIVER_H
#define RADE_RECEIVER_H

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "codec2_fifo.h"
#include "RadeMemory.h"
extern "C"
{
#include "rade_rx_v2.h"
}

namespace ezdv
{

namespace audio
{

/**
 * RADE V2 receiver: 8 kHz audio from the radio in, 16 kHz speech out.
 *
 * RADE V2's decoder network runs in a task of its own on core 1, as it's the
 * heaviest part and most of its time is spent waiting on PSRAM. The
 * demodulator and FARGAN (features to speech) run in the caller's task
 * (FreeDVTask, on core 0), FARGAN from weights in internal RAM.
 */
class RadeReceiver
{
public:
    RadeReceiver();
    virtual ~RadeReceiver();

    /**
     * Takes all the radio audio waiting in input and adds whatever speech
     * is ready to output. Call regularly (every FreeDVTask tick).
     */
    void process(struct FIFO* input, struct FIFO* output);

    bool isSynced() const { return synced_; }

#if CONFIG_EZDV_RADE_TEST_SIGNAL
    /**
     * Receives this (looped) from the next RadeReceiver on, instead of the
     * built-in recording. It has to outlast them.
     */
    static void SetTestSignal(const short* samples, size_t length);
#endif // CONFIG_EZDV_RADE_TEST_SIGNAL

private:
    struct FIFO* demodInput_;     // radio audio for the receive task
    QueueHandle_t latents_;       // one decoder run's worth of latents per item
    QueueHandle_t features_;      // one 10 ms frame of features per item
    TaskHandle_t receiveTaskHandle_;
    SemaphoreHandle_t receiveTaskDone_;
    std::atomic<bool> stopping_;
    std::atomic<bool> synced_;
    std::atomic<UBaseType_t> freeDVTaskStackUnused_ {0}; // bytes, as of the last process() call
    std::atomic<uint64_t> demodCycles_ {0}; // since the last report
    std::atomic<uint64_t> farganCycles_ {0};
    std::atomic<int> droppedLatents_ {0};
    std::atomic<int> farganFrames_ {0};
    bool movedWeights_ = false; // the rest of FARGAN's to internal RAM, once ezDV has settled

    // The demodulator's and the receive task's buffers (here rather than on
    // their stacks).
    short rxIn_[RADE_V2_SYM_LEN * 2];
    RADE_COMP rxIq_[RADE_V2_SYM_LEN * 2];
    float demodLatents_[RADE_V2_LATENT_DIM];
    float decodeLatents_[RADE_V2_LATENT_DIM];
    float rxFeatures_[RADE_V2_FEATURES_OUT];

    RadeMemory memory_;
    void* rx_;                    // rade_rx_v2_state, in PSRAM
    void* fargan_;                // FARGANState, in internal RAM

#if CONFIG_EZDV_RADE_TEST_SIGNAL
    size_t testSignalPos_;
    static const short* TestSignal_;
    static size_t TestSignalLen_;
#endif // CONFIG_EZDV_RADE_TEST_SIGNAL

    static void ReceiveTaskEntry_(void* param);
    void receiveTask_();
};

}

}

#endif // RADE_RECEIVER_H
