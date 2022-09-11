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

#ifndef TRACKED_PACKET_STATE_H
#define TRACKED_PACKET_STATE_H

#include "task/DVTimer.h"
#include "IcomProtocolState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class TrackedPacketState : public IcomProtocolState
{
public:
    TrackedPacketState(IcomStateMachine* parent);
    virtual ~TrackedPacketState() = default;

    virtual void onEnterState() override;
    virtual void onExitState() override;

    virtual void onReceivePacket(IcomPacket& packet) override;

protected:
    DVTimer pingTimer_;
    DVTimer idleTimer_;

    void sendTracked_(IcomPacket& packet);

private:
    uint16_t pingSequenceNumber_;
    uint16_t sendSequenceNumber_;

    uint32_t numSavedBytesInPacketQueue_;

    std::map<uint16_t, std::pair<uint64_t, IcomPacket> > sentPackets_;

    void sendPing_();
    void retransmitPacket_(uint16_t packet);

    void onPingTimer_();
    void onIdleTimer_();

    void incrementPingSequence_(uint16_t pingSeq);

};

}

}

}

#endif // TRACKED_PACKET_STATE_H