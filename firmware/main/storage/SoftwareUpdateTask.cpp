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

#include "SoftwareUpdateTask.h"

#define CURRENT_LOG_TAG "SoftwareUpdateTask"

namespace ezdv
{

namespace storage
{
    
SoftwareUpdateTask::SoftwareUpdateTask()
    : DVTask("SoftwareUpdateTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 256, pdMS_TO_TICKS(20))
{
    registerMessageHandler(this, &SoftwareUpdateTask::onStartFirmwareUploadMessage_);
    registerMessageHandler(this, &SoftwareUpdateTask::onFirmwareUploadDataMessage_);
}

SoftwareUpdateTask::~SoftwareUpdateTask()
{
    // empty
}

void SoftwareUpdateTask::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting SoftwareUpdateTask");
}

void SoftwareUpdateTask::onTaskWake_()
{
    // Same as start.
    onTaskStart_();
}

void SoftwareUpdateTask::onTaskSleep_()
{
    
}

void SoftwareUpdateTask::onTaskTick_()
{
    
}

void SoftwareUpdateTask::onStartFirmwareUploadMessage_(DVTask* origin, network::StartFirmwareUploadMessage* message)
{
    
}

void SoftwareUpdateTask::onFirmwareUploadDataMessage_(DVTask* origin, network::FirmwareUploadDataMessage* message)
{
    
}

}

}