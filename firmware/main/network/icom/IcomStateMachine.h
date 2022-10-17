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

#ifndef ICOM_STATE_MACHINE_H
#define ICOM_STATE_MACHINE_H

#include "StateMachine.h"
#include "IcomMessage.h"
#include "IcomPacket.h"
#include "task/DVTimer.h"

using namespace ezdv::task;

namespace ezdv
{

namespace network
{

namespace icom
{

class IcomProtocolState;

class IcomStateMachine : public StateMachine
{
public:
    IcomStateMachine(DVTask* owner);
    virtual ~IcomStateMachine() = default;

    uint32_t getOurIdentifier();
    uint32_t getTheirIdentifier();
    void setTheirIdentifier(uint32_t id);

    void start(std::string ip, uint16_t port, std::string username, std::string password, int localPort = 0);

    void sendUntracked(IcomPacket& packet);

    std::string getUsername();
    std::string getPassword();
    
    void readPendingPackets();

protected:
    virtual std::string getName_() = 0;

    virtual void onTransitionComplete_();

private:
    int socket_;

    uint32_t ourIdentifier_;
    uint32_t theirIdentifier_;

    std::string ip_;
    uint16_t port_;
    std::string username_;
    std::string password_;
    uint16_t localPort_;
    
    DVTimer packetReadTimer_;

    IcomProtocolState* getProtocolState_();

    void onSendPacket_(DVTask* owner, SendPacketMessage* message);
    void onReceivePacket_(DVTask* owner, ReceivePacketMessage* message);
    void onCloseSocket_(DVTask* owner, CloseSocketMessage* message);
    
    void openSocket_();
};

}

}

}

#endif // ICOM_STATE_MACHINE_H