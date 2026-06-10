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

#include <cstdint>

// Parry-haste (spec M-010): a unit that parries gets its own next main-hand
// swing accelerated. Ported from cmangos-tbc DealMeleeDamage,
// Unit.cpp:2277-2315 @ 009455e (main-hand path). Integer p20 = floor(S/5)
// vs the oracle's float 0.20f differs <=1 ms for speeds not divisible by 5
// (ledger D-017).

namespace arena {

constexpr int64_t parry_hastened_remaining(int64_t remaining_ms, int32_t weapon_speed_ms) {
    const int64_t p20 = weapon_speed_ms / 5;
    const int64_t p60 = 3 * p20;
    if (remaining_ms > p60) return remaining_ms - 2 * p20;
    if (remaining_ms > p20) return p20;
    return remaining_ms;
}

} // namespace arena
