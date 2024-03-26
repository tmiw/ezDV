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

#ifndef ARE_YOU_READY_STATE_H
#define ARE_YOU_READY_STATE_H

#include "task/DVTimer.h"
#include "IcomProtocolState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class AreYouReadyState : public IcomProtocolState
{
public:
    AreYouReadyState(IcomStateMachine* parent);
    virtual ~AreYouReadyState() = default;

    virtual void onEnterState() override;
    virtual void onExitState() override;
    
    virtual std::string getName() override;

    virtual void onReceivePacket(IcomPacket& packet) override;

protected:
    virtual void onReceivePacketImpl_(IcomPacket& packet) = 0;

private:
    DVTimer areYouReadyTimer_;

    void onAreYouReadyTimer_(DVTimer*);
};

}

}

}

#endif // STATE_MACHINE_STATE_H