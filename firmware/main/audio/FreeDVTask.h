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

#ifndef FREEDV_TASK_H
#define FREEDV_TASK_H

#include "AudioInput.h"
#include "Resampler.h"
#include "FreeDVMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

#include "RadeReceiver.h"
#include "RadeTransmitter.h"

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

/**
 * Analog pass-through (resampled), or RADE V2: a RadeReceiver, swapped for a
 * RadeTransmitter while transmitting.
 */
class FreeDVTask : public DVTask, public AudioInput
{
public:
    FreeDVTask();
    virtual ~FreeDVTask();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;
    
private:
    const void* models_; // RADE V2's weights, in PSRAM while it's in use
    RadeReceiver* radeRx_;
    RadeTransmitter* radeTx_;
    int currentMode_;

    bool isTransmitting_;
    bool isEndingTransmit_;
    bool isActive_;

    // For analog: the radio's audio is 8 kHz, the user's 16 kHz.
    Resampler analogUp_;
    Resampler analogDown_;

    void startRade_();
    void stopRade_();

#if CONFIG_EZDV_RADE_TX_SELF_TEST
    enum { SELF_TEST_WAITING, SELF_TEST_TRANSMITTING, SELF_TEST_DONE } selfTest_;
    int64_t selfTestStart_;
    size_t selfTestMicPos_;
    short* selfTestModem_;       // in PSRAM
    size_t selfTestModemLen_;
    struct FIFO* selfTestMic_;
    struct FIFO* selfTestOut_;

    bool selfTestTick_();
#endif // CONFIG_EZDV_RADE_TX_SELF_TEST

    void onSetFreeDVMode_(DVTask* origin, SetFreeDVModeMessage* message);
    void onSetPTTState_(DVTask* origin, FreeDVSetPTTStateMessage* message);
    void onRequestGetFreeDVMode_(DVTask* origin, RequestGetFreeDVModeMessage* message);
};

}

}

#endif // FREEDV_TASK_H
