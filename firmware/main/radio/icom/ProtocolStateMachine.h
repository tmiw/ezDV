#ifndef RADIO__ICOM__PROTOCOL_STATE_MACHINE_H
#define RADIO__ICOM__PROTOCOL_STATE_MACHINE_H

#include "smooth/core/ipc/TaskEventQueue.h"
#include "smooth/core/fsm/StaticFSM.h"
#include "smooth/core/network/IPv4.h"
#include "PacketTypes.h"
#include "UdpSocket.h"

namespace sm1000neo::radio::icom
{
    class ProtocolStateMachine;
    
    constexpr std::size_t LargestStateSize = 64;
    
    class BaseState
    {
    public:
        explicit BaseState(ProtocolStateMachine& sm)
            : sm_(sm)
        {
            // empty
        }
        
        virtual ~BaseState() = default;
        
        virtual void enter_state() {}
        virtual void leave_state() {}
        
        virtual std::string name() = 0;
        
    protected:
        ProtocolStateMachine& sm_;
    };
    
    class ProtocolStateMachine 
        : public StaticFSM<BaseState, LargestStateSize>
        , public core::ipc::IEventListener<smooth::core::network::event::DataAvailableEvent<IcomProtocol>>
    {
    public:
        ProtocolStateMachine();
        virtual ~ProtocolStateMachine() = default;
        
        /*void f()
        {
            get_state()->f();
        }*/
        
        [[nodiscard]] std::string get_name() const
        {
            return get_state()->name();
        }
        
        void start(std::string ip, uint16_t controlPort);
        
        void event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event) override;
    private:
        std::shared_ptr<smooth::core::network::BufferContainer<IcomProtocol>> buffer_;
        std::shared_ptr<smooth::core::network::UdpSocket> socket_;
        std::shared_ptr<smooth::core::network::InetAddress> address_;
    };
    
    class IdleState : public BaseState
    {
        public:
            explicit IdleState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
            }

            std::string name() override
            {
                return "IdleState";
            }

            //void f() override;
    };
    
}

#endif // RADIO__ICOM__PROTOCOL_STATE_MACHINE_H