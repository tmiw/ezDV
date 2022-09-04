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

#include "freedv_api.h"

#include "FreeDVTask.h"

#define FREEDV_TIMER_TICK_US 20000
#define CURRENT_LOG_TAG ("FreeDV")

namespace ezdv
{

namespace audio
{

FreeDVTask::FreeDVTask()
    : DVTask("FreeDVTask", 10 /* TBD */, 48000, 1, 100)
    , AudioInput(2, 2)
    , freedvTick_(this, std::bind(&FreeDVTask::onTimerTick_, this), FREEDV_TIMER_TICK_US)
    , dv_(nullptr)
    , isTransmitting_(false)
{
    // empty
}

FreeDVTask::~FreeDVTask()
{
    if (dv_ != nullptr)
    {
        freedv_close(dv_);
        dv_ = nullptr;
    }
}

void FreeDVTask::onTaskStart_(DVTask* origin, TaskStartMessage* message)
{
    freedvTick_.start();
}

void FreeDVTask::onTaskWake_(DVTask* origin, TaskWakeMessage* message)
{
    onTaskStart_(origin, nullptr);
}

void FreeDVTask::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    freedvTick_.stop();

    if (dv_ != nullptr)
    {
        freedv_close(dv_);
        dv_ = nullptr;
    }
}

void FreeDVTask::onTimerTick_()
{
    //ESP_LOGI(CURRENT_LOG_TAG, "timer tick");

    // TBD -- just loopback for now to make sure it's working
    struct FIFO* leftInputFifo = getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL);
    struct FIFO* rightInputFifo = getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL);
    struct FIFO* leftOutputFifo = getAudioOutput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL);
    struct FIFO* rightOutputFifo = getAudioOutput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL);

    short tmp[160];
    int numSamples = sizeof(tmp) / sizeof(short);
    while (codec2_fifo_used(leftInputFifo) >= numSamples)
    {
        codec2_fifo_read(leftInputFifo, tmp, numSamples);
        codec2_fifo_write(leftOutputFifo, tmp, numSamples);
    }

    while (codec2_fifo_used(rightInputFifo) >= numSamples)
    {
        codec2_fifo_read(rightInputFifo, tmp, numSamples);
        codec2_fifo_write(rightOutputFifo, tmp, numSamples);
    }
}

}

}