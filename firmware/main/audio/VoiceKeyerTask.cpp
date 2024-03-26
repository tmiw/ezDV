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

#include <unistd.h>
#include "VoiceKeyerTask.h"

#define CURRENT_LOG_TAG "VoiceKeyerTask"
#define VOICE_KEYER_FILE ("/vk/keyer.wav")

// Number of audio samples to read from the WAV file.
// This number was set to match the flash page size (4KB).
#define SAMPLES_TO_READ_PER_CYCLE (4096)

// Maximum number of samples in the FIFO.
#define MAX_SAMPLES_IN_FIFO (SAMPLES_TO_READ_PER_CYCLE << 1)

// Number of samples to forward onto FDV task.
#define SAMPLES_TO_SEND_PER_CYCLE (160)

// Interval which to send samples to FDV task.
#define TIMER_TICK_INTERVAL (MS_TO_US(20))

// Interval which to attempt to read more samples from flash.
#define FILE_READ_INTERVAL (MS_TO_US(250))

// If we fall behind for some reason, the VK task will send 
// x * SAMPLES_TO_SEND_PER_CYCLE (where x is the number calculated
// to be needed to be back in sync). This places an upper bound
// on x so we don't end up permanently being out of sync.
#define MAXIMUM_NUMBER_OF_LOOPS_PER_TICK (2)

namespace ezdv
{

namespace audio
{

VoiceKeyerTask::VoiceKeyerTask(AudioInput* micDeviceTask, AudioInput* fdvTask)
    : DVTask("VoiceKeyerTask", 15, 4096, tskNO_AFFINITY, 256, portMAX_DELAY)
    , AudioInput(1, 1)
    , currentState_(VoiceKeyerTask::IDLE)
    , voiceKeyerTickTimer_(this, this, &VoiceKeyerTask::tickKeyer_, TIMER_TICK_INTERVAL, "VKSendTimer")
    , lastTimeInTick_(0)
    , voiceKeyerFile_(nullptr)
    , wavReader_(nullptr)
    , timeAtBeginningOfState_(0)
    , numSecondsToWait_(0)
    , timesToTransmit_(0)
    , timesTransmitted_(0)
    , bytesToUpload_(0)
    , fileReadTimer_(this, this, &VoiceKeyerTask::readSamplesIntoFifo_, FILE_READ_INTERVAL, "VKFileReadTimer")
    , micDeviceTask_(micDeviceTask)
    , fdvTask_(fdvTask)
    , wlHandle_(-1)
{
    registerMessageHandler(this, &VoiceKeyerTask::onStartVoiceKeyerMessage_);
    registerMessageHandler(this, &VoiceKeyerTask::onStopVoiceKeyerMessage_);
    registerMessageHandler(this, &VoiceKeyerTask::onVoiceKeyerSettingsMessage_);

    registerMessageHandler(this, &VoiceKeyerTask::onFileUploadDataMessage_);
    registerMessageHandler(this, &VoiceKeyerTask::onStartFileUploadMessage_);

    registerMessageHandler(this, &VoiceKeyerTask::onRequestRxMessage_);

    fileReadFifo_ = codec2_fifo_create(MAX_SAMPLES_IN_FIFO);
    assert(fileReadFifo_ != nullptr);

    fileReadScratchBuf_ = (short*)heap_caps_calloc(SAMPLES_TO_READ_PER_CYCLE, sizeof(short), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    assert(fileReadScratchBuf_ != nullptr);
}

VoiceKeyerTask::~VoiceKeyerTask()
{
    heap_caps_free(fileReadScratchBuf_);
    codec2_fifo_destroy(fileReadFifo_);
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

void VoiceKeyerTask::readSamplesIntoFifo_(DVTimer*)
{
    if (wavReader_ == nullptr || codec2_fifo_free(fileReadFifo_) < SAMPLES_TO_READ_PER_CYCLE)
    {
        return;
    }

    auto numRead = wavReader_->read(fileReadScratchBuf_, SAMPLES_TO_READ_PER_CYCLE);
    if (numRead < SAMPLES_TO_READ_PER_CYCLE)
    {
        delete wavReader_;
        wavReader_ = nullptr;

        fclose(voiceKeyerFile_);
        voiceKeyerFile_ = nullptr;

        fileReadTimer_.stop();
    }

    codec2_fifo_write(fileReadFifo_, fileReadScratchBuf_, numRead);
}

void VoiceKeyerTask::onTaskTick_()
{
    // empty
}

void VoiceKeyerTask::tickKeyer_(DVTimer*)
{
    auto currentTime = esp_timer_get_time();

    switch (currentState_)
    {
        case VoiceKeyerTask::IDLE:
            break;
        case VoiceKeyerTask::TX:
        {
            short samples[SAMPLES_TO_SEND_PER_CYCLE];
            auto fifo = getAudioOutput(ezdv::audio::AudioInput::LEFT_CHANNEL);
            assert(fifo != nullptr);

            // If it takes longer than expected to get into this handler,
            // we should send enough extra samples to compensate. 
            int numTimesToRead = std::max(1, (int)(currentTime - lastTimeInTick_) / TIMER_TICK_INTERVAL);
            numTimesToRead = std::min(numTimesToRead, MAXIMUM_NUMBER_OF_LOOPS_PER_TICK);
            if (numTimesToRead > 1)
            {
                ESP_LOGW(CURRENT_LOG_TAG, "Took longer than expected to enter tick handler, now sending %d samples", numTimesToRead * SAMPLES_TO_SEND_PER_CYCLE);
            }

            for (int count = 0; count < numTimesToRead; count++)
            {
                auto numToRead = std::min(codec2_fifo_used(fileReadFifo_), SAMPLES_TO_SEND_PER_CYCLE);

                if (codec2_fifo_free(fifo) < numToRead)
                {
                    break;
                }

                codec2_fifo_read(fileReadFifo_, samples, numToRead);
                codec2_fifo_write(fifo, samples, numToRead);

                if (numToRead < SAMPLES_TO_SEND_PER_CYCLE)
                {
                    currentState_ = VoiceKeyerTask::WAITING;
                    timeAtBeginningOfState_ = currentTime;

                    // Request return to RX
                    RequestRxMessage message;
                    publish(&message);

                    break;
                }
            }

            break;
        }
        case VoiceKeyerTask::WAITING:
        {
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

    lastTimeInTick_ = currentTime;
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

        // Fully populate the FIFO before starting TX.
        // Note: each call can read up to half of the FIFO worth of 
        // samples at a time.
        while (voiceKeyerFile_ != nullptr && codec2_fifo_free(fileReadFifo_) >= SAMPLES_TO_READ_PER_CYCLE)
        {
            readSamplesIntoFifo_(nullptr);
        }

        // Start file read timer to ensure that the input FIFO
        // remains full.
        fileReadTimer_.start();

        // Reroute input audio so it's coming from us.
        // Only do this if we just started the keyer for the first time.
        if (timesTransmitted_ == 0)
        {
            micDeviceTask_->setAudioOutput(ezdv::audio::AudioInput::LEFT_CHANNEL, nullptr);
            setAudioOutput(
                ezdv::audio::AudioInput::LEFT_CHANNEL,
                fdvTask_->getAudioInput(ezdv::audio::AudioInput::LEFT_CHANNEL));
        }

        // Request TX
        RequestTxMessage message;
        publish(&message);

        currentState_ = VoiceKeyerTask::TX;
        voiceKeyerTickTimer_.start();
        lastTimeInTick_ = esp_timer_get_time();
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
    voiceKeyerTickTimer_.stop();

    // If there's anything still in the FIFO,
    // make sure it's gone.
    short tmp;
    while (codec2_fifo_read(fileReadFifo_, &tmp, 1) == 0)
    {
        // empty
    }
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

    // Don't allow files larger than 512KB to avoid crashes.
    if (bytesToUpload_ >= 512000)
    {
        FileUploadCompleteMessage response(false, FileUploadCompleteMessage::FILE_TOO_LARGE);
        publish(&response);

        bytesToUpload_ = 0;
        return;
    }

    unlink(VOICE_KEYER_FILE);
    voiceKeyerFile_ = fopen(VOICE_KEYER_FILE, "w+b");
    if (voiceKeyerFile_ == nullptr)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Cannot open voice keyer file (errno %d)", errno);
        vTaskDelay(pdMS_TO_TICKS(100));

        FileUploadCompleteMessage response(false, FileUploadCompleteMessage::SYSTEM_ERROR, errno);
        publish(&response);

        bytesToUpload_ = 0;
    }
}

void VoiceKeyerTask::onFileUploadDataMessage_(DVTask* origin, network::FileUploadDataMessage* message)
{
    if (voiceKeyerFile_ != nullptr && currentState_ == VoiceKeyerTask::IDLE)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Saving %d bytes to %s", message->length, VOICE_KEYER_FILE);

        auto numWritten = fwrite(message->buf, 1, message->length, voiceKeyerFile_);
        heap_caps_free(message->buf);
        bytesToUpload_ -= message->length;

        if (numWritten <= 0)
        {
            FileUploadCompleteMessage response(false, FileUploadCompleteMessage::SYSTEM_ERROR, errno);
            publish(&response);

            bytesToUpload_ = 0;
        }
        else if (bytesToUpload_ <= 0)
        {
            // Make sure the user uploaded something using the 
            // correct number of channels and sample rate.
            fseek(voiceKeyerFile_, 0, SEEK_SET);

            wavReader_ = new WAVFileReader(voiceKeyerFile_);
            assert(wavReader_ != nullptr);

            bool success = true;
            if (wavReader_->num_channels() != 1)
            {
                FileUploadCompleteMessage response(false, FileUploadCompleteMessage::INCORRECT_NUM_CHANNELS);
                publish(&response);
                success = false;
            }
            else if (wavReader_->sample_rate() != 8000)
            {
                FileUploadCompleteMessage response(false, FileUploadCompleteMessage::INCORRECT_SAMPLE_RATE);
                publish(&response);
                success = false;
            }
            else
            {
                FileUploadCompleteMessage response(true);
                publish(&response);
            }

            delete wavReader_;
            wavReader_ = nullptr;

            fclose(voiceKeyerFile_);
            voiceKeyerFile_ = nullptr;

            if (!success)
            {
                unlink(VOICE_KEYER_FILE);
            }

            bytesToUpload_ = 0;
        }
    }
    else
    {
        heap_caps_free(message->buf);
        bytesToUpload_ = 0;
    }
}

void VoiceKeyerTask::onRequestRxMessage_(DVTask* origin, audio::RequestRxMessage* message)
{
    if (currentState_ == VoiceKeyerTask::TX)
    {
        // If something is requesting receive while the keyer is active, we should 
        // stop the keyer.
        audio::RequestStartStopKeyerMessage vkRequest(false);
        publish(&vkRequest);
    }
}

}

}
