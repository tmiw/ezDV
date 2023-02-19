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

#include <cmath>

#include "BeeperTask.h"

// Beeper constants. This is based on 1 dit per 60/(50 * wpm) = 60/(50*15) = 0.08s
#define CW_TIME_UNIT_MS 80
#define DIT_SIZE 1
#define DAH_SIZE 3
#define SPACE_BETWEEN_DITS 1
#define SPACE_BETWEEN_CHARS 3
#define SPACE_BETWEEN_WORDS 7
#define CW_SIDETONE_FREQ_HZ ((float)600.0)
#define SAMPLE_RATE_RECIP 0.000125 /* 8000 Hz */

// Found via experimentation
#define BEEPER_TIMER_TICK_MS ((int)(CW_TIME_UNIT_MS))
#define BEEPER_TIMER_TICK_US (BEEPER_TIMER_TICK_MS * 1000)

#define CURRENT_LOG_TAG ("BeeperTask")

namespace ezdv
{

namespace audio
{

std::map<char, std::string> BeeperTask::CharacterToMorse_ = {
    { 'A', ".-" },
    { 'B', "-..." },
    { 'C', "-.-." },
    { 'D', "-.." },
    { 'E', "." },
    { 'F', "..-." },
    { 'G', "--." },
    { 'H', "...." },
    { 'I', ".." },
    { 'J', ".---" },
    { 'K', "-.-" },
    { 'L', ".-.." },
    { 'M', "--" },
    { 'N', "-." },
    { 'O', "---" },
    { 'P', ".--." },
    { 'Q', "--.-" },
    { 'R', ".-." },
    { 'S', "..." },
    { 'T', "-" },
    { 'U', "..-" },
    { 'V', "...-" },
    { 'W', ".--" },
    { 'X', "-..-" },
    { 'Y', "-.--" },
    { 'Z', "--.." },
    
    { '1', ".----" },
    { '2', "..---" },
    { '3', "...--" },
    { '4', "....-" },
    { '5', "....." },
    { '6', "-...." },
    { '7', "--..." },
    { '8', "---.." },
    { '9', "----." },
    { '0', "-----" },
};

BeeperTask::BeeperTask()
    : DVTask("BeeperTask", 10 /* TBD */, 4096, tskNO_AFFINITY, 10)
    , AudioInput(1, 1) // we don't need the input FIFO, just the output one
    , beeperTimer_(this, std::bind(&BeeperTask::onTimerTick_, this), BEEPER_TIMER_TICK_US)
    , sineCounter_(0)
    , deferShutdown_(false)
{
    registerMessageHandler(this, &BeeperTask::onSetBeeperText_);
    registerMessageHandler(this, &BeeperTask::onClearBeeperText_);
}

BeeperTask::~BeeperTask()
{
    beeperTimer_.stop();
}

void BeeperTask::onTaskStart_()
{
    deferShutdown_ = false;
}

void BeeperTask::onTaskWake_()
{
    deferShutdown_ = false;
}

void BeeperTask::onTaskSleep_()
{
    beeperTimer_.stop();
    beeperList_.clear();
    sineCounter_ = 0;
}

void BeeperTask::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    if (beeperList_.size() > 0)
    {
        // We're deferring shutdown until we've played through the beeper
        // list.
        deferShutdown_ = true;
    }
    else
    {
        DVTask::onTaskSleep_(origin, message);
    }
}

void BeeperTask::onSetBeeperText_(DVTask* origin, SetBeeperTextMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got beeper message %s", message->text);

    beeperTimer_.stop();

    beeperList_.clear();
    sineCounter_ = 0;
    stringToBeeperScript_(message->text);

    beeperTimer_.start();
}

void BeeperTask::onClearBeeperText_(DVTask* origin, ClearBeeperTextMessage* message)
{
    beeperTimer_.stop();
    beeperList_.clear();
    sineCounter_ = 0;
}

void BeeperTask::onTimerTick_()
{
    // TBD -- assuming 8KHz sample rate
    struct FIFO* outputFifo = getAudioOutput(AudioInput::LEFT_CHANNEL);

    if (beeperList_.size() > 0)
    {
        short bufToQueue[CW_TIME_UNIT_MS * 8000 / 1000];
        bool emitSine = beeperList_[0];
        beeperList_.erase(beeperList_.begin());

        //ESP_LOGI("UserInterface", "Beep: %d", emitSine);
        if (emitSine)
        {
            for (int index = 0; index < sizeof(bufToQueue) / sizeof(short); index++)
            {
                bufToQueue[index] = 10000 * sin(2 * M_PI * CW_SIDETONE_FREQ_HZ * sineCounter_++ * SAMPLE_RATE_RECIP);
            }
        }
        else
        {
            sineCounter_ = 0;
            memset(bufToQueue, 0, sizeof(bufToQueue));
        }

        codec2_fifo_write(outputFifo, bufToQueue, sizeof(bufToQueue) / sizeof(short));
    }
    else
    {
        if (deferShutdown_)
        {
            // NOW we can shut down.
            onTaskSleep_(nullptr, nullptr);
        }
        else
        {
            beeperTimer_.stop();
            sineCounter_ = 0;
        }
    }
}

void BeeperTask::stringToBeeperScript_(std::string str)
{
    for (int index = 0; index < str.size(); index++)
    {        
        // Decode actual letter to beeper script.
        auto ch = str[index];
        if (ch != ' ')
        {
            charToBeeperScript_(ch);
        }
        else
        {
            // Add inter-word spacing
            for (int count = 0; count < SPACE_BETWEEN_WORDS; count++)
            {
                beeperList_.push_back(false);
            }
        }
    }
}

void BeeperTask::charToBeeperScript_(char ch)
{
    std::string morseString = CharacterToMorse_[ch];
    
    for (int index = 0; index < morseString.size(); index++)
    {
        int counts = morseString[index] == '-' ? DAH_SIZE : DIT_SIZE;
        
        // Add audio for the character
        for (int count = 0; count < counts; count++)
        {
            beeperList_.push_back(true);
        }
        
        // Add intra-character space
        if (index < morseString.size() - 1)
        {
            for (int count = 0; count < SPACE_BETWEEN_DITS; count++)
            {
                beeperList_.push_back(false);
            }
        }
    }
    
    // Add inter-character space
    for (int count = 0; count < SPACE_BETWEEN_CHARS; count++)
    {
        beeperList_.push_back(false);
    }
}
}

}