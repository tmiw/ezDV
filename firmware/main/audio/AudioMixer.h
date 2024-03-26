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

#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include "AudioInput.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

class AudioMixer : public DVTask, public AudioInput
{
public:
    AudioMixer();
    virtual ~AudioMixer();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

private:
    DVTimer mixerTick_;
    
    void onTimerTick_(DVTimer*);
};

}

}

#endif // AUDIO_MIXER_H