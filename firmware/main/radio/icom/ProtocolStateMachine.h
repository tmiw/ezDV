#ifndef RADIO__ICOM__PROTOCOL_STATE_MACHINE_H
#define RADIO__ICOM__PROTOCOL_STATE_MACHINE_H

#include <map>
#include "smooth/core/Task.h"
#include "smooth/core/ipc/TaskEventQueue.h"
#include "smooth/core/fsm/StaticFSM.h"
#include "smooth/core/network/IPv4.h"
#include "smooth/core/timer/Timer.h"
#include "smooth/core/timer/TimerExpiredEvent.h"
#include "PacketTypes.h"
#include "UdpSocket.h"
#include "codec2_fifo.h"

namespace sm1000neo::radio::icom
{
    class ProtocolStateMachine;
    
    constexpr std::size_t LargestStateSize = 64;
    
    class BaseState
    {
    public:
        int ctr = 0;
                
        explicit BaseState(ProtocolStateMachine& sm)
            : sm_(sm)
        {
            // empty
        }
        
        virtual ~BaseState() = default;
        
        virtual void enter_state() { }
        
        virtual void leave_state() { }
        
        virtual std::string name() = 0;
        
        virtual void packetReceived(IcomPacket& packet) { }
        
        virtual void event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event) 
        {
            ctr++;
            
            int freeHeapSizeBefore = xPortGetFreeHeapSize();
            int largestFreeBlockBefore = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
            
            //ESP_LOGI(sm_.get_name().c_str(), "Heap memory: total free = %d, largest block = %d", xPortGetFreeHeapSize(), );
            {   
                IcomPacket packet;
                if (event.get(packet))
                {
                    packetReceived(packet);
                }
            }
            
            int freeHeapSizeAfter = xPortGetFreeHeapSize();
            int largestFreeBlockAfter = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
            
            int heapDiff = freeHeapSizeAfter - freeHeapSizeBefore;
            if (heapDiff < 0)
            {
                //ESP_LOGW(name().c_str(), "LEAK of %d bytes while handling msg (total free = %d, largest block = %d)", -heapDiff, freeHeapSizeAfter, largestFreeBlockAfter);
            }
        }
        virtual void event(const smooth::core::network::event::TransmitBufferEmptyEvent& event) { }
        
    protected:
        ProtocolStateMachine& sm_;
    };
    
    class ProtocolStateMachine 
        : public smooth::core::fsm::StaticFSM<BaseState, LargestStateSize>
        , public smooth::core::ipc::IEventListener<smooth::core::network::event::TransmitBufferEmptyEvent>
        , public smooth::core::ipc::IEventListener<smooth::core::network::event::ConnectionStatusEvent>
        , public smooth::core::ipc::IEventListener<smooth::core::network::event::DataAvailableEvent<IcomProtocol>>
        , public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>
    {
    public:        
        enum StateMachineType
        {
            CONTROL_SM,
            CIV_SM,
            AUDIO_SM,
        };
        
        ProtocolStateMachine(StateMachineType smType, smooth::core::Task& task);
        virtual ~ProtocolStateMachine();
        
        StateMachineType getStateMachineType() const;
        
        [[nodiscard]] std::string get_name() const;
        
        void start(std::string ip, uint16_t controlPort, std::string username, std::string password);
        
        void event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event) override;
        
        void event(const smooth::core::network::event::TransmitBufferEmptyEvent& event) override;
        void event(const smooth::core::network::event::ConnectionStatusEvent& event) override;
        void event(const smooth::core::timer::TimerExpiredEvent& event) override;
        
        smooth::core::Task& getTask() { return task_; }        
        uint32_t getOurIdentifier() const { return ourIdentifier_; }
        uint32_t getTheirIdentifier() const { return theirIdentifier_; }
        void setTheirIdentifier(uint32_t id) { theirIdentifier_ = id; }
        
        void setOurTokenRequest(uint16_t token) { ourTokenRequest_ = token; }
        uint16_t getOurTokenRequest() const { return ourTokenRequest_; }
        
        void sendUntracked(IcomPacket& packet);
        void sendPing();
        void sendLoginPacket();
        void sendTokenAckPacket(uint32_t theirToken);
        void sendTokenRenewPacket();
        void sendTracked(IcomPacket& packet);
        
        void setLocalIp(uint32_t ip)
        {
            localIp_ = ip;
        }
        
        void incrementPingSequence(uint16_t pingSeq)
        {
            pingSequenceNumber_ = pingSeq + 1;
        }
        
        uint16_t getCurrentPingSequence() const 
        {
            return pingSequenceNumber_;
        }
        
        void retransmitPacket(uint16_t packet);
        
        void clearRadioCapabilities()
        {
            radioCapabilities_.clear();
        }
        
        void insertCapability(radio_cap_packet_t radio)
        {
            IcomPacket packet((char*)&radio, sizeof(radio_cap_packet));
            //radioCapabilities_.push_back(std::move(packet));
            radioCapabilities_.push_back(packet);
        }
        
        void initializeCivAndAudioStateMachines(int radioIndex);
        void startCivAndAudioStateMachines(int audioPort, int civPort);
        void writeOutFifo(short* data, int len);
        
        std::map<uint16_t, IcomPacket> rxAudioPackets_;
        uint16_t lastAudioPacketSeqId_;
        
    private:
        StateMachineType smType_;
        smooth::core::Task& task_;
        uint32_t ourIdentifier_;
        uint32_t theirIdentifier_;
        uint16_t pingSequenceNumber_;
        uint16_t authSequenceNumber_;
        uint16_t sendSequenceNumber_;
        std::shared_ptr<smooth::core::network::BufferContainer<IcomProtocol>> buffer_;
        std::shared_ptr<smooth::core::network::Socket<IcomProtocol, IcomPacket>> socket_;
        std::shared_ptr<smooth::core::network::InetAddress> address_;
        
        std::map<uint16_t, std::pair<uint64_t, IcomPacket> > sentPackets_;
        
        std::string username_;
        std::string password_;
        uint32_t localIp_;
        
        uint32_t ourTokenRequest_;
        uint32_t theirToken_;
        uint32_t numSavedBytesInPacketQueue_;
        
        std::vector<IcomPacket> radioCapabilities_;
        
        std::shared_ptr<ProtocolStateMachine> civStateMachine_;
        std::shared_ptr<ProtocolStateMachine> audioStateMachine_;
        int civSocket_;
        int audioSocket_;
        struct FIFO* outFifo_;
        
        smooth::core::timer::TimerOwner audioOutTimer_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> timerExpiredQueue_;
        
        // For use only within the root SM for starting the auxiliary ones.
        void start(std::string ip, uint16_t auxPort, int socket);
    };
    
    class AreYouThereState 
        : public BaseState
        , public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>
    {
        public:
            explicit AreYouThereState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
                // empty
            }

            virtual ~AreYouThereState() = default;
            
            std::string name() override
            {
                return "AreYouThere";
            }

            virtual void enter_state() override;
            virtual void leave_state() override;
            
            virtual void event(const smooth::core::timer::TimerExpiredEvent& event) override;
            virtual void packetReceived(IcomPacket& packet) override;
            virtual void event(const smooth::core::network::event::TransmitBufferEmptyEvent& event) override;
            
        private:
            smooth::core::timer::TimerOwner areYouThereRetransmitTimer_;
            std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> areYouThereTimerExpiredQueue_;
    };
    
    class AreYouReadyState 
        : public BaseState
    {
        public:
            explicit AreYouReadyState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
                // empty
            }

            virtual ~AreYouReadyState() = default;
            
            std::string name() override
            {
                return "AreYouReady";
            }

            virtual void enter_state() override;
            virtual void leave_state() override;
            
            virtual void packetReceived(IcomPacket& packet) override;
    };
    
    class LoginState 
        : public BaseState
        , public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>
    {
        public:
            explicit LoginState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
                // empty
            }

            virtual ~LoginState() = default;
            
            std::string name() override
            {
                return "Login";
            }

            virtual void enter_state() override;
            virtual void leave_state() override;
            
            virtual void packetReceived(IcomPacket& packet) override;
            
            virtual void event(const smooth::core::timer::TimerExpiredEvent& event) override;
            
        private:
            smooth::core::timer::TimerOwner pingTimer_;
            smooth::core::timer::TimerOwner idleTimer_;
            smooth::core::timer::TimerOwner tokenRenewTimer_;
            std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> timerExpiredQueue_;
    };
}

#endif // RADIO__ICOM__PROTOCOL_STATE_MACHINE_H
