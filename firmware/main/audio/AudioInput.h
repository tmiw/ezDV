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

#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include <inttypes.h>

#include "codec2_fifo.h"

// 0.5s @ 48000 Hz
#define DEFAULT_NUM_SAMPLES_FOR_FIFO 24000

namespace ezdv
{

namespace audio
{

/// @brief Mixer class that provides inter-task audio input.
class AudioInput
{
public:
    enum ChannelLabel 
    {
        LEFT_CHANNEL = 0,
        RIGHT_CHANNEL = 1,

        USER_CHANNEL = LEFT_CHANNEL,
        RADIO_CHANNEL = RIGHT_CHANNEL,
    };

    /// @brief Creates an instance of AudioInput.
    /// @param numInputChannels The number of input channels to support.
    /// @param numOutputChannels The number of output channels to support.
    /// @param numSamplesInFifo The number of input samples per FIFO.
    AudioInput(int8_t numInputChannels, int8_t numOutputChannels, uint32_t numSamplesInFifo = DEFAULT_NUM_SAMPLES_FOR_FIFO);
    virtual ~AudioInput();

    /// @brief Retrieves the input FIFO for the given channel.
    /// @param channel The channel to retrieve the FIFO for.
    struct FIFO* getAudioInput(ChannelLabel channel);

    /// @brief Stores a link to the output FIFO on the given channel.
    /// @param channel The channel to set the output FIFO for.
    /// @param fifo The FIFO to set the channel's output to.
    void setAudioOutput(ChannelLabel channel, struct FIFO* fifo);

    /// @brief Retrieves the output FIFO for the given channel.
    /// @param channel The channel to retrieve the FIFO for.
    struct FIFO* getAudioOutput(ChannelLabel channel);
private:
    struct FIFO** inputAudioFifos_;
    struct FIFO** outputAudioFifos_;
    int8_t numChannels_;
};

}

}

#endif // AUDIO_INPUT_H