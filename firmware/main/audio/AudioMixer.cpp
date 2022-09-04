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

#include "AudioMixer.h"

#define AUDIO_MIXER_TIMER_TICK_US 10000

namespace ezdv
{

namespace audio
{

AudioMixer::AudioMixer()
    : DVTask("AudioMixer", 10 /* TBD */, 4096, tskNO_AFFINITY, 100)
    , AudioInput(2, 1)
    , mixerTick_(this, std::bind(&AudioMixer::onTimerTick_, this), AUDIO_MIXER_TIMER_TICK_US)
{
    // empty
}

AudioMixer::~AudioMixer()
{
    mixerTick_.stop();
}

void AudioMixer::onTaskStart_()
{
    mixerTick_.start();
}

void AudioMixer::onTaskWake_()
{
    mixerTick_.start();
}

void AudioMixer::onTaskSleep_()
{
    mixerTick_.stop();
}

void AudioMixer::onTimerTick_()
{
    struct FIFO* leftInputFifo = getAudioInput(AudioInput::LEFT_CHANNEL);
    struct FIFO* rightInputFifo = getAudioInput(AudioInput::RIGHT_CHANNEL);
    struct FIFO* outputFifo = getAudioOutput(AudioInput::LEFT_CHANNEL);

    // Process on a sample by sample basis
    short bufLeft;
    short bufRight;
    while (codec2_fifo_used(leftInputFifo) > 0 || codec2_fifo_used(rightInputFifo) > 0)
    {
        bufLeft = 0;
        bufRight = 0;
        
        codec2_fifo_read(leftInputFifo, &bufLeft, 1);
        codec2_fifo_read(rightInputFifo, &bufRight, 1);
        
        // See https://dsp.stackexchange.com/questions/3581/algorithms-to-mix-audio-signals-without-clipping
        // for more info. This is basically (1/sqrt(2)) * (a + b)
        float addedSample = 0.707106 * (bufLeft + bufRight);
        short resultShort = (short)addedSample;
        
        codec2_fifo_write(outputFifo, &resultShort, 1);
    }
}

}

}