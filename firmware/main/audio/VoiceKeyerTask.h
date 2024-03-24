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

#ifndef VOICE_KEYER_TASK_H
#define VOICE_KEYER_TASK_H

#include "esp_vfs_fat.h"

#include "codec2_fifo.h"

#include "AudioInput.h"
#include "VoiceKeyerMessage.h"
#include "WAVFileReader.h"
#include "audio/FreeDVMessage.h"
#include "network/NetworkMessage.h"
#include "storage/SettingsMessage.h"
#include "task/DVTask.h"
#include "task/DVTimer.h"

namespace ezdv
{

namespace audio
{

using namespace ezdv::task;

class VoiceKeyerTask : public DVTask, public AudioInput
{
public:
    VoiceKeyerTask(AudioInput* micDeviceTask, AudioInput* fdvTask);
    virtual ~VoiceKeyerTask();

    // Special setter for Flex radios as microphone goes through SmartSDR
    // instead of TLV320.
    void setMicDeviceTask(AudioInput* task) { micDeviceTask_ = task; }
    
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;

    virtual void onTaskTick_() override;
    
private:
    enum { IDLE, TX, WAITING } currentState_;

    DVTimer voiceKeyerTickTimer_;
    uint64_t lastTimeInTick_;
    FILE* voiceKeyerFile_;
    WAVFileReader* wavReader_;
    uint64_t timeAtBeginningOfState_;
    int numSecondsToWait_;
    int timesToTransmit_;
    int timesTransmitted_;
    int bytesToUpload_;

    FIFO* fileReadFifo_;
    short* fileReadScratchBuf_;
    DVTimer fileReadTimer_;

    // These are so we can shut off mic audio from the codec
    // chip during TX and restore audio routing once voice keying
    // ends.
    AudioInput* micDeviceTask_;
    AudioInput* fdvTask_;

    wl_handle_t wlHandle_;

    void startKeyer_();
    void stopKeyer_();
    void tickKeyer_();

    void onStartVoiceKeyerMessage_(DVTask* origin, StartVoiceKeyerMessage* message);
    void onStopVoiceKeyerMessage_(DVTask* origin, StopVoiceKeyerMessage* message);
    void onVoiceKeyerSettingsMessage_(DVTask* origin, storage::VoiceKeyerSettingsMessage* message);

    // Voice keyer file upload handlers
    void onStartFileUploadMessage_(DVTask* origin, network::StartFileUploadMessage* message);
    void onFileUploadDataMessage_(DVTask* origin, network::FileUploadDataMessage* message);

    // Listen for RequestRxMessage so we can stop voice keyer if running.
    void onRequestRxMessage_(DVTask* origin, audio::RequestRxMessage* message);

    // Timer handler to read VK file into FIFO
    void readSamplesIntoFifo_();
};

}

}

#endif // VOICE_KEYER_TASK_H