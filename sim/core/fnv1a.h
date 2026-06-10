/*
 * This file is part of tbc-arena-lab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// FNV-1a 64-bit over canonical byte serializations — never over raw struct
// memory (CLAUDE.md code conventions).

namespace arena {

constexpr uint64_t FNV1A_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV1A_PRIME  = 1099511628211ULL;

constexpr uint64_t fnv1a(const uint8_t* data, size_t len, uint64_t h = FNV1A_OFFSET) {
    for (size_t i = 0; i < len; ++i) {
        h = (h ^ data[i]) * FNV1A_PRIME;
    }
    return h;
}

constexpr uint64_t fnv1a(std::string_view s, uint64_t h = FNV1A_OFFSET) {
    for (char c : s) {
        h = (h ^ static_cast<uint8_t>(c)) * FNV1A_PRIME;
    }
    return h;
}

} // namespace arena
