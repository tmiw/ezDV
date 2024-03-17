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
    : DVTask("SoftwareUpdateTask", 10, 4096, tskNO_AFFINITY, 256)
    , isRunning_(false)
    , nextAppPartition_(nullptr)
    , nextHttpPartition_(nullptr)
    , appPartitionHandle_(0)
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

void SoftwareUpdateTask::onTaskSleep_()
{
    if (updateThread_.joinable())
    {
        isRunning_ = false;
        dataBlockCV_.notify_one();
        updateThread_.join();
    }
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

bool SoftwareUpdateTask::setPartitionPointers_()
{
    // Get partition objects required for the update.
    int appSlot = 0;
    nextAppPartition_ = const_cast<esp_partition_t*>(esp_ota_get_next_update_partition(nullptr));
    if (nextAppPartition_->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
    {
        appSlot = 0;
    }
    else if (nextAppPartition_->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)
    {
        appSlot = 1;
    }
    else
    {
        // Should not reach here.
        ESP_LOGE(CURRENT_LOG_TAG, "Firmware flash doesn't support more than two OTA slots");
        nextAppPartition_ = nullptr;
        return false;
    }
    
    ESP_LOGI(CURRENT_LOG_TAG, "Will be flashing to slot %d", appSlot);
    
    std::string nextHttpSlotName = "http_0";
    if (appSlot == 0)
    {
        nextHttpSlotName = "http_0";
    }
    else if (appSlot == 1)
    {
        nextHttpSlotName = "http_1";
    }
    
    nextHttpPartition_ = const_cast<esp_partition_t*>(esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nextHttpSlotName.c_str()));
    if (nextHttpPartition_ == nullptr)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Could not find http partition '%s'", nextHttpSlotName.c_str());
        nextAppPartition_ = nullptr;
        return false;
    }
    
    // Open and prep partitions for flashing
    if (esp_ota_begin(nextAppPartition_, OTA_SIZE_UNKNOWN, &appPartitionHandle_) != ESP_OK)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Could not begin flashing app partition");
        nextHttpPartition_ = nullptr;
        nextAppPartition_ = nullptr;
        return false;
    }
    
    if (esp_partition_erase_range(nextHttpPartition_, 0, nextHttpPartition_->size) != ESP_OK)
    {
        // Caller will abort the OTA operation if we can't erase this partition.
        ESP_LOGE(CURRENT_LOG_TAG, "Could not erase http partition '%s'", nextHttpSlotName.c_str());
        return false;
    }
    
    httpPartitionOffset_ = 0;
    return true;
}

void SoftwareUpdateTask::updateThreadEntryFn_()
{
    bool success = false;
    
    ESP_LOGI(CURRENT_LOG_TAG, "Begin FW flash helper thread");
    
    // Grab needed partition objects.
    setPartitionPointers_();
    
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
            // Tar read was successful, check if flash was successful.
            if (nextHttpPartition_ == nullptr)
            {
                // HTTP partition was successfully flashed if nextHttpPartition_ gets reset to null.
                if (esp_ota_end(appPartitionHandle_) == ESP_OK && esp_ota_set_boot_partition(nextAppPartition_) == ESP_OK)
                {
                    ESP_LOGI(CURRENT_LOG_TAG, "Flash successful, will boot using the next slot on reboot.");
                    success = true;
                }
            }
            
            FirmwareUpdateCompleteMessage message(success);
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
    
    if (!success)
    {
        // Abort firmware flash if we got a failure.
        if (nextAppPartition_ != nullptr)
        {
            esp_ota_abort(appPartitionHandle_);
            appPartitionHandle_ = 0;
        }
    }
    nextAppPartition_ = nullptr;
    nextHttpPartition_ = nullptr;
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
    SoftwareUpdateTask* thisPtr = (SoftwareUpdateTask*)context_data;
    
    if (!strcmp(header->filename, "ezdv.bin"))
    {
        // app partition handling
        if (esp_ota_write(thisPtr->appPartitionHandle_, block, length) != ESP_OK)
        {
            return -1;
        }
    }
    else if (!strcmp(header->filename, "http.bin") || !strcmp(header->filename, "http_0.bin"))
    {
        // HTTP partition handling
        if (esp_partition_write(thisPtr->nextHttpPartition_, thisPtr->httpPartitionOffset_, block, length) != ESP_OK)
        {
            return -1;
        }
        thisPtr->httpPartitionOffset_ += length;
    }
    return 0; // non-zero terminates untarring
}

int SoftwareUpdateTask::UntarEndFileCallback_(header_translated_t *header, 
									int entry_index, 
									void *context_data)
{
    SoftwareUpdateTask* thisPtr = (SoftwareUpdateTask*)context_data;
    ESP_LOGI(CURRENT_LOG_TAG, "Finished reading %s from tarball", header->filename);
    
    if (!strcmp(header->filename, "http.bin"))
    {
        // Set partition pointer to NULL to mark that we've finished flashing the HTTP partition.
        thisPtr->nextHttpPartition_ = nullptr;
    }
    
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
    
    return *thisPtr->currentDataBlock_;
}

}

}