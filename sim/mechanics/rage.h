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

// c(70) = 0.0091107836*70^2 + 3.225598133*70 + 4.2652911 = 274.7000, x10.
// Verified: cmangos-tbc Player::RewardRage, Player.cpp:2336 @ 009455e.
constexpr int32_t RAGE_C10_L70 = 2747;
constexpr int32_t RAGE_CAP_DECI = 1000;
constexpr int32_t RAGE_F2_HIT = 7;    // hit factor 3.5, x2 (Unit.cpp:945)
constexpr int32_t RAGE_F2_CRIT = 14;  // hit factor 7.0, x2 (Unit.cpp:943)

// Attacker rage from damage dealt: (D/c * 7.5 + hf) / 2 with the hit factor
// hf = floor(f * speed_seconds) truncated BEFORE halving, single truncation
// of the final deci value — matches the oracle's float pipeline exactly for
// tested values (spec M-006, ledger D-016).
// Misses/dodges/parries generate none here — DELIBERATE divergence from the
// oracle (ledger D-011/D-012); fully blocked 0-damage swings also give none.
constexpr int32_t rage_dealt_deci(int32_t damage, int32_t weapon_speed_ms, bool crit) {
    if (damage <= 0) return 0;
    const int64_t hf =
        static_cast<int64_t>(crit ? RAGE_F2_CRIT : RAGE_F2_HIT) * weapon_speed_ms / 2000;
    return static_cast<int32_t>((375LL * damage + 5 * hf * RAGE_C10_L70) / RAGE_C10_L70);
}

// Defender rage from damage taken: 2.5*D/c, in deci-rage
// (Player.cpp:2347 @ 009455e; D = post-block damage here, ledger D-013).
constexpr int32_t rage_taken_deci(int32_t damage) {
    if (damage <= 0) return 0;
    return static_cast<int32_t>(250LL * damage / RAGE_C10_L70);
}

constexpr int32_t add_rage_deci(int32_t current, int32_t gain) {
    return std::clamp(current + gain, 0, RAGE_CAP_DECI);
}

} // namespace arena
