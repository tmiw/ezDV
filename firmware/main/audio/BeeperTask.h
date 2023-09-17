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

#ifndef BEEPER_TASK_H
#define BEEPER_TASK_H

#include <map>
#include <string>

#include "AudioInput.h"
#include "BeeperMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"
#include "util/SineWaveGenerator.h"

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

class BeeperTask : public DVTask, public AudioInput
{
public:
    BeeperTask();
    virtual ~BeeperTask();

protected:
    virtual void onTaskSleep_(DVTask* origin, TaskSleepMessage* message) override;

    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

private:
    DVTimer beeperTimer_;
    util::SineWaveGenerator sineGenerator_;
    int sineCounter_;
    bool deferShutdown_;
    std::vector<bool> beeperList_;

    void onSetBeeperText_(DVTask* origin, SetBeeperTextMessage* message);
    void onClearBeeperText_(DVTask* origin, ClearBeeperTextMessage* message);

    void onTimerTick_();

    void stringToBeeperScript_(std::string str);
    void charToBeeperScript_(char ch);

    static std::map<char, std::string> CharacterToMorse_;
};

} // namespace audio

} // namespace ezdv


#endif // BEEPER_TASK_H