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

#ifndef LOGIN_STATE_H
#define LOGIN_STATE_H

#include "TrackedPacketState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class LoginState : public TrackedPacketState
{
public:
    LoginState(IcomStateMachine* parent);
    virtual ~LoginState() = default;

    virtual void onEnterState() override;
    virtual void onExitState() override;

    virtual std::string getName() override;

    virtual void onReceivePacket(IcomPacket& packet) override;

private:
    DVTimer tokenRenewTimer_;
    std::vector<IcomPacket> radioCapabilities_;
    uint32_t ourTokenRequest_;
    uint32_t theirToken_;
    uint16_t authSequenceNumber_;
    int civPort_;
    int audioPort_;

    void sendLoginPacket_();
    void sendTokenAckPacket_(uint32_t theirToken);
    void sendTokenRenewPacket_();
    void sendUseRadioPacket_(int radioIndex);

    void insertCapability_(radio_cap_packet_t radio);
    void clearRadioCapabilities_();

    void onTokenRenewTimer_();
};

}

}

}

#endif // LOGIN_STATE_H