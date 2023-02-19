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
#include "FreeDVMessage.h"
#include "storage/SettingsMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

#include "freedv_api.h"
#include "reliable_text.h"

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

class FreeDVTask : public DVTask, public AudioInput
{
public:
    FreeDVTask();
    virtual ~FreeDVTask();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;
    
private:
    struct freedv* dv_;
    reliable_text_t rText_;

    bool isTransmitting_;
    bool isActive_;

    void onSetFreeDVMode_(DVTask* origin, SetFreeDVModeMessage* message);
    void onSetPTTState_(DVTask* origin, FreeDVSetPTTStateMessage* message);
    void onReportingSettingsUpdate_(DVTask* origin, storage::ReportingSettingsMessage* message);

    static void OnReliableTextRx_(reliable_text_t rt, const char* txt_ptr, int length, void* state);
};

}

}

#endif // FREEDV_TASK_H