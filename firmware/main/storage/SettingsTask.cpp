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

#include "esp_log.h"
#include "SettingsTask.h"

#define CURRENT_LOG_TAG ("SettingsTask")

#define LEFT_CHAN_VOL_ID ("lfChanVol")
#define RIGHT_CHAN_VOL_ID ("rtChanVol")

namespace ezdv
{

namespace storage
{

SettingsTask::SettingsTask()
    : DVTask("SettingsTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 10)
    , leftChannelVolume_(0)
    , rightChannelVolume_(0)
    , commitTimer_(this, [this](DVTimer*) { commit_(); }, 1000000)
{
    // Subscribe to messages
    registerMessageHandler(this, &SettingsTask::onSetLeftChannelVolume_);
    registerMessageHandler(this, &SettingsTask::onSetRightChannelVolume_);

    // Initialize NVS
    ESP_LOGI(CURRENT_LOG_TAG, "Initializing NVS.");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(CURRENT_LOG_TAG, "erasing NVS");
        
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    
    // Open NVS handle.
    ESP_LOGI(CURRENT_LOG_TAG, "Opening NVS handle.");
    esp_err_t result;
    storageHandle_ = nvs::open_nvs_handle("storage", NVS_READWRITE, &result);
    if (result != ESP_OK)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "error opening NVS handle: %s", esp_err_to_name(result));
        ESP_LOGW(CURRENT_LOG_TAG, "settings will not be saved.");
        storageHandle_ = nullptr;
    }
}

void SettingsTask::onTaskStart_()
{
    loadAllSettings_();
}

void SettingsTask::onTaskWake_()
{
    loadAllSettings_();
}

void SettingsTask::onTaskSleep_()
{
    // none
}

void SettingsTask::onSetLeftChannelVolume_(DVTask* origin, SetLeftChannelVolumeMessage* message)
{
    setLeftChannelVolume_(message->volume);
}

void SettingsTask::onSetRightChannelVolume_(DVTask* origin, SetRightChannelVolumeMessage* message)
{
    setRightChannelVolume_(message->volume);
}

void SettingsTask::loadAllSettings_()
{
    if (storageHandle_)
    {            
        esp_err_t result = storageHandle_->get_item(LEFT_CHAN_VOL_ID, leftChannelVolume_);
        if (result == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "leftChannelVolume not found, will set to defaults");
            setLeftChannelVolume_(0);
        }
        else if (result != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "error retrieving leftChannelVolume: %s", esp_err_to_name(result));
        }
        else
        {
            ESP_LOGI(CURRENT_LOG_TAG, "leftChannelVolume: %d", leftChannelVolume_);

            // Broadcast volume so that other components can initialize themselves with it.
            LeftChannelVolumeMessage* message = new LeftChannelVolumeMessage();
            assert(message != nullptr);

            message->volume = leftChannelVolume_;
            publish(message);
            delete message;
        }
        
        result = storageHandle_->get_item(RIGHT_CHAN_VOL_ID, rightChannelVolume_);
        if (result == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "rightChannelVolume not found, will set to defaults");
            setRightChannelVolume_(0);
        }
        else if (result != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "error retrieving rightChannelVolume: %s", esp_err_to_name(result));
        }
        else
        {
            ESP_LOGI(CURRENT_LOG_TAG, "rightChannelVolume: %d", rightChannelVolume_);

            // Broadcast volume so that other components can initialize themselves with it.
            RightChannelVolumeMessage* message = new RightChannelVolumeMessage();
            assert(message != nullptr);

            message->volume = rightChannelVolume_;
            publish(message);
            delete message;
        }
    }
}

void SettingsTask::commit_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Committing pending settings to flash.");
        
    esp_err_t err = storageHandle_->commit();
    if (err != ESP_OK)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "error committing settings: %s", esp_err_to_name(err));
    }
}

void SettingsTask::setLeftChannelVolume_(int8_t vol)
{
    leftChannelVolume_ = vol;
    
    if (storageHandle_)
    {
        esp_err_t result = storageHandle_->set_item(LEFT_CHAN_VOL_ID, vol);
        if (result != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "error setting leftChannelVolume: %s", esp_err_to_name(result));
        }

        commitTimer_.stop();
        commitTimer_.start(true);

        // Publish new volume setting to everyone who may care.
        LeftChannelVolumeMessage* message = new LeftChannelVolumeMessage();
        assert(message != nullptr);

        message->volume = vol;
        publish(message);
        delete message;
    }
}

void SettingsTask::setRightChannelVolume_(int8_t vol)
{    
    rightChannelVolume_ = vol;
    
    if (storageHandle_)
    {
        esp_err_t result = storageHandle_->set_item(RIGHT_CHAN_VOL_ID, vol);
        if (result != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "error setting rightChannelVolume: %s", esp_err_to_name(result));
        }
        commitTimer_.stop();
        commitTimer_.start(true);

        // Publish new volume setting to everyone who may care.
        RightChannelVolumeMessage* message = new RightChannelVolumeMessage();
        assert(message != nullptr);

        message->volume = vol;
        publish(message);
        delete message;
    }
}

}

}