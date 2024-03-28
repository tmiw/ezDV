/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2024 Mooneer Salem
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

#ifndef OBJECT_LIST_H
#define OBJECT_LIST_H

namespace ezdv
{

namespace util
{

template<typename InnerType>
struct ObjectList
{
public:
    int count;
    int blockSize;
    InnerType* mem;

    ObjectList()
    : count(0)
    , blockSize(0)
    , mem(nullptr)
    {
        resize_();
    }

    ~ObjectList()
    {
        heap_caps_free(mem);
    }

    void append(InnerType obj)
    {
        mem[count++] = obj;
        if (count == blockSize)
        {
            resize_();
        }
    }

    void* operator new  ( std::size_t count )
    {
        return heap_caps_calloc(1, count, MALLOC_CAP_SPIRAM);
    }

    void operator delete  ( void* ptr )
    {
        heap_caps_free(ptr);
    }

private:
    void resize_()
    {
        blockSize = blockSize == 0 ? 1 : (blockSize << 1);
        if (mem == nullptr)
        {
            mem = (InnerType*)heap_caps_calloc(blockSize, sizeof(InnerType), MALLOC_CAP_SPIRAM);
        }
        else
        {
            mem = (InnerType*)heap_caps_realloc(mem, blockSize * sizeof(InnerType), MALLOC_CAP_SPIRAM);
        }
        assert(mem != nullptr);
    }
};

}
}

#endif // OBJECT_LIST_H