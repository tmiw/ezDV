/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2022 Mooneer Salem
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "IcomStateMachine.h"
#include "IcomProtocolState.h"
#include "IcomPacket.h"

namespace ezdv
{

namespace network
{

namespace icom
{

IcomStateMachine::IcomStateMachine(DVTask* owner)
    : StateMachine(owner)
    , socket_(0)
    , ourIdentifier_(0)
    , theirIdentifier_(0)
    , port_(0)
{
    // empty
}

std::string IcomStateMachine::getUsername()
{
    return username_;
}

std::string IcomStateMachine::getPassword()
{
    return password_;
}

uint32_t IcomStateMachine::getOurIdentifier()
{
    return ourIdentifier_;
}

uint32_t IcomStateMachine::getTheirIdentifier()
{
    return theirIdentifier_;
}

void IcomStateMachine::setTheirIdentifier(uint32_t id)
{
    theirIdentifier_ = id;
}

void IcomStateMachine::sendUntracked(IcomPacket& packet)
{
    auto rv = send(socket_, packet.getData(), packet.getSendLength(), 0);
    if (rv == -1)
    {
        auto err = errno;
        ESP_LOGE(getName().c_str(), "Got socket error %d (%s) while sending", err, strerror(err));

        // TBD: close and reopen
    }
}

void IcomStateMachine::start(std::string ip, uint16_t port, std::string username, std::string password, int localPort)
{
    ip_ = ip;
    port_ = port;
    username_ = username;
    password_ = password;

    struct sockaddr_in radioAddress;
    radioAddress.sin_addr.s_addr = inet_addr(ip_.c_str());
    radioAddress.sin_family = AF_INET;
    radioAddress.sin_port = htons(port_);

    // Create and bind UDP socket to force the specified local port number.
    if (localPort == 0)
    {
        localPort = port_;
    }
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == -1)
    {
        auto err = errno;
        ESP_LOGE(getName().c_str(), "Got socket error %d (%s) while creating socket", err, strerror(err));
    }
    assert(socket_ != -1);

    struct sockaddr_in ourSocketAddress;
    memset((char *) &ourSocketAddress, 0, sizeof(ourSocketAddress));

    ourSocketAddress.sin_family = AF_INET;
    ourSocketAddress.sin_port = htons(localPort);
    ourSocketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        
    auto rv = bind(socket_, (struct sockaddr*)&ourSocketAddress, sizeof(ourSocketAddress));
    if (rv == -1)
    {
        auto err = errno;
        ESP_LOGE(getName().c_str(), "Got socket error %d (%s) while binding", err, strerror(err));
    }
    assert(rv != -1);

    // Generate our identifier by concatenating the last two octets of our IP
    // with the port we're using to connect. We bind to this port ourselves prior
    // to connection.
    uint32_t localIp = radioAddress.sin_addr.s_addr;
    ourIdentifier_ = 
        (((localIp >> 8) & 0xFF) << 24) | 
        ((localIp & 0xFF) << 16) |
        (localPort & 0xFFFF);

    // Connect to the radio.
    rv = connect(socket_, (struct sockaddr*)&radioAddress, sizeof(radioAddress));
    if (rv == -1)
    {
        auto err = errno;
        ESP_LOGE(getName().c_str(), "Got socket error %d (%s) while connecting", err, strerror(err));
    }
    assert(rv != -1);

    // We're now connected, start running the state machine.
    transitionState(IcomProtocolState::ARE_YOU_THERE);
}

void IcomStateMachine::readPendingPackets()
{
    auto state = getProtocolState_();
    
    // Skip processing if we're not connected yet.
    if (state == nullptr)
    {
        return;
    }
    
    fd_set readSet;
    struct timeval tv = {0, 0};
    
    FD_ZERO(&readSet);
    FD_SET(socket_, &readSet);
    
    // Loop while there are pending datagrams in the buffer
    while (select(socket_ + 1, &readSet, nullptr, nullptr, &tv) > 0)
    {
        char buffer[MAX_PACKET_SIZE];
        
        auto rv = recv(socket_, buffer, MAX_PACKET_SIZE, 0);
        if (rv > 0)
        {
            // Forward packet to current state for processing.
            IcomPacket packet(buffer, rv);
            state->onReceivePacket(packet);
        }
        
        // Reinitialize the read set for the next pass.
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);
    }
}

IcomProtocolState* IcomStateMachine::getProtocolState_()
{
    return static_cast<IcomProtocolState*>(getCurrentState());
}

}

}

}