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

#include "util/klib/khash.h"

extern "C"
{
    KHASH_INIT2(publish_map, klib_unused, khint64_t, void*, 1, kh_int64_hash_func, kh_int64_hash_equal);
    KHASH_INIT2(event_map, klib_unused, khint64_t, void*, 1, kh_int64_hash_func, kh_int64_hash_equal);
}