#ifndef RADIO__ICOM__ICOM_RADIO_TASK_H
#define RADIO__ICOM__ICOM_RADIO_TASK_H

#include "smooth/core/Task.h"
#include "ProtocolStateMachine.h"
#include "../Messaging.h"

namespace sm1000neo::radio::icom
{
    class IcomRadioTask
        : public smooth::core::Task
        , public smooth::core::ipc::IEventListener<sm1000neo::radio::RadioPTTMessage>
    {
    public:
        IcomRadioTask()
            : smooth::core::Task("IcomRadioTask", 8192, 10, std::chrono::milliseconds(1))
            , controlChannelSM_(ProtocolStateMachine::StateMachineType::CONTROL_SM, *this)
            , pttEventQueue_(smooth::core::ipc::TaskEventQueue<sm1000neo::radio::RadioPTTMessage>::create(2, *this, *this))
        {
            // empty
        }
        
        virtual ~IcomRadioTask() = default;
        
        void setLocalIp(uint32_t ip)
        {
            controlChannelSM_.setLocalIp(ip);
        }
        
        virtual void event(const sm1000neo::radio::RadioPTTMessage& event);
        
    protected:
        virtual void init();
        
    private:
        ProtocolStateMachine controlChannelSM_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::radio::RadioPTTMessage>> pttEventQueue_;
    };
}

#endif // RADIO__ICOM__ICOM_RADIO_TASK_H