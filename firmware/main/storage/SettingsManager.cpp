#include "SettingsManager.h"

#define CURRENT_LOG_TAG ("SettingsManager")

#define LEFT_CHAN_VOL_ID ("lfChanVol")
#define RIGHT_CHAN_VOL_ID ("rtChanVol")

namespace ezdv::storage
{
    SettingsManager SettingsManager::Task_;
    
    void SettingsManager::event(const smooth::core::timer::TimerExpiredEvent& event)
    {
        std::unique_lock<std::mutex> lock(storageMutex_);
        
        ESP_LOGI(CURRENT_LOG_TAG, "Committing pending settings to flash.");
        
        esp_err_t err = storageHandle_->commit();
        if (err != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "error committing settings: %s", esp_err_to_name(err));
        }
        
        commitTimer_->stop();
    }
        
    void SettingsManager::init()
    {
        commitTimer_ = smooth::core::timer::Timer::create(0, commitQueue_, true, std::chrono::milliseconds(1000));
        
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
        else
        {            
            result = storageHandle_->get_item(LEFT_CHAN_VOL_ID, leftChannelVolume_);
            if (result == ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGW(CURRENT_LOG_TAG, "leftChannelVolume not found, will set to defaults");
                setLeftChannelVolume(0);
            }
            else if (result != ESP_OK)
            {
                ESP_LOGE(CURRENT_LOG_TAG, "error retrieving leftChannelVolume: %s", esp_err_to_name(result));
            }
            else
            {
                ESP_LOGI(CURRENT_LOG_TAG, "leftChannelVolume: %d", leftChannelVolume_);
            }
            
            result = storageHandle_->get_item(RIGHT_CHAN_VOL_ID, rightChannelVolume_);
            if (result == ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGW(CURRENT_LOG_TAG, "rightChannelVolume not found, will set to defaults");
                setRightChannelVolume(0);
            }
            else if (result != ESP_OK)
            {
                ESP_LOGE(CURRENT_LOG_TAG, "error retrieving rightChannelVolume: %s", esp_err_to_name(result));
            }
            else
            {
                ESP_LOGI(CURRENT_LOG_TAG, "rightChannelVolume: %d", rightChannelVolume_);
            }
        }
    }
        
    int8_t SettingsManager::getLeftChannelVolume()
    {
        ESP_LOGI(CURRENT_LOG_TAG, "retrieving leftChannelVolume: %d", leftChannelVolume_);
        return leftChannelVolume_;
    }
    
    int8_t SettingsManager::getRightChannelVolume()
    {
        ESP_LOGI(CURRENT_LOG_TAG, "retrieving rightChannelVolume: %d", rightChannelVolume_);
        return rightChannelVolume_;
    }
    
    void SettingsManager::setLeftChannelVolume(int8_t vol)
    {
        std::unique_lock<std::mutex> lock(storageMutex_);
        
        leftChannelVolume_ = vol;
        
        if (storageHandle_)
        {
            esp_err_t result = storageHandle_->set_item(LEFT_CHAN_VOL_ID, vol);
            if (result != ESP_OK)
            {
                ESP_LOGE(CURRENT_LOG_TAG, "error setting leftChannelVolume: %s", esp_err_to_name(result));
            }
            commitTimer_->reset();
        }
    }
    
    void SettingsManager::setRightChannelVolume(int8_t vol)
    {
        std::unique_lock<std::mutex> lock(storageMutex_);
        
        rightChannelVolume_ = vol;
        
        if (storageHandle_)
        {
            esp_err_t result = storageHandle_->set_item(RIGHT_CHAN_VOL_ID, vol);
            if (result != ESP_OK)
            {
                ESP_LOGE(CURRENT_LOG_TAG, "error setting rightChannelVolume: %s", esp_err_to_name(result));
            }
            commitTimer_->reset();
        }
    }

}