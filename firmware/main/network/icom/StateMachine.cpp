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

#include <cassert>
#include "StateMachine.h"
#include "StateMachineState.h"

extern "C"
{
    DV_EVENT_DEFINE_BASE(STATE_MACHINE_MESSAGE);
}

namespace ezdv
{

namespace network
{

namespace icom
{

StateMachine::StateMachine(DVTask* owner)
    : owner_(owner)
    , currentState_(nullptr)
{
    owner_->registerMessageHandler(this, &StateMachine::onStateMachineTransition_);
}

DVTask* StateMachine::getTask()
{
    return owner_;
}

StateMachineState* StateMachine::getCurrentState()
{
    return currentState_;
}

std::string StateMachine::getName()
{
    std::string retVal = getName_() + "/";
    if (currentState_ != nullptr)
    {
        retVal += currentState_->getName();
    }
    else
    {
        retVal += "none";
    }

    return retVal;
}

void StateMachine::transitionState(int newState)
{
    // Queue up state transition
    StateMachineTransitionMessage message(newState);
    owner_->post(&message);
}

void StateMachine::addState_(int stateId, StateMachineState* state)
{
    stateIdToStateMap_[stateId] = state;
}

void StateMachine::onStateMachineTransition_(DVTask* origin, StateMachineTransitionMessage* message)
{
    StateMachineState* newState = stateIdToStateMap_[message->newState];
    assert(newState != nullptr);

    if (currentState_ != nullptr)
    {
        currentState_->onExitState();
    }

    currentState_ = newState;
    currentState_->onEnterState();
}

}

}

}