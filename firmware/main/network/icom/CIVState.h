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

#ifndef CIV_STATE_H
#define CIV_STATE_H

#include "TrackedPacketState.h"
#include "audio/FreeDVMessage.h" // so we can listen for PTT requests

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class CIVState : public TrackedPacketState
{
public:
    CIVState(IcomStateMachine* parent);
    virtual ~CIVState() = default;

    virtual void onEnterState() override;
    virtual void onExitState() override;

    virtual std::string getName() override;

    virtual void onReceivePacket(IcomPacket& packet) override;

private:
    DVTimer civWatchdogTimer_;
    uint16_t civSequenceNumber_;
    uint8_t civId_;
    
    void sendCIVOpenPacket_();
    void sendCIVClosePacket_();
    void sendCIVPacket_(uint8_t* packet, uint16_t size);

    void onCIVWatchdog_();
    void onFreeDVSetPTTStateMessage_(DVTask* origin, ezdv::audio::FreeDVSetPTTStateMessage* message);
};

}

}

}

#endif // LOGIN_STATE_H