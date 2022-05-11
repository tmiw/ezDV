#ifndef RADIO__ICOM__ICOM_RADIO_TASK_H
#define RADIO__ICOM__ICOM_RADIO_TASK_H

#include "smooth/core/Task.h"
#include "ProtocolStateMachine.h"

namespace sm1000neo::radio::icom
{
    class IcomRadioTask : 
        public smooth::core::Task
    {
    public:
        IcomRadioTask()
            : smooth::core::Task("IcomRadioTask", 4096, 10, std::chrono::milliseconds(1))
            , controlChannelSM_(ProtocolStateMachine::StateMachineType::CONTROL_SM, *this)
        {
            // empty
        }
        
        virtual ~IcomRadioTask() = default;
        
    protected:
        virtual void init();
        
    private:
        ProtocolStateMachine controlChannelSM_;
    };
}

#endif // RADIO__ICOM__ICOM_RADIO_TASK_H