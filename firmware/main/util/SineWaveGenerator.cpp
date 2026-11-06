/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
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

#include "SineWaveGenerator.h"

#include <cmath>
#include "esp_heap_caps.h"

namespace ezdv
{

namespace util
{

SineWaveGenerator::SineWaveGenerator(int frequency, int amplitude, int sampleRate)
{
    samples_ = (short*)heap_caps_malloc(sampleRate*sizeof(short), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(samples_ != nullptr);
    
    // Pre-generate samples to avoid delays during execution.
    for (int index = 0; index < sampleRate; index++)
    {
        samples_[index] = amplitude * sin(2 * M_PI * frequency * index / sampleRate);
    }
}

SineWaveGenerator::~SineWaveGenerator()
{
    heap_caps_free(samples_);
}


}

}
