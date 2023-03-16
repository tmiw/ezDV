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

#ifndef SOFTWARE_UPDATE_TASK_H
#define SOFTWARE_UPDATE_TASK_H

#include <thread>
#include <condition_variable>
#include <vector>

#include "untar.h"
#include "uzlib.h"

#include "network/NetworkMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

namespace ezdv
{

namespace storage
{

using namespace ezdv::task;

class SoftwareUpdateTask : public DVTask
{
public:
    SoftwareUpdateTask();
    virtual ~SoftwareUpdateTask();

protected:
    virtual void onTaskStart_() override;
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;
    
private:
    char tarBlock_[TAR_BLOCK_SIZE];
    std::thread updateThread_;
    std::condition_variable dataBlockCV_;
    
    // Firmware file upload handlers
    void onStartFirmwareUploadMessage_(DVTask* origin, network::StartFirmwareUploadMessage* message);
    void onFirmwareUploadDataMessage_(DVTask* origin, network::FirmwareUploadDataMessage* message);
    
    // tinyuntar callbacks
    static int UntarHeaderCallback_(header_translated_t *header, 
    										int entry_index, 
    										void *context_data);
                                            
    static int UntarDataCallback_(header_translated_t *header, 
    										int entry_index, 
    										void *context_data, 
    										unsigned char *block, 
    										int length);

    static int UntarEndFileCallback_(header_translated_t *header, 
    									int entry_index, 
    									void *context_data);

    static int UntarReadBlockCallback_(void *context_data, unsigned char* block, int length);
};

}

}

#endif // VOICE_KEYER_TASK_H