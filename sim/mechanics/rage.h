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

#include <algorithm>
#include <cstdint>

// Rage generation (spec M-006). Rage is stored as deci-rage int32 in
// [0, 1000] (= 0..100 rage). Level-70 only in M0; scenario loading rejects
// other levels.

namespace arena {

constexpr int32_t RAGE_C10_L70 = 2747;  // c(70) = 274.7, x10. TODO(verify) spec M-006
constexpr int32_t RAGE_CAP_DECI = 1000;
constexpr int32_t RAGE_F2_HIT = 7;    // hit factor 3.5, x2. TODO(verify) spec M-006
constexpr int32_t RAGE_F2_CRIT = 14;  // hit factor 7.0, x2. TODO(verify) spec M-006

// Attacker rage from damage dealt: 15*D/(4c) + f*s/2, in deci-rage.
// Misses/dodges/parries (and fully blocked 0-damage swings) generate none.
constexpr int32_t rage_dealt_deci(int32_t damage, int32_t weapon_speed_ms, bool crit) {
    if (damage <= 0) return 0;
    const int64_t term_damage = 375LL * damage / RAGE_C10_L70;
    const int64_t term_speed =
        static_cast<int64_t>(crit ? RAGE_F2_CRIT : RAGE_F2_HIT) * weapon_speed_ms / 400;
    return static_cast<int32_t>(term_damage + term_speed);
}

// Defender rage from damage taken: 2.5*D/c, in deci-rage.
constexpr int32_t rage_taken_deci(int32_t damage) {
    if (damage <= 0) return 0;
    return static_cast<int32_t>(250LL * damage / RAGE_C10_L70);
}

constexpr int32_t add_rage_deci(int32_t current, int32_t gain) {
    return std::clamp(current + gain, 0, RAGE_CAP_DECI);
}

} // namespace arena
