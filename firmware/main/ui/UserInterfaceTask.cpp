#include "UserInterfaceTask.h"
#include "../codec/Messaging.h"
#include "../radio/Messaging.h"
#include "../audio/AudioMixer.h"
#include "freedv_api.h"

#define CURRENT_LOG_TAG "UserInterfaceTask"

static std::map<char, std::string> CharacterToMorse_ = {
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

static std::pair<int, std::string> ModeList_[] = {
    { -1, " ANA" },
    { FREEDV_MODE_700D, " 700D" },
    { FREEDV_MODE_700E, " 700E" },
    { FREEDV_MODE_1600, " 1600" },
};

#define NUM_AVAILABLE_MODES 4

namespace sm1000neo::ui
{
    void UserInterfaceTask::event(const smooth::core::timer::TimerExpiredEvent& event)
    {
        // TBD -- assuming 8KHz sample rate
        if (beeperList_.size() > 0)
        {
            short bufToQueue[CW_TIME_UNIT_MS * 8000 / 1000];
            auto emitSine = beeperList_[0];
            beeperList_.erase(beeperList_.begin());
        
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
        
            sm1000neo::audio::AudioMixer::ThisTask().enqueueAudio(sm1000neo::audio::ChannelLabel::RIGHT_CHANNEL, bufToQueue, sizeof(bufToQueue) / sizeof(short));
        }
    }
    
    void UserInterfaceTask::event(const sm1000neo::ui::UserInterfaceControlMessage& event)
    {
        if (event.action == UserInterfaceControlMessage::UPDATE_SYNC)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Sync is now %d", event.value);
            syncLed_.set(event.value);
        }
        // others TBD
    }
    
    void UserInterfaceTask::event(const smooth::core::io::InterruptInputEvent& event)
    {
        bool state = !event.get_state(); // active low
        
        switch(event.get_io())
        {
            case GPIO_PTT_BUTTON:
                ESP_LOGI(CURRENT_LOG_TAG, "User PTT button is %d", state);
                pttLed_.set(state);
                pttNPN_.set(state);
                
                sm1000neo::codec::FreeDVChangePTTMessage message;
                message.pttEnabled = state;
                sm1000neo::util::NamedQueue::Send(FREEDV_PTT_PIPE_NAME, message);
                
                sm1000neo::radio::RadioPTTMessage radioMessage;
                radioMessage.value = state;
                sm1000neo::util::NamedQueue::Send(RADIO_CONTROL_PIPE_NAME, radioMessage);
                break;
            case GPIO_MODE_BUTTON:
                currentFDVMode_ = (currentFDVMode_ + 1) % NUM_AVAILABLE_MODES;
                beeperList_.clear();
                changeFDVMode_(currentFDVMode_);
                break;
            case GPIO_VOL_UP_BUTTON:
            case GPIO_VOL_DOWN_BUTTON:
            default:
                // TBD
                break;
        }
    }
    
    void UserInterfaceTask::changeFDVMode_(int mode)
    {
        sm1000neo::codec::FreeDVChangeModeMessage modeMessage;
        modeMessage.newMode = ModeList_[mode].first;
        sm1000neo::util::NamedQueue::Send(FREEDV_CONTROL_PIPE_NAME, modeMessage);
        
        stringToBeeperScript_(ModeList_[mode].second);
    }
    
    void UserInterfaceTask::init()
    {
        // empty
    }
    
    void UserInterfaceTask::stringToBeeperScript_(std::string str)
    {
        for (int index = 0; index < str.size(); index++)
        {
            // Inter-word spacing
            for (int count = 0; count < SPACE_BETWEEN_WORDS; count++)
            {
                beeperList_.push_back(false);
            }
            
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
    
    void UserInterfaceTask::charToBeeperScript_(char ch)
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
            for (int count = 0; count < SPACE_BETWEEN_DITS; count++)
            {
                beeperList_.push_back(false);
            }
        }
        
        // Add inter-character space
        for (int count = 0; count < SPACE_BETWEEN_CHARS; count++)
        {
            beeperList_.push_back(false);
        }
    }
}