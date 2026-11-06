/*
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2026 Mooneer Salem
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
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

#include "RadeMemory.h"

namespace ezdv
{

namespace audio
{

namespace
{

bool HasRoom_(size_t bytes, size_t floor)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= floor + bytes + 15 &&
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= bytes + 15;
}

void* Align_(void* raw)
{
    return (void*)(((uintptr_t)raw + 15) & ~(uintptr_t)15);
}

}

RadeMemory::~RadeMemory()
{
    for (int t = 0; t < numTables_; t++)
    {
        for (int i = 0; i < tables_[t].count; i++)
        {
            tables_[t].layers[i].layer = tables_[t].saved[i];
        }
        heap_caps_free(tables_[t].saved);
    }
    for (int i = 0; i < numAllocs_; i++)
    {
        heap_caps_free(allocs_[i]);
    }
}

void* RadeMemory::alloc(size_t bytes, bool wantInternal, size_t floor, bool* gotInternal)
{
    void* raw = nullptr;
    if (wantInternal && HasRoom_(bytes, floor))
    {
        raw = heap_caps_malloc(bytes + 15, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (raw == nullptr)
    {
        raw = heap_caps_malloc(bytes + 15, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    assert(raw != nullptr && numAllocs_ < MAX_ALLOCS);
    allocs_[numAllocs_++] = raw;
    if (gotInternal != nullptr)
    {
        *gotInternal = esp_ptr_internal(raw);
    }
    return Align_(raw);
}

const void* RadeMemory::copyToInternal(const void* src, size_t bytes, size_t floor)
{
    if (!HasRoom_(bytes, floor) || numAllocs_ >= MAX_ALLOCS)
    {
        return nullptr;
    }
    void* raw = heap_caps_malloc(bytes + 15, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (raw == nullptr)
    {
        return nullptr;
    }
    allocs_[numAllocs_++] = raw;
    void* p = Align_(raw);
    memcpy(p, src, bytes);
    return p;
}

void RadeMemory::saveLayers(Q8Layer* layers, int count)
{
    for (int t = 0; t < numTables_; t++)
    {
        if (tables_[t].layers == layers)
        {
            return;
        }
    }
    assert(numTables_ < MAX_TABLES && count <= MAX_LAYERS);
    LinearLayer* saved = (LinearLayer*)heap_caps_malloc(sizeof(LinearLayer) * count, MALLOC_CAP_SPIRAM);
    assert(saved != nullptr);
    for (int i = 0; i < count; i++)
    {
        saved[i] = layers[i].layer;
    }
    tables_[numTables_++] = { layers, count, saved };
}

size_t RadeMemory::placeInternal(Q8Layer* layers, int count, bool (*pick)(const char* name), size_t floor)
{
    size_t placed = 0;
    saveLayers(layers, count);
    for (int i = 0; i < count; i++)
    {
        Q8Layer* q = &layers[i];
        LinearLayer* l = &q->layer;
        if (!pick(q->name))
        {
            continue;
        }

        size_t vec = sizeof(float) * l->nb_outputs;
        struct { const void** p; size_t bytes; } arrays[] = {
            { (const void**)&l->weights, q->weight_bytes },
            { (const void**)&l->float_weights, sizeof(float) * l->nb_inputs * l->nb_outputs },
            { (const void**)&l->bias, vec },
            { (const void**)&l->scale, vec },
        };
        for (auto& a : arrays)
        {
            if (*a.p == nullptr || a.bytes == 0 || esp_ptr_internal(*a.p))
            {
                continue;
            }
            const void* copy = copyToInternal(*a.p, a.bytes, floor);
            if (copy != nullptr)
            {
                *a.p = copy;
                placed += a.bytes;
            }
        }
    }
    return placed;
}

}

}
