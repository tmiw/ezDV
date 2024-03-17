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

#define AUDIO_MIXER_TIMER_TICK_US (20000)
#define AUDIO_MIXER_NUM_SAMPLES_PER_INTERVAL 160

namespace ezdv
{

namespace audio
{

AudioMixer::AudioMixer()
    : DVTask("AudioMixer", 15, 3144, tskNO_AFFINITY, pdMS_TO_TICKS(20))
    , AudioInput(2, 1)
    , mixerTick_(this, std::bind(&AudioMixer::onTimerTick_, this), AUDIO_MIXER_TIMER_TICK_US, "AudioMixerTimer")
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

void AudioMixer::onTaskSleep_()
{
    mixerTick_.stop();

    // Flush anything remaining in the fifos.
    struct FIFO* leftInputFifo = getAudioInput(AudioInput::LEFT_CHANNEL);
    struct FIFO* rightInputFifo = getAudioInput(AudioInput::RIGHT_CHANNEL);
    int ctr = 2; // Should only need to run twice to flush everything
    while (ctr-- > 0 && (codec2_fifo_used(leftInputFifo) > 0 || codec2_fifo_used(rightInputFifo) > 0))
    {
        onTimerTick_();
    }
}

void AudioMixer::onTimerTick_()
{
    struct FIFO* leftInputFifo = getAudioInput(AudioInput::LEFT_CHANNEL);
    struct FIFO* rightInputFifo = getAudioInput(AudioInput::RIGHT_CHANNEL);
    struct FIFO* outputFifo = getAudioOutput(AudioInput::LEFT_CHANNEL);

    // Process on a sample by sample basis
    int ctr = AUDIO_MIXER_NUM_SAMPLES_PER_INTERVAL;
    short bufLeft;
    short bufRight;
    while (ctr-- > 0 && (codec2_fifo_used(leftInputFifo) > 0 || codec2_fifo_used(rightInputFifo) > 0))
    {
        bufLeft = 0;
        bufRight = 0;
        
        codec2_fifo_read(leftInputFifo, &bufLeft, 1);
        codec2_fifo_read(rightInputFifo, &bufRight, 1);
        
        // See https://dsp.stackexchange.com/questions/3581/algorithms-to-mix-audio-signals-without-clipping
        // for more info. This is basically (1/sqrt(2)) * (a + b) but done in a way that avoids the use
        // of float or SW division (i.e. multiplies the sum by 724/1024 or ~0.707).
        int32_t addedSample = ((bufLeft + bufRight) * 724) >> 10;
        if (addedSample >= SHRT_MAX)
        {
            addedSample = SHRT_MAX;
        }
        else if (addedSample <= SHRT_MIN)
        {
            addedSample = SHRT_MIN;
        }
        short resultShort = (short)addedSample;
        
        codec2_fifo_write(outputFifo, &resultShort, 1);
    }
}

}

}
