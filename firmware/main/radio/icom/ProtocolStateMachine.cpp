#include <random>
#include "ProtocolStateMachine.h"

namespace sm1000neo::radio::icom
{
    ProtocolStateMachine::ProtocolStateMachine(StateMachineType smType, smooth::core::Task& task)
        : smType_(smType)
        , task_(task)
        , ourIdentifier_(0)
        , theirIdentifier_(0)
        , pingSequenceNumber_(0)
        , authSequenceNumber_(0)
        , sendSequenceNumber_(0)
    {
        // Generate random four byte identifier. This is sent in every packet.
        std::random_device r;
        std::default_random_engine generator(r());
        std::uniform_int_distribution<uint32_t> uniform_dist(0, UINT32_MAX);
        ourIdentifier_ = uniform_dist(generator);
    }
    
    ProtocolStateMachine::StateMachineType ProtocolStateMachine::getStateMachineType() const
    {
        return smType_;
    }
    
    std::string ProtocolStateMachine::get_name() const
    {
        std::string prefix = "IcomUdpProtocol/";
        
        switch(smType_)
        {
            case CONTROL_SM:
                prefix = prefix + "Control";
                break;
            case CIV_SM:
                prefix = prefix + "CIV";
                break;
            case AUDIO_SM:
                prefix = prefix + "Audio";
                break;
            default:
                assert(0);
        }
        
        return prefix + "[" + get_state()->name() + "]";
    }
    
    void ProtocolStateMachine::sendUntracked(IcomPacket& packet)
    {
        socket_->send(packet);
    }
    
    void ProtocolStateMachine::sendPing()
    {
        auto packet = IcomPacket::CreatePingPacket(pingSequenceNumber_, ourIdentifier_, theirIdentifier_);
        socket_->send(packet);
    }
    
    void ProtocolStateMachine::sendLoginPacket()
    {
        auto packet = IcomPacket::CreateLoginPacket(authSequenceNumber_++, ourIdentifier_, theirIdentifier_, username_, password_, "sm1000neo");
        sendTracked(packet);
    }
    
    void ProtocolStateMachine::sendTracked(IcomPacket& packet)
    {
        auto typedPacket = packet.getTypedPacket<control_packet>();
        typedPacket->seq = ToBigEndian(sendSequenceNumber_++);
        
        sentPackets_[sendSequenceNumber_ - 1] = std::pair(time(NULL), packet);
        socket_->send(packet);
    }
    
    void ProtocolStateMachine::start(std::string ip, uint16_t controlPort, std::string username, std::string password)
    {
        username_ = username;
        password_ = password;
        
        address_ = std::shared_ptr<smooth::core::network::InetAddress>(new smooth::core::network::IPv4(ip, controlPort));
        
        buffer_ = 
            std::make_shared<smooth::core::network::BufferContainer<IcomProtocol>>(task_,
                                                                                   *this,
                                                                                   *this,
                                                                                   *this,
                                                                                   std::make_unique<IcomProtocol>());
                                                                                   
        socket_ = UdpSocket<IcomProtocol, IcomPacket>::create(buffer_);
        socket_->start(address_);
    }
    
    void ProtocolStateMachine::event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event)
    {
        get_state()->event(event);
    }
    
    void ProtocolStateMachine::event(const smooth::core::network::event::ConnectionStatusEvent& event)
    { 
        if (event.is_connected())
        {
            set_state(new (this) AreYouThereState(*this));
        }
    }
    
    // 1. Control: Send Are You There message.
    void AreYouThereState::enter_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Entering state");
        
        // Send packet.
        auto packet = IcomPacket::CreateAreYouTherePacket(sm_.getOurIdentifier(), sm_.getTheirIdentifier());
        sm_.sendUntracked(packet);
        
        // Start timer to trigger resend if we don't get any response from the radio.
        areYouThereTimerExpiredQueue_ =
            smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(1, sm_.getTask(), *this);
        areYouThereRetransmitTimer_ = 
            smooth::core::timer::Timer::create(
                    1, areYouThereTimerExpiredQueue_, true,
                    std::chrono::milliseconds(AREYOUTHERE_PERIOD));
        areYouThereRetransmitTimer_->start();
    }
    
    void AreYouThereState::event(const smooth::core::timer::TimerExpiredEvent& event)
    {
        ESP_LOGI(sm_.get_name().c_str(), "Retransmitting AreYouThere packet");
        
        auto packet = IcomPacket::CreateAreYouTherePacket(sm_.getOurIdentifier(), sm_.getTheirIdentifier());
        sm_.sendUntracked(packet);
    }
    
    void AreYouThereState::packetReceived(IcomPacket& packet)
    {
        uint32_t theirId = 0;
        if (packet.isIAmHere(theirId))
        {
            ESP_LOGI(sm_.get_name().c_str(), "Received I Am Here from %x", theirId);
            
            sm_.setTheirIdentifier(theirId);
            sm_.set_state(new (sm_) AreYouReadyState(sm_));
        }
        
        // Ignore unexpected packets. TBD -- may need to send Disconnect instead?
    }
    
    void AreYouThereState::leave_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Leaving state");
        
        // Stop timer. The destructor of this class is called immediately after this
        // to free up memory.
        areYouThereRetransmitTimer_->stop();
    }
    
    // 2. Control: Send Are You Ready
    void AreYouReadyState::enter_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Entering state");
    }
        
    void AreYouReadyState::packetReceived(IcomPacket& packet)
    {
        if (packet.isIAmReady())
        {
            ESP_LOGI(sm_.get_name().c_str(), "Received I Am Ready");
            sm_.set_state(new (sm_) LoginState(sm_));
        }
    }
    
    void AreYouReadyState::leave_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Leaving state");
    }
    
    // 2. Control: Send Login
    void LoginState::enter_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Entering state");
        sm_.sendLoginPacket();
    }
        
    void LoginState::packetReceived(IcomPacket& packet)
    {
       
    }
    
    void LoginState::leave_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Leaving state");
    }
}