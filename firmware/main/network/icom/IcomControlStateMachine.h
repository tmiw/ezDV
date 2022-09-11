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

#ifndef ICOM_CONTROL_STATE_MACHINE_H
#define ICOM_CONTROL_STATE_MACHINE_H

#include "IcomStateMachine.h"

#include "AreYouThereState.h"
#include "AreYouReadyControlState.h"
#include "LoginState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

class IcomControlStateMachine : public IcomStateMachine
{
public:
    IcomControlStateMachine(DVTask* owner);
    virtual ~IcomControlStateMachine() = default;

protected:
    virtual std::string getName_() override;
    
private:
    AreYouThereState areYouThereState_;
    AreYouReadyControlState areYouReadyState_;
    LoginState loginState_;
};

}

}

}

#endif // ICOM_CONTROL_STATE_MACHINE_H