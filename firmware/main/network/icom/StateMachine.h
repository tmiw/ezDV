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

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <string>
#include <map>
#include "task/DVTask.h"
#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(STATE_MACHINE_MESSAGE);
}

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class StateMachineState;

/// @brief Represents a state machine in the application.
class StateMachine
{
public:
    StateMachine(DVTask* owner);
    virtual ~StateMachine() = default;

    void transitionState(int newState);
    void reset();
    
    std::string getName();
    DVTask* getTask();
    
    StateMachineState* getCurrentState();

protected:
    virtual std::string getName_() = 0;
    
    // Optional; default is no-op.
    virtual void onTransitionComplete_();

    void addState_(int stateId, StateMachineState* state);
    
private:
    class StateMachineTransitionMessage : public DVTaskMessageBase<1, StateMachineTransitionMessage>
    {
    public:
        StateMachineTransitionMessage(int newStateProvided = 0)
            : DVTaskMessageBase<1, StateMachineTransitionMessage>(STATE_MACHINE_MESSAGE)
            , newState(newStateProvided)
            {}
        virtual ~StateMachineTransitionMessage() = default;

        int newState;
    };

    std::map<int, StateMachineState*> stateIdToStateMap_;
    DVTask* owner_;
    StateMachineState* currentState_;

    void onStateMachineTransition_(DVTask* origin, StateMachineTransitionMessage* message);
};

}

}

}

#endif // STATE_MACHINE_H