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

#ifndef RADE_TRANSMITTER_H
#define RADE_TRANSMITTER_H

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "codec2_fifo.h"
#include "RadeMemory.h"
extern "C"
{
#include "lpcnet.h"
#include "rade_tx_v2.h"
}

namespace ezdv
{

namespace audio
{

/**
 * RADE V2 transmitter: 16 kHz mic audio in, 8 kHz modem signal out.
 *
 * Opus's feature extraction (with PitchDNN), every 10 ms of mic audio, runs
 * in the caller's task (FreeDVTask, on core 0). RADE V2's encoder network
 * and the modulator, every 40 ms, run in a task of their own on core 1.
 *
 * Only one of this and RadeReceiver exists at a time: both want what spare
 * internal RAM there is.
 */
class RadeTransmitter
{
public:
    RadeTransmitter();
    virtual ~RadeTransmitter();

    /**
     * Takes all the mic audio waiting in input and adds whatever modem
     * signal is ready to output. Call regularly (every FreeDVTask tick).
     */
    void process(struct FIFO* input, struct FIFO* output);

    /**
     * Ends the transmission: no more mic audio is taken, and once what's
     * already been encoded is out, the end-of-over and some silence (so that
     * the radio sends all of it) follow. Then done() is true.
     */
    void finish();
    bool done() const { return done_; }

private:
    RadeMemory memory_;

    QueueHandle_t steps_;        // one encoder run's worth of features per item
    struct FIFO* modemOutput_;   // from the encode task, for process() to pass on
    TaskHandle_t encodeTaskHandle_;
    SemaphoreHandle_t encodeTaskDone_;
    std::atomic<bool> stopping_;
    std::atomic<bool> eooQueued_;
    std::atomic<bool> eooWritten_;
    bool finishing_;
    bool done_;

    LPCNetEncState* enc_;        // feature extraction (in internal RAM if there's room)
    rade_tx_v2_state* tx_;       // encoder and modulator
    float features_[RADE_V2_FRAMES_PER_STEP * NB_TOTAL_FEATURES];
    int numFrames_;              // in features_

    // The encode task's buffers (here rather than on its stack).
    float encodeFeatures_[RADE_V2_FRAMES_PER_STEP * NB_TOTAL_FEATURES];
    RADE_COMP encodeIq_[RADE_V2_NEOO];
    short encodeOut_[RADE_V2_NEOO];

    // Timing, for the log.
    std::atomic<uint64_t> featureCycles_ {0}; // since the last report
    std::atomic<UBaseType_t> freeDVTaskStackUnused_ {0};

    static void EncodeTaskEntry_(void* param);
    void encodeTask_();
};

}

}

#endif // RADE_TRANSMITTER_H
