#include "IcomRadioTask.h"
#include "../../util/NamedQueue.h"

namespace ezdv::radio::icom
{
    void IcomRadioTask::init()
    {
        controlChannelSM_.start(host_, port_, username_, password_);
        ezdv::util::NamedQueue::Add(RADIO_CONTROL_PIPE_NAME, pttEventQueue_);
    }
    
    void IcomRadioTask::event(const ezdv::radio::RadioPTTMessage& event)
    {
        ESP_LOGI("IcomRadioTask", "Sending PTT CIV message (PTT = %d)", event.value);
        
        uint8_t civPacket[] = {
            0xFE,
            0xFE,
            controlChannelSM_.getCIVId(),
            0xE0,
            0x1C, // PTT on/off command/subcommand
            0x00,
            event.value ? (uint8_t)0x01 : (uint8_t)0x00,
            0xFD
        };
        
        controlChannelSM_.sendCIVPacket(civPacket, sizeof(civPacket));
    }
}
