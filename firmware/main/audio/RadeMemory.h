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

#ifndef RADE_MEMORY_H
#define RADE_MEMORY_H

#include <cstddef>

extern "C"
{
#include "nnet_q8.h"
}

namespace ezdv
{

namespace audio
{

/**
 * Memory for RADE V2's receiver or transmitter while it runs, all freed
 * when this is destroyed.
 *
 * The models' weights are in PSRAM (FreeDVTask loads q8_models.bin there
 * when RADE V2 starts). The layers that run most often go faster from
 * internal RAM, where the other weights streaming through the cache can't
 * evict them: placeInternal() copies those there while there's room, and
 * the layer tables are put back as they were on destruction.
 */
class RadeMemory
{
public:
    RadeMemory() = default;
    ~RadeMemory();

    RadeMemory(const RadeMemory&) = delete;
    RadeMemory& operator=(const RadeMemory&) = delete;

    /**
     * 16-byte aligned memory: from internal RAM if wanted and that would
     * leave at least floor bytes of it free, otherwise from PSRAM. (ezDV
     * aborts on a failed allocation.)
     */
    void* alloc(size_t bytes, bool wantInternal, size_t floor, bool* gotInternal = nullptr);

    /**
     * Copies the arrays (int8 and float weights, biases, scales) of each of
     * the layers that pick() chooses to internal RAM, while that leaves at
     * least floor bytes of it free. Returns the bytes copied.
     */
    size_t placeInternal(Q8Layer* layers, int count, bool (*pick)(const char* name), size_t floor);

    /**
     * Copies one array to internal RAM if that leaves at least floor bytes
     * free. Returns the copy, or nullptr. The caller points what used it at
     * the copy; the layer tables are put back on destruction anyway if
     * saveLayers() was called for them.
     */
    const void* copyToInternal(const void* src, size_t bytes, size_t floor);

    /** Remembers a layer table, to put it back as it is now on destruction. */
    void saveLayers(Q8Layer* layers, int count);

private:
    enum { MAX_ALLOCS = 128, MAX_TABLES = 4, MAX_LAYERS = 64 };

    void* allocs_[MAX_ALLOCS];
    int numAllocs_ = 0;

    struct SavedTable
    {
        Q8Layer* layers;
        int count;
        LinearLayer* saved; // in PSRAM
    };
    SavedTable tables_[MAX_TABLES];
    int numTables_ = 0;
};

}

}

#endif // RADE_MEMORY_H
