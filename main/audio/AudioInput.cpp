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

#include <cassert>

#include "AudioInput.h"

namespace ezdv
{

namespace audio
{

AudioInput::AudioInput(int8_t numInputChannels, int8_t numOutputChannels, uint32_t numSamplesInFifo)
    : numChannels_(numInputChannels)
{
    assert(numInputChannels > 0);
    assert(numSamplesInFifo > 0);

    inputAudioFifos_ = new FIFO*[numInputChannels];
    assert(inputAudioFifos_ != nullptr);

    outputAudioFifos_ = new FIFO*[numOutputChannels];
    assert(outputAudioFifos_ != nullptr);

    for (int index = 0; index < numInputChannels; index++)
    {
        inputAudioFifos_[index] = codec2_fifo_create(numSamplesInFifo);
        assert(inputAudioFifos_[index] != nullptr);
    }

    for (int index = 0; index < numOutputChannels; index++)
    {
        outputAudioFifos_[index] = nullptr;
    }
}

AudioInput::~AudioInput()
{
    for (int index = 0; index < numChannels_; index++)
    {
        codec2_fifo_free(inputAudioFifos_[index]);
    }

    delete[] inputAudioFifos_;
}

struct FIFO* AudioInput::getAudioInput(ChannelLabel channel)
{
    return inputAudioFifos_[(int)channel];
}

void AudioInput::setAudioOutput(ChannelLabel channel, struct FIFO* fifo)
{
    outputAudioFifos_[(int)channel] = fifo;
}

struct FIFO* AudioInput::getAudioOutput(ChannelLabel channel)
{
    return outputAudioFifos_[(int)channel];
}

}

}