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
            : smooth::core::Task("IcomRadioTask", 9216, 10, std::chrono::milliseconds(1))
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
        
        static IcomRadioTask& ThisTask()
        {
            static IcomRadioTask Task_;
            return Task_;
        } 
        
        void setAuthInfo(std::string ip, uint16_t controlPort, std::string username, std::string password)
        {
            host_ = ip;
            port_ = controlPort;
            username_ = username;
            password_ = password;
        }
    protected:
        virtual void init();
        
    private:
        ProtocolStateMachine controlChannelSM_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<sm1000neo::radio::RadioPTTMessage>> pttEventQueue_;
        std::string host_;
        uint16_t port_;
        std::string username_;
        std::string password_;
    };
}

#endif // RADIO__ICOM__ICOM_RADIO_TASK_H