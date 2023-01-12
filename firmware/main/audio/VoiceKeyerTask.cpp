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

#include "VoiceKeyerTask.h"

#define CURRENT_LOG_TAG "VoiceKeyerTask"
#define VOICE_KEYER_FILE ("/vk/keyer.wav")

namespace ezdv
{

namespace audio
{

VoiceKeyerTask::VoiceKeyerTask(AudioInput* micDeviceTask, AudioInput* fdvTask)
    : DVTask("VoiceKeyerTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 256, pdMS_TO_TICKS(20))
    , AudioInput(1, 1)
    , currentState_(VoiceKeyerTask::IDLE)
    , voiceKeyerFile_(nullptr)
    , wavReader_(nullptr)
    , timeAtBeginningOfState_(0)
    , numSecondsToWait_(0)
    , timesToTransmit_(0)
    , timesTransmitted_(0)
    , bytesToUpload_(0)
    , micDeviceTask_(micDeviceTask)
    , fdvTask_(fdvTask)
    , wlHandle_(-1)
{
    registerMessageHandler(this, &VoiceKeyerTask::onStartVoiceKeyerMessage_);
    registerMessageHandler(this, &VoiceKeyerTask::onStopVoiceKeyerMessage_);
    registerMessageHandler(this, &VoiceKeyerTask::onVoiceKeyerSettingsMessage_);

    registerMessageHandler(this, &VoiceKeyerTask::onFileUploadDataMessage_);
    registerMessageHandler(this, &VoiceKeyerTask::onStartFileUploadMessage_);
}

VoiceKeyerTask::~VoiceKeyerTask()
{
    // empty
}

void VoiceKeyerTask::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting VoiceKeyerTask");
    esp_vfs_fat_mount_config_t mountConfig = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 0,
        .disk_status_check_enable = false,
    };

    ESP_ERROR_CHECK(
        esp_vfs_fat_spiflash_mount_rw_wl(
            "/vk",
            "vk",
            &mountConfig,
            &wlHandle_
        ));
}

void VoiceKeyerTask::onTaskWake_()
{
    // Same as start.
    onTaskStart_();
}

void VoiceKeyerTask::onTaskSleep_()
{
    if (currentState_ != IDLE)
    {
        // Clean up after ourselves.
        stopKeyer_();
    }

    // Unmount FATFS after we're done with it
    if (wlHandle_ != -1)
    {
        auto err = esp_vfs_fat_spiflash_unmount_rw_wl(
            "/vk",
            wlHandle_
        );
            
        if (err != ESP_OK)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Could not unmount voice keyer partition: %s", esp_err_to_name(err));
        }
    }
}

void VoiceKeyerTask::onTaskTick_()
{
    switch (currentState_)
    {
        case VoiceKeyerTask::IDLE:
            break;
        case VoiceKeyerTask::TX:
        {
            short numSamples[160];
            auto fifo = getAudioOutput(ezdv::audio::AudioInput::LEFT_CHANNEL);
            assert(fifo != nullptr);

            auto numRead = wavReader_->read(numSamples, 160);
            codec2_fifo_write(fifo, numSamples, numRead);

            if (numRead < 160)
            {
                delete wavReader_;
                wavReader_ = nullptr;

                fclose(voiceKeyerFile_);
                voiceKeyerFile_ = nullptr;

                currentState_ = VoiceKeyerTask::WAITING;
                timeAtBeginningOfState_ = esp_timer_get_time();

                // Request return to RX
                RequestRxMessage message;
                publish(&message);
            }

            break;
        }
        case VoiceKeyerTask::WAITING:
        {
            auto currentTime = esp_timer_get_time();

            // Time elapsed in seconds
            auto timeElapsed = (currentTime - timeAtBeginningOfState_) / 1000000;

            if (timeElapsed >= numSecondsToWait_)
            {
                if (timesTransmitted_ < timesToTransmit_)
                {
                    timesTransmitted_++;
                    startKeyer_();
                }
                else
                {
                    ESP_LOGI(CURRENT_LOG_TAG, "Keyed %d times, stopping voice keyer", timesTransmitted_);
                    stopKeyer_();

                    // Indicate that we're done.
                    VoiceKeyerCompleteMessage completeMessage;
                    publish(&completeMessage);
                }
            }
            break;
        }
    }
}

void VoiceKeyerTask::startKeyer_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting voice keyer");

    // Open keyer file
    voiceKeyerFile_ = fopen(VOICE_KEYER_FILE, "rb");
    if (voiceKeyerFile_ != nullptr)
    {
        wavReader_ = new WAVFileReader(voiceKeyerFile_);
        assert(wavReader_ != nullptr);

        // Reroute input audio so it's coming from us
        micDeviceTask_->setAudioOutput(ezdv::audio::AudioInput::LEFT_CHANNEL, nullptr);
        setAudioOutput(
            ezdv::audio::AudioInput::LEFT_CHANNEL,
            fdvTask_->getAudioInput(ezdv::audio::AudioInput::LEFT_CHANNEL));

        // Request TX
        RequestTxMessage message;
        publish(&message);

        currentState_ = VoiceKeyerTask::TX;
    }
    else
    {
        ESP_LOGW(CURRENT_LOG_TAG, "No voice keyer file found, possibly not set up yet");
    }
}

void VoiceKeyerTask::stopKeyer_()
{
    // Reroute input audio so it's coming from mic
    micDeviceTask_->setAudioOutput(
        ezdv::audio::AudioInput::LEFT_CHANNEL, 
        fdvTask_->getAudioInput(ezdv::audio::AudioInput::LEFT_CHANNEL));

    setAudioOutput(
        ezdv::audio::AudioInput::LEFT_CHANNEL,
        nullptr);

    if (wavReader_ != nullptr)
    {
        delete wavReader_;
        wavReader_ = nullptr;

        fclose(voiceKeyerFile_);
        voiceKeyerFile_ = nullptr;
    }

    // Request RX
    RequestRxMessage message;
    publish(&message);

    currentState_ = VoiceKeyerTask::IDLE;
}

void VoiceKeyerTask::onStartVoiceKeyerMessage_(DVTask* origin, StartVoiceKeyerMessage* message)
{
    timesTransmitted_ = 0;
    startKeyer_();
}

void VoiceKeyerTask::onStopVoiceKeyerMessage_(DVTask* origin, StopVoiceKeyerMessage* message)
{
    stopKeyer_();
}

void VoiceKeyerTask::onVoiceKeyerSettingsMessage_(DVTask* origin, storage::VoiceKeyerSettingsMessage* message)
{
    numSecondsToWait_ = message->secondsToWait;
    timesToTransmit_ = message->timesToTransmit;
}

void VoiceKeyerTask::onStartFileUploadMessage_(DVTask* origin, network::StartFileUploadMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Saving %d bytes to %s", message->length, VOICE_KEYER_FILE);
    bytesToUpload_ = message->length;

    if (voiceKeyerFile_ != nullptr)
    {
        stopKeyer_();
    }

    unlink(VOICE_KEYER_FILE);
    voiceKeyerFile_ = fopen(VOICE_KEYER_FILE, "wb");
    if (voiceKeyerFile_ == nullptr)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Cannot open voice keyer file (errno %d)", errno);
        vTaskDelay(pdMS_TO_TICKS(100));
        assert(voiceKeyerFile_ != nullptr);
    }
}

void VoiceKeyerTask::onFileUploadDataMessage_(DVTask* origin, network::FileUploadDataMessage* message)
{
    assert(voiceKeyerFile_ != nullptr);

    ESP_LOGI(CURRENT_LOG_TAG, "Saving %d bytes to %s", message->length, VOICE_KEYER_FILE);

    fwrite(message->buf, 1, message->length, voiceKeyerFile_);
    bytesToUpload_ -= message->length;
    heap_caps_free(message->buf);

    if (bytesToUpload_ <= 0)
    {
        fclose(voiceKeyerFile_);
        voiceKeyerFile_ = nullptr;

        FileUploadCompleteMessage response;
        publish(&response);
    }
}

}

}