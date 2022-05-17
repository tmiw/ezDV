#include <random>
#include "ProtocolStateMachine.h"
#include "../../audio/Constants.h"
#include "../../codec/FreeDVTask.h"
#include "../../util/NamedQueue.h"

namespace sm1000neo::radio::icom
{
    ProtocolStateMachine::ProtocolStateMachine(StateMachineType smType, smooth::core::Task& task)
        : lastAudioPacketSeqId_(0)
        , smType_(smType)
        , task_(task)
        , ourIdentifier_(0)
        , theirIdentifier_(0)
        , pingSequenceNumber_(0)
        , authSequenceNumber_(0)
        , sendSequenceNumber_(1) // Start sequence at 1.
        , civSequenceNumber_(0)
        , localIp_(0)
        , numSavedBytesInPacketQueue_(0)
        , civSocket_(0)
        , audioSocket_(0)
        , civId_(0)
    {
        outFifo_ = codec2_fifo_create(1920);
        assert(outFifo_ != nullptr);
    }
    
    ProtocolStateMachine::~ProtocolStateMachine()
    {
        codec2_fifo_destroy(outFifo_);
    }
    
    void ProtocolStateMachine::writeOutFifo(short* data, int len)
    {
        codec2_fifo_write(outFifo_, data, len);
    }
    
    void ProtocolStateMachine::event(const smooth::core::timer::TimerExpiredEvent& event)
    {
        /*while (rxAudioPackets_.size() > MAX_RX_AUDIO_PACKETS)
        {
            auto frontPacket = rxAudioPackets_.begin()->second;
            short* audioData;
            uint16_t audioSeqId;
            
            // Extract raw audio data.
            frontPacket.isAudioPacket(audioSeqId, &audioData);
            
            int totalSize = (frontPacket.get_send_length() - 0x18) / sizeof(short);
            writeOutFifo(audioData, totalSize);  
            
            rxAudioPackets_.erase(rxAudioPackets_.begin());          
        }*/
        
        while (codec2_fifo_used(outFifo_) >= I2S_NUM_SAMPLES_PER_INTERVAL)
        {
            short tmpAudio[I2S_NUM_SAMPLES_PER_INTERVAL];
            memset(tmpAudio, 0, sizeof(short) * I2S_NUM_SAMPLES_PER_INTERVAL);
            
            codec2_fifo_read(outFifo_, tmpAudio, I2S_NUM_SAMPLES_PER_INTERVAL);
            
            auto& task = sm1000neo::codec::FreeDVTask::ThisTask();
            task.enqueueAudio(sm1000neo::audio::ChannelLabel::RADIO_CHANNEL, tmpAudio, I2S_NUM_SAMPLES_PER_INTERVAL);
        }
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
        auto typedPacket = packet.getConstTypedPacket<login_packet>();
        
        setOurTokenRequest(typedPacket->tokrequest);
        sendTracked(packet);
    }
    
    void ProtocolStateMachine::sendCIVOpenPacket()
    {
        ESP_LOGI(get_name().c_str(), "Sending CIV open packet");
        auto packet = IcomPacket::CreateCIVOpenClosePacket(civSequenceNumber_++, ourIdentifier_, theirIdentifier_, false);
        sendTracked(packet);
    }
    
    void ProtocolStateMachine::sendCIVPacket(uint8_t* civPacket, uint16_t civLength)
    {
        if (civStateMachine_ != nullptr)
        {
            // Forward to CIV SM as the control channel has no idea how to handle these.
            civStateMachine_->sendCIVPacket(civPacket, civLength);
        }
        else
        {
            ESP_LOGI(get_name().c_str(), "Sending CIV data packet");
            
            auto packet = IcomPacket::CreateCIVPacket(ourIdentifier_, theirIdentifier_, civSequenceNumber_++, civPacket, civLength);
            sendTracked(packet);
        }
    }
    
    void ProtocolStateMachine::sendTokenAckPacket(uint32_t theirToken)
    {
        theirToken_ = theirToken;
        
        auto packet = IcomPacket::CreateTokenAckPacket(authSequenceNumber_++, ourTokenRequest_, theirToken, ourIdentifier_, theirIdentifier_);
        sendTracked(packet);
    }
    
    void ProtocolStateMachine::sendTokenRenewPacket()
    {
        auto packet = IcomPacket::CreateTokenRenewPacket(authSequenceNumber_++, ourTokenRequest_, theirToken_, ourIdentifier_, theirIdentifier_);
        sendTracked(packet);
    }
    
    void ProtocolStateMachine::sendTracked(IcomPacket& packet)
    {
        uint8_t* rawPacket = const_cast<uint8_t*>(packet.get_data());
        
        // If sequence number is now 0, we've probably rolled over.
        if (sendSequenceNumber_ == 0)
        {
            sentPackets_.clear();
            numSavedBytesInPacketQueue_ = 0;
        }
        else
        {
            // Iterate through the current sent queue and delete packets
            // that are older than PURGE_SECONDS.
            auto iter = sentPackets_.begin();
            auto curTime = time(NULL);
            while (iter != sentPackets_.end())
            {
                if (curTime - iter->second.first >= PURGE_SECONDS)
                {
                    numSavedBytesInPacketQueue_ -= iter->second.second.get_send_length();
                    iter = sentPackets_.erase(iter);
                }
                else
                {
                    iter++;
                }
            }
            
            // If we're still going to have more than MAX_NUM_BYTES_AVAILABLE_FOR_RETRANSMIT
            // in the queue after adding this packet, go ahead and delete some more.
            if ((numSavedBytesInPacketQueue_ + packet.get_send_length()) >= MAX_NUM_BYTES_AVAILABLE_FOR_RETRANSMIT)
            {
                auto iter = sentPackets_.begin();
                while (iter != sentPackets_.end())
                {
                    numSavedBytesInPacketQueue_ -= iter->second.second.get_send_length();
                    iter = sentPackets_.erase(iter);
                
                    if (numSavedBytesInPacketQueue_ < MAX_NUM_BYTES_AVAILABLE_FOR_RETRANSMIT)
                    {
                        break;
                    }
                }
            }
        }
        
        // We need to manually force the sequence number into the packet because
        // simply treating it like a control_packet doesn't work.
        rawPacket[6] = sendSequenceNumber_ & 0xFF;
        rawPacket[7] = (sendSequenceNumber_ >> 8) & 0xFF;
        sendSequenceNumber_++;
        
        numSavedBytesInPacketQueue_ += packet.get_send_length();
        socket_->send(packet);
        sentPackets_[sendSequenceNumber_ - 1] = std::pair(time(NULL), std::move(packet));
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
                                                                                   
        // Generate our identifier by concatenating the last two octets of our IP
        // with the port we're using to connect. We bind to this port in UdpSocket prior to connection.
        ourIdentifier_ = 
            (((localIp_ >> 8) & 0xFF) << 24) | 
            ((localIp_ & 0xFF) << 16) |
            (controlPort & 0xFFFF);
        
        socket_ = UdpSocket<IcomProtocol, IcomPacket>::create(buffer_, std::chrono::milliseconds(0), std::chrono::milliseconds(0));
        socket_->start(address_);
    }
    
    void ProtocolStateMachine::start(std::string ip, uint16_t auxPort, int socket)
    {
        address_ = std::shared_ptr<smooth::core::network::InetAddress>(new smooth::core::network::IPv4(ip, auxPort));
        
        buffer_ = 
            std::make_shared<smooth::core::network::BufferContainer<IcomProtocol>>(task_,
                                                                                   *this,
                                                                                   *this,
                                                                                   *this,
                                                                                   std::make_unique<IcomProtocol>());
                                                                                   
        // Generate our identifier by concatenating the last two octets of our IP
        // with the port we're using to connect. We bind to this port in UdpSocket prior to connection.
        ourIdentifier_ = 
            (((localIp_ >> 8) & 0xFF) << 24) | 
            ((localIp_ & 0xFF) << 16) |
            (auxPort & 0xFFFF);
        
        socket_ = UdpSocket<IcomProtocol, IcomPacket>::create(address_, socket, buffer_, std::chrono::milliseconds(0), std::chrono::milliseconds(0));
        socket_->start(address_);
        
        if (getStateMachineType() == AUDIO_SM)
        {
            timerExpiredQueue_ =
                smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(2, getTask(), *this);
            audioOutTimer_ = 
                smooth::core::timer::Timer::create(
                        1, timerExpiredQueue_, true,
                        std::chrono::milliseconds(10));
            audioOutTimer_->start();
        }
    }
    
    void ProtocolStateMachine::event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event)
    {
        get_state()->event(event);
    }
    
    void ProtocolStateMachine::event(const smooth::core::network::event::ConnectionStatusEvent& event)
    { 
        if (event.is_connected())
        {
            set_state(new AreYouThereState(*this));
        }
    }
    
    void ProtocolStateMachine::event(const smooth::core::network::event::TransmitBufferEmptyEvent& event)
    {
        get_state()->event(event);
    }
    
    void ProtocolStateMachine::retransmitPacket(uint16_t packet)
    {
        ESP_LOGI(get_name().c_str(), "Retransmitting packet %d", packet);
        
        if (sentPackets_.find(packet) != sentPackets_.end())
        {
            // No need to track as we've sent it before.
            sendUntracked(sentPackets_[packet].second);
        }
        else
        {
            // Send idle packet with the same seq# if we can't find the original packet.
            IcomPacket tmpPacket = IcomPacket::CreateIdlePacket(packet, getOurIdentifier(), getTheirIdentifier());
            sendUntracked(tmpPacket);
        }
    }
    
    void ProtocolStateMachine::initializeCivAndAudioStateMachines(int radioIndex)
    {
        if (civSocket_ != 0 || audioSocket_ != 0)
        {
            // already in progress, don't do again
            return;
        }
        
        // We need to create CIV and audio sockets and get their local port numbers.
        // The protocol seems to require it, which is weird but ok.
        civSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        audioSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        assert(civSocket_ > 0 && audioSocket_ > 0); // TBD -- should just force a disconnect of the main SM instead.
        
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		sin.sin_port = 0;
		assert(bind(civSocket_, (struct sockaddr *) &sin, sizeof sin) >= 0);
        
        socklen_t addressLength = sizeof(sin);
        getsockname(civSocket_, (struct sockaddr*)&sin, &addressLength);
        int civPort = ntohs(sin.sin_port);
        
        ESP_LOGI(get_name().c_str(), "Local UDP port for CIV comms will be %d", civPort);
        
        sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		sin.sin_port = 0;
		assert(bind(audioSocket_, (struct sockaddr *) &sin, sizeof sin) >= 0);
        
        addressLength = sizeof(sin);
        getsockname(audioSocket_, (struct sockaddr*)&sin, &addressLength);
        int audioPort = ntohs(sin.sin_port);
        
        ESP_LOGI(get_name().c_str(), "Local UDP port for audio comms will be %d", audioPort);
        
        IcomPacket packet(sizeof(conninfo_packet));
        auto typedPacket = packet.getTypedPacket<conninfo_packet>();
        
        typedPacket->len = sizeof(conninfo_packet);
        typedPacket->sentid = getOurIdentifier();
        typedPacket->rcvdid = getTheirIdentifier();
        typedPacket->payloadsize = ToBigEndian((uint16_t)(sizeof(conninfo_packet) - 0x10));
        typedPacket->requesttype = 0x03;
        typedPacket->requestreply = 0x01;
        
        IcomPacket& capPacket = radioCapabilities_[radioIndex];
        auto cap = capPacket.getConstTypedPacket<radio_cap_packet>();
        if (cap->commoncap == 0x8010)
        {
            // can use MAC address in packet
            typedPacket->commoncap = 0x8010;
            memcpy(&typedPacket->macaddress, cap->macaddress, 6);
        }
        else
        {
            memcpy(&typedPacket->guid, cap->guid, GUIDLEN);
        }
        
        typedPacket->innerseq = ToBigEndian(authSequenceNumber_++);
        typedPacket->tokrequest = ourTokenRequest_;
        typedPacket->token = theirToken_;
        memcpy(typedPacket->name, cap->name, strlen(cap->name));
        typedPacket->rxenable = 1;
        typedPacket->txenable = 1;
        
        // Force 8K sample rate PCM
        typedPacket->rxcodec = 4;
        typedPacket->rxsample = ToBigEndian((uint32_t)8000);
        typedPacket->txcodec = 4;
        typedPacket->txsample = ToBigEndian((uint32_t)8000);
        
        // CIV/audio local port numbers and latency
        typedPacket->civport = ToBigEndian((uint32_t)civPort);
        typedPacket->audioport = ToBigEndian((uint32_t)audioPort);
        typedPacket->txbuffer = ToBigEndian((uint32_t)150);
        typedPacket->convert = 1;
        
        sendTracked(packet);
        
        // Create child state machines
        civStateMachine_ = std::make_shared<ProtocolStateMachine>(CIV_SM, task_);
        civStateMachine_->localIp_ = localIp_;
        audioStateMachine_ = std::make_shared<ProtocolStateMachine>(AUDIO_SM, task_);
        audioStateMachine_->localIp_ = localIp_;
    }
    
    void ProtocolStateMachine::startCivAndAudioStateMachines(int audioPort, int civPort)
    {
        audioStateMachine_->start(address_->get_host(), audioPort, audioSocket_);
        civStateMachine_->start(address_->get_host(), civPort, civSocket_);
    }
    
    // 1. Control/CIV/Audio: Send Are You There message.
    void AreYouThereState::enter_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Entering state");
        
        // Send packet.
        auto packet = IcomPacket::CreateAreYouTherePacket(sm_.getOurIdentifier(), sm_.getTheirIdentifier());
        sm_.sendUntracked(packet);
    }
    
    void AreYouThereState::event(const smooth::core::network::event::TransmitBufferEmptyEvent& event)
    {
        // Start timer to trigger resend if we don't get any response from the radio.
        areYouThereTimerExpiredQueue_ =
            smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(1, sm_.getTask(), *this);
        areYouThereRetransmitTimer_ = 
            smooth::core::timer::Timer::create(
                    1, areYouThereTimerExpiredQueue_, false,
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
    
    // 2. Control/CIV: Send Are You Ready
    // 2. Audio: Skip to Login
    void AreYouReadyState::enter_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Entering state");
        
        auto packet = IcomPacket::CreateAreYouReadyPacket(sm_.getOurIdentifier(), sm_.getTheirIdentifier());
        sm_.sendUntracked(packet);
        
        // If we're an audio state machine, we're already getting audio at this point.
        // We can go straight to the main state and handle the I Am Ready response there.
        if (sm_.getStateMachineType() == ProtocolStateMachine::AUDIO_SM)
        {
            sm_.set_state(new (sm_) LoginState(sm_));
        }
    }
    
    void AreYouReadyState::packetReceived(IcomPacket& packet)
    {    
        if (packet.isIAmReady())
        {
            ESP_LOGI(sm_.get_name().c_str(), "Received I Am Ready");
            
            if (sm_.getStateMachineType() == ProtocolStateMachine::CIV_SM)
            {
                // Weirdly, we need to get the remote ID again here for CIV.
                auto typedPacket = packet.getTypedPacket<control_packet>();
                sm_.setTheirIdentifier(typedPacket->sentid);
                
                sm_.set_state(new (sm_) CIVState(sm_));
            }
            else
            {
                sm_.set_state(new (sm_) LoginState(sm_));
            }
        }
    }
    
    void AreYouReadyState::leave_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Leaving state");
    }
    
    // 3. Control: Send Login
    // 3. Audio: Handle RX/TX audio
    void LoginState::enter_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Entering state");
        
        if (sm_.getStateMachineType() == ProtocolStateMachine::CONTROL_SM)
        {
            // Login packet is only necessary on the control state machine.
            sm_.sendLoginPacket();
            sm_.clearRadioCapabilities();
        }
        
        // Start ping and idle timers at this point. Idle will be stopped/started
        // whenever we send something.
        timerExpiredQueue_ =
            smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(5, sm_.getTask(), *this);
        pingTimer_ = 
            smooth::core::timer::Timer::create(
                    1, timerExpiredQueue_, true,
                    std::chrono::milliseconds(PING_PERIOD));
        idleTimer_ = 
            smooth::core::timer::Timer::create(
                    2, timerExpiredQueue_, true,
                    std::chrono::milliseconds(IDLE_PERIOD));
        tokenRenewTimer_ = 
            smooth::core::timer::Timer::create(
                    3, timerExpiredQueue_, true,
                    std::chrono::milliseconds(TOKEN_RENEWAL));
                    
        pingTimer_->start();
        idleTimer_->start();
    }
        
    void LoginState::packetReceived(IcomPacket& packet)
    {    
        std::string connType;
        bool isPasswordIncorrect;
        uint16_t tokenRequest;
        uint32_t radioToken;
        uint16_t ourTokenReq = sm_.getOurTokenRequest();
        uint16_t pingSequence;
        bool packetSent;
        
        if (packet.isLoginResponse(connType, isPasswordIncorrect, tokenRequest, radioToken))
        {
            ESP_LOGI(sm_.get_name().c_str(), "Connection type: %s", connType.c_str());
            ESP_LOGI(sm_.get_name().c_str(), "Password incorrect: %d", isPasswordIncorrect);
            ESP_LOGI(sm_.get_name().c_str(), "Token req: %x, our token req: %x, radio token: %x", tokenRequest, ourTokenReq, radioToken);
            
            if (!isPasswordIncorrect && tokenRequest == ourTokenReq)
            {
                ESP_LOGI(sm_.get_name().c_str(), "Login successful, acknowledging token");
                sm_.sendTokenAckPacket(radioToken);
                packetSent = true;
                
                // Begin renewing token every 60 seconds.
                tokenRenewTimer_->start();
            }
            else
            {
                ESP_LOGE(sm_.get_name().c_str(), "Password incorrect!");
            }
        }
        else if (packet.isPingRequest(pingSequence))
        {
            // Respond to ping requests        
            //ESP_LOGI(sm_.get_name().c_str(), "Got ping, seq %d", ctr, pingSequence);
            auto packet = std::move(IcomPacket::CreatePingAckPacket(pingSequence, sm_.getOurIdentifier(), sm_.getTheirIdentifier()));
            sm_.sendUntracked(packet);
            packetSent = true;
        }
        else if (packet.isPingResponse(pingSequence))
        {
            // Got ping response, increment to next ping sequence number.
            //ESP_LOGI(sm_.get_name().c_str(), "Got ping ack, seq %d", pingSequence);
            sm_.incrementPingSequence(pingSequence);
            
            ESP_LOGI("HEAP", "Free memory: %d", xPortGetFreeHeapSize());
        }
        else
        {
            std::vector<radio_cap_packet_t> radios;
            std::vector<uint16_t> retryPackets;
            std::string radioName;
            uint32_t radioIp;
            bool isBusy;
            bool connSuccess;
            bool connDisconnected;
            uint16_t civPort;
            uint16_t audioPort;
            short* audioData;
            uint16_t audioSeqId;
            
            if (packet.isCapabilitiesPacket(radios))
            {
                ESP_LOGI(sm_.get_name().c_str(), "Available radios:");
                int index = 0;
                for (auto& radio : radios)
                {
                    ESP_LOGI(
                        sm_.get_name().c_str(), 
                        "[%d]    %s: MAC=%02x:%02x:%02x:%02x:%02x:%02x, CIV=%02x, Audio=%s (rxsample %d, txsample %d)", 
                        index++, 
                        radio->name,
                        radio->macaddress[0], radio->macaddress[1], radio->macaddress[2], radio->macaddress[3], radio->macaddress[4], radio->macaddress[5],
                        radio->civ, 
                        radio->audio,
                        radio->rxsample,
                        radio->txsample);
                        
                    sm_.insertCapability(radio);
                }
            }
            else if (packet.isRetransmitPacket(retryPackets))
            {
                for (auto packetId : retryPackets)
                {
                    sm_.retransmitPacket(packetId);
                }
            }
            else if (packet.isConnInfoPacket(radioName, radioIp, isBusy))
            {
                ESP_LOGI(
                    sm_.get_name().c_str(), 
                    "Connection info for %s: IP = %x, Is Busy = %d",
                    radioName.c_str(),
                    radioIp,
                    isBusy ? 1 : 0);
                    
                sm_.initializeCivAndAudioStateMachines(0);
                packetSent = true;
            }
            else if (packet.isStatusPacket(connSuccess, connDisconnected, civPort, audioPort))
            {
                if (connSuccess)
                {
                    ESP_LOGI(
                        sm_.get_name().c_str(), 
                        "Starting audio and CIV state machines using remote ports %d and %d",
                        audioPort,
                        civPort);
                    
                    sm_.startCivAndAudioStateMachines(audioPort, civPort);
                }
                else if (connDisconnected)
                {
                    ESP_LOGE(
                        sm_.get_name().c_str(), 
                        "Disconnected from the radio"
                    );
                        
                    // TBD -- reset state machines
                }
                else
                {
                    ESP_LOGE(
                        sm_.get_name().c_str(), 
                        "Connection failed"
                    );
                        
                    // TBD -- reset state machines
                }
            }
            else if (packet.isAudioPacket(audioSeqId, &audioData))
            {
                /*
                if (audioSeqId == 0)
                {
                    // Clear RX buffer
                    sm_.rxAudioPackets_.clear();
                }
                
                // The audio packet has to one we haven't already received to go in the map.
                if (audioSeqId > sm_.lastAudioPacketSeqId_ && sm_.rxAudioPackets_.find(audioSeqId) == sm_.rxAudioPackets_.end())
                {
                    sm_.lastAudioPacketSeqId_ = audioSeqId;
                    sm_.rxAudioPackets_[audioSeqId] = packet;
                }
                
                // Iterate through map and look for gaps in the stream. We'll need to rerequest those missing
                // packets.
                int first = -1, second = -1;
                std::vector<uint16_t> packetIdsToRetransmit;
                for (auto& iter : sm_.rxAudioPackets_)
                {
                    first = second;
                    second = iter.first;
                    
                    if (first != -1 && second != -1)
                    {
                        for (int seq = first; seq < second; seq++)
                        {
                            // gap found, request packets
                            packetIdsToRetransmit.push_back(seq);
                            
                            ESP_LOGI(
                                sm_.get_name().c_str(), 
                                "Requesting retransmit of packet %d",
                                seq);
                        }
                    }
                }
                
                auto reqPacket = IcomPacket::CreateRetransmitRequest(sm_.getOurIdentifier(), sm_.getTheirIdentifier(), packetIdsToRetransmit);
                sm_.sendUntracked(reqPacket);*/
                int totalSize = (packet.get_send_length() - 0x18) / sizeof(short);
                sm_.writeOutFifo(audioData, totalSize);  
            }
        }
        
        if (packetSent)
        {
            // Recycle idle timer as we sent something non-idle.
            idleTimer_->stop();
            idleTimer_->start();
        }
    }
    
    void LoginState::event(const smooth::core::timer::TimerExpiredEvent& event)
    {
        switch (event.get_id())
        {
            case 1:
            {
                // Ping timer fired. Send ping request.
                //ESP_LOGI(sm_.get_name().c_str(), "Send ping, seq %d", sm_.getCurrentPingSequence());
                auto packet = IcomPacket::CreatePingPacket(sm_.getCurrentPingSequence(), sm_.getOurIdentifier(), sm_.getTheirIdentifier());
                sm_.sendUntracked(packet);
                idleTimer_->stop();
                idleTimer_->start();
                break;
            }
            case 2:
            {
                // Idle timer fired. Send control packet with seq = 0
                auto packet = IcomPacket::CreateIdlePacket(0, sm_.getOurIdentifier(), sm_.getTheirIdentifier());
                sm_.sendUntracked(packet);
                break;
            }
            case 3:
            {
                // Token renewal time
                ESP_LOGI(sm_.get_name().c_str(), "Renewing token");
                sm_.sendTokenRenewPacket();
                break;
            }
        }
    }
    
    void LoginState::leave_state() 
    {
        ESP_LOGI(sm_.get_name().c_str(), "Leaving state");
        
        idleTimer_->stop();
        pingTimer_->stop();
        tokenRenewTimer_->stop();
    }
    
    // 3. CIV: Send Open CIV and handle RX/TX of CI-V packets
    void CIVState::enter_state() 
    {
        LoginState::enter_state();
        sm_.sendCIVOpenPacket();
        
        // Send request to get the radio ID on the other side.
        uint8_t civPacket[] = {
            0xFE,
            0xFE,
            0x00,
            0xE0,
            0x19, // Request radio ID command/subcommand
            0x00,
            0xFD
        };
        
        sm_.sendCIVPacket(civPacket, sizeof(civPacket));
    }
    
    void CIVState::leave_state() 
    {
        LoginState::leave_state();
    }
    
    void CIVState::packetReceived(IcomPacket& packet)
    {
        uint8_t* civPacket;
        uint16_t civLength;
        
        if (packet.isCivPacket(&civPacket, &civLength))
        {
            // ignore for now except to get the CI-V ID of the 705. TBD
            ESP_LOGI(sm_.get_name().c_str(), "Received CIV packet (from %02x, to %02x, type %02x)", civPacket[3], civPacket[2], civPacket[4]);
            
            if (civPacket[2] == 0xE0)
            {
                sm_.setCIVId(civPacket[3]);
            }
        }
        else
        {
            LoginState::packetReceived(packet);
        }
    }
}