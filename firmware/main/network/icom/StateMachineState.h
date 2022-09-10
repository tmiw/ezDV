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

#ifndef STATE_MACHINE_STATE_H
#define STATE_MACHINE_STATE_H

#include <string>

namespace ezdv
{

namespace network
{

namespace icom
{

class StateMachineState
{
public:
    virtual ~StateMachineState() = default;

    virtual void onEnterState() = 0;
    virtual void onExitState() = 0;
    
    virtual std::string getName() = 0;

protected:
    StateMachineState() = default;
};

}

}

}

#endif // STATE_MACHINE_STATE_H