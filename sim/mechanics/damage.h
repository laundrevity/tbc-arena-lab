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

#include "sim/mechanics/attack_table.h"

// Weapon damage pipeline (specs M-002..M-005). Pure integer math; flooring
// at each stage is a documented divergence (ledger D-004).

namespace arena {

// AP/14 per weapon-speed-second (spec M-002).
constexpr int32_t ap_bonus(int32_t attack_power, int32_t weapon_speed_ms) {
    return static_cast<int32_t>(static_cast<int64_t>(attack_power) * weapon_speed_ms / 14000);
}

// Level-70 armor reduction: DR = armor / (armor + 10557.5), capped at 75%,
// post-armor damage floored at 1 (spec M-004; cmangos-tbc
// Unit::CalcArmorReducedDamage, Unit.cpp:2445-2471 @ 009455e).
// Numerator/denominator doubled to clear the .5.
constexpr int32_t ARMOR_K2_L70 = 21115;     // 2 * 10557.5
constexpr int32_t ARMOR_KEPT_MIN_PCT = 25;  // 75% DR cap

constexpr int32_t apply_armor(int32_t damage, int32_t armor) {
    if (damage <= 0) return damage;
    if (armor <= 0) return damage;
    const int64_t kept = static_cast<int64_t>(damage) * ARMOR_K2_L70 /
                         (2 * static_cast<int64_t>(armor) + ARMOR_K2_L70);
    const int64_t floor_kept = static_cast<int64_t>(damage) * ARMOR_KEPT_MIN_PCT / 100;
    const int64_t reduced = kept > floor_kept ? kept : floor_kept;
    return static_cast<int32_t>(reduced > 1 ? reduced : 1);
}

constexpr int32_t MELEE_CRIT_MULT = 2;  // spec M-003

// Full pipeline for a contact outcome (block/crit/hit): armor first, then
// crit x2, then block-value subtraction floored at 0 (specs M-003, M-005).
constexpr int32_t resolve_damage(Outcome outcome, int32_t weapon_roll, const UnitSpec& attacker,
                                 const UnitSpec& defender) {
    int32_t dmg = weapon_roll + ap_bonus(attacker.attack_power, attacker.weapon_speed_ms);
    dmg = apply_armor(dmg, defender.armor);
    if (outcome == Outcome::Crit) dmg *= MELEE_CRIT_MULT;
    if (outcome == Outcome::Block) dmg = std::max(0, dmg - defender.block_value);
    return dmg;
}

} // namespace arena
