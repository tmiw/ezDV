#include "UserInterfaceTask.h"
#include "../codec/Messaging.h"

#define CURRENT_LOG_TAG "UserInterfaceTask"

namespace sm1000neo::ui
{
    
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
                break;
            case GPIO_MODE_BUTTON:
            case GPIO_VOL_UP_BUTTON:
            case GPIO_VOL_DOWN_BUTTON:
            default:
                // TBD
                break;
        }
    }
    
    void UserInterfaceTask::init()
    {
        // empty
    }
}