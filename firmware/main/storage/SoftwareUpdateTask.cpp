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

#include <chrono>

using namespace std::chrono_literals;

#include "SoftwareUpdateTask.h"
#include "SoftwareUpdateMessage.h"

#define CURRENT_LOG_TAG "SoftwareUpdateTask"

namespace ezdv
{

namespace storage
{
    
SoftwareUpdateTask::SoftwareUpdateTask()
    : DVTask("SoftwareUpdateTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 256, pdMS_TO_TICKS(20))
    , isRunning_(false)
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
    
    // Do one-time initialization of uzlib.
    uzlib_init();
}

void SoftwareUpdateTask::onTaskWake_()
{
    // Same as start.
    onTaskStart_();
}

void SoftwareUpdateTask::onTaskSleep_()
{
    if (updateThread_.joinable())
    {
        isRunning_ = false;
        dataBlockCV_.notify_one();
        updateThread_.join();
    }
}

void SoftwareUpdateTask::onTaskTick_()
{
    // empty
}

void SoftwareUpdateTask::onStartFirmwareUploadMessage_(DVTask* origin, network::StartFirmwareUploadMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Beginning firmware flash");
    
    // If we're already running, we should stop the existing firmware update first.
    if (updateThread_.joinable())
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Need to stop current flash before we can start again");
        
        isRunning_ = false;
        dataBlockCV_.notify_one();
        updateThread_.join();
    }
    
    // Start update thread. This is needed because of the API uzlib/tinyutar use
    // that make its use as part of this component infrastructure non-trivial.
    // The thread will end itself once the SW update finishes (either successfully or
    // otherwise).
    isRunning_ = true;
    updateThread_ = std::thread(std::bind(&SoftwareUpdateTask::updateThreadEntryFn_, this));
}

void SoftwareUpdateTask::onFirmwareUploadDataMessage_(DVTask* origin, network::FirmwareUploadDataMessage* message)
{
    // If we're not currently flashing, ignore the message.
    if (!updateThread_.joinable())
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Received firmware data but not currently running!");
        
        delete[] message->buf;
        return;
    }
    
    // Queue received data and let the update thread know that it's available.
    {
        std::unique_lock<std::mutex> lock(dataBlockMutex_);
        receivedDataBlocks_.push_back(VectorEntryType(message->buf, message->length));
    }
    dataBlockCV_.notify_one();
}

void SoftwareUpdateTask::updateThreadEntryFn_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Begin FW flash helper thread");
    
    currentDataBlock_ = nullptr;
    
    // Initialize uzlib data needed for decompression.
    uzlibData_ = (uzlib_uncomp*)heap_caps_calloc(1, sizeof(uzlib_uncomp), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(uzlibData_ != nullptr);
    uzlibDict_ = (char*)heap_caps_calloc(UZLIB_DICT_SIZE, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(uzlibDict_ != nullptr);
    
    uzlib_uncompress_init(uzlibData_, uzlibDict_, UZLIB_DICT_SIZE);
    
    uzlibData_->contextData = this;
    uzlibData_->source_read_cb = &UzlibReadCallback_;
    
    auto res = uzlib_gzip_parse_header(uzlibData_);
    if (res != TINF_OK) 
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Not a valid .gz file (res = %d)", res);

        FirmwareUpdateCompleteMessage message(false);
        publish(&message);
        
        goto fw_cleanup;
    }
    
    {
        entry_callbacks_t untarCallbacks = {
            .read_data_cb = &UntarReadBlockCallback_,
            .header_cb = &UntarHeaderCallback_,
            .data_cb = &UntarDataCallback_,
            .end_cb = &UntarEndFileCallback_
        };
    
        auto ret = read_tar(&untarCallbacks, this);
        if (ret == 0)
        {
            // Tar read was successful
            FirmwareUpdateCompleteMessage message(true);
            publish(&message);
        }
        else
        {        
            // Tar read was unsuccessful. The returned code was why.
            ESP_LOGE(CURRENT_LOG_TAG, "Untar failed (errno = %d)", ret);
            FirmwareUpdateCompleteMessage message(false);
            publish(&message);
        }
    }

fw_cleanup:
    isRunning_ = false;

    // Perform cleanup as required
    {
        std::unique_lock<std::mutex> lock(dataBlockMutex_);
    
        for (auto& val : receivedDataBlocks_)
        {
            delete[] val.first;
        }
        receivedDataBlocks_.clear();
        
        if (currentDataBlock_)
        {
            delete[] currentDataBlock_;
        }
        
        heap_caps_free(uzlibData_);
        heap_caps_free(uzlibDict_);
    }
}

int SoftwareUpdateTask::UntarHeaderCallback_(header_translated_t *header, 
										int entry_index, 
										void *context_data)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting to read file %s from tarball", header->filename);
    
    return 0; // non-zero terminates untarring
}
                                        
int SoftwareUpdateTask::UntarDataCallback_(header_translated_t *header, 
										int entry_index, 
										void *context_data, 
										unsigned char *block, 
										int length)
{
    // Called when a data block has been seen in the file.
    
    return 0; // non-zero terminates untarring
}

int SoftwareUpdateTask::UntarEndFileCallback_(header_translated_t *header, 
									int entry_index, 
									void *context_data)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Finished reading %s from tarball", header->filename);
    
    return 0; // non-zero terminates untarring
}

int SoftwareUpdateTask::UntarReadBlockCallback_(void *context_data, unsigned char* block, int length)
{
    SoftwareUpdateTask* thisPtr = (SoftwareUpdateTask*)context_data;
    
    // Initialize destination for gunzip operation.
    thisPtr->uzlibData_->dest_start = block;
    thisPtr->uzlibData_->dest = block;
    thisPtr->uzlibData_->dest_limit = block + length;
    
    // Request up to length bytes of decompressed data from uzlib.
    uzlib_uncompress(thisPtr->uzlibData_);
    
    return (int)(thisPtr->uzlibData_->dest - thisPtr->uzlibData_->dest_start);
}

int SoftwareUpdateTask::UzlibReadCallback_(struct uzlib_uncomp *uncomp)
{
    SoftwareUpdateTask* thisPtr = (SoftwareUpdateTask*)uncomp->contextData;
    std::unique_lock<std::mutex> lock(thisPtr->dataBlockMutex_);
    
    ESP_LOGI(CURRENT_LOG_TAG, "uzlib has requested more data");
    
    // If there's no data available, wait until we get more.
    while (thisPtr->isRunning_ && thisPtr->receivedDataBlocks_.size() == 0)
    {
        ESP_LOGW(CURRENT_LOG_TAG, "no data yet for uzlib");
        thisPtr->dataBlockCV_.wait_for(lock, 10ms);
    }

    if (!thisPtr->isRunning_)
    {
        // Return EOF so we force stop of the gunzip process.
        return -1;
    }
    
    // Set source pointers and return.
    if (thisPtr->uzlibData_->source != nullptr)
    {
        delete[] thisPtr->currentDataBlock_;
        thisPtr->uzlibData_->source = nullptr;
        thisPtr->currentDataBlock_ = nullptr;
    }
    
    thisPtr->currentDataBlock_ = thisPtr->receivedDataBlocks_[0].first;
    thisPtr->uzlibData_->source = (const unsigned char*)thisPtr->currentDataBlock_ + 1;
    thisPtr->uzlibData_->source_limit = (const unsigned char*)(thisPtr->receivedDataBlocks_[0].first + thisPtr->receivedDataBlocks_[0].second);
    thisPtr->receivedDataBlocks_.erase(thisPtr->receivedDataBlocks_.begin());
    
    return *thisPtr->currentDataBlock_;;
}

}

}