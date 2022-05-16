#include "IcomRadioTask.h"
#include "../../util/NamedQueue.h"

namespace sm1000neo::radio::icom
{
    void IcomRadioTask::init()
    {
        controlChannelSM_.start("10.1.2.3", 50001, "YOUR_USERNAME", "YOUR_PASSWORD");
        sm1000neo::util::NamedQueue::Add(RADIO_CONTROL_PIPE_NAME, pttEventQueue_);
    }
    
    void IcomRadioTask::event(const sm1000neo::radio::RadioPTTMessage& event)
    {
        ESP_LOGI("IcomRadioTask", "Sending PTT CIV message (PTT = %d)", event.value);
        
        uint8_t civPacket[] = {
            0xFE,
            0xFE,
            0xA4,
            0xE0,
            0x1C, // PTT on/off command/subcommand
            0x00,
            event.value ? (uint8_t)0x01 : (uint8_t)0x00,
            0xFD
        };
        
        controlChannelSM_.sendCIVPacket(civPacket, sizeof(civPacket));
    }
}
