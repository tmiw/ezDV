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

#include "IcomStateMachine.h"
#include "IcomProtocolState.h"

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

void IcomStateMachine::start(std::string ip, uint16_t port, std::string username, std::string password)
{
    ip_ = ip;
    port_ = port;
    username_ = username;
    password_ = password;

    struct sockaddr_in radioAddress;
    radioAddress.sin_addr.s_addr = inet_addr(ip_.c_str());
    radioAddress.sin_family = AF_INET;
    radioAddress.sin_port = htons(port_);

    // Generate our identifier by concatenating the last two octets of our IP
    // with the port we're using to connect. We bind to this port ourselves prior
    // to connection.
    uint32_t localIp = radioAddress.sin_addr.s_addr;
    ourIdentifier_ = 
        (((localIp >> 8) & 0xFF) << 24) | 
        ((localIp & 0xFF) << 16) |
        (port & 0xFFFF);

    // Create and bind UDP socket to force the specified local port number.
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(socket_ != -1);

    struct sockaddr_in ourSocketAddress;
    memset((char *) &ourSocketAddress, 0, sizeof(ourSocketAddress));

    ourSocketAddress.sin_family = AF_INET;
    ourSocketAddress.sin_port = htons(port_);
    ourSocketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        
    auto rv = bind(socket_, (struct sockaddr*)&ourSocketAddress, sizeof(ourSocketAddress));
    assert(rv != -1);

    // Connect to the radio.
    rv = connect(socket_, (struct sockaddr*)&radioAddress, sizeof(radioAddress));
    assert(rv != -1);

    // We're now connected, start running the state machine.
    transitionState(IcomProtocolState::ARE_YOU_THERE);
}

IcomProtocolState* IcomStateMachine::getProtocolState_()
{
    return static_cast<IcomProtocolState*>(getCurrentState());
}

}

}

}