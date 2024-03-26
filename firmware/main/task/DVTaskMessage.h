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
 
#ifndef DV_TASK_MESSAGE_H
#define DV_TASK_MESSAGE_H

#include <cstdint>
#include <type_traits>

using DVEventBaseType = const char*;
#define DV_EVENT_DECLARE_BASE(NAME) extern DVEventBaseType NAME
#define DV_EVENT_DEFINE_BASE(NAME) DVEventBaseType NAME = #NAME

namespace ezdv
{

namespace task
{

class DVTaskMessage
{
public:
    DVTaskMessage() = default;
    virtual ~DVTaskMessage() = default;

    virtual uint32_t getSize() const = 0;
    virtual DVEventBaseType getEventBase() const = 0;
    virtual int32_t getEventType() const = 0;

    virtual uint64_t getEventPair() { return ((uint64_t)getEventBase() << 32) | (getEventType()); }
};

template<uint32_t EVENT_TYPE_ID, typename MessageType>
class DVTaskMessageBase : public DVTaskMessage
{
public:
    DVTaskMessageBase(DVEventBaseType base)
        : base_(base)
    {
        // empty
    }

    virtual ~DVTaskMessageBase() = default;

    virtual DVEventBaseType getEventBase() const override
    {
        return base_;
    }

    virtual int32_t getEventType() const override
    {
        return EVENT_TYPE_ID;
    }
    
    virtual uint32_t getSize() const override
    {
        return sizeof(MessageType);
    }

private:
    const DVEventBaseType base_;
};

}

}

#endif // DV_TASK_MESSAGE_H