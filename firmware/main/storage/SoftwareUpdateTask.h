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
#include <mutex>

#include "esp_partition.h"
#include "esp_ota_ops.h"

#include "untar.h"
#include "uzlib.h"

#include "util/PSRamAllocator.h"
#include "network/NetworkMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

#define UZLIB_DICT_SIZE (32767)

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
    
private:
    char* uzlibDict_;
    std::thread updateThread_;
    std::condition_variable dataBlockCV_;
    std::mutex dataBlockMutex_;
    uzlib_uncomp* uzlibData_;
    bool isRunning_;
    char* currentDataBlock_;
    esp_partition_t* nextAppPartition_;
    esp_partition_t* nextHttpPartition_;
    esp_ota_handle_t appPartitionHandle_;
    int httpPartitionOffset_;
    
    typedef std::pair<char*, int> VectorEntryType;
    std::vector<VectorEntryType, util::PSRamAllocator<VectorEntryType> > receivedDataBlocks_;
    
    // Partition pointer initialization.
    bool setPartitionPointers_();
    
    // Update thread entry function.
    void updateThreadEntryFn_();
    
    // Firmware file upload handlers
    void onStartFirmwareUploadMessage_(DVTask* origin, network::StartFirmwareUploadMessage* message);
    void onFirmwareUploadDataMessage_(DVTask* origin, network::FirmwareUploadDataMessage* message);
    
    // uzlib callbacks
    static int UzlibReadCallback_(struct uzlib_uncomp *uncomp);
    
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