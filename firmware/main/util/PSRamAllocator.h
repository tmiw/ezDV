/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
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

#ifndef PS_RAM_ALLOCATOR_H
#define PS_RAM_ALLOCATOR_H

#include "esp_heap_caps.h"

namespace ezdv
{

namespace util
{

template<typename T>
struct PSRamAllocator
{
    typedef T value_type;
    PSRamAllocator() noexcept { }
    
    template<class U> PSRamAllocator(const PSRamAllocator<U>&) noexcept {}
    template<class U> bool operator==(const PSRamAllocator<U>&) const noexcept
    {
        return true;
    }
    template<class U> bool operator!=(const PSRamAllocator<U>&) const noexcept
    {
        return false;
    }
    
    T* allocate(const size_t n) const noexcept
    {
        return (T*)heap_caps_malloc(n*sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    }
    
    void deallocate(T* const p, size_t) const noexcept
    {
        heap_caps_free(p);
    }
};

}

}

#endif // PS_RAM_ALLOCATOR_H