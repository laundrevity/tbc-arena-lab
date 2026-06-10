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

#include "sim/core/rng.h"
#include "sim/mechanics/attack_table.h"
#include "sim/mechanics/damage.h"
#include "sim/mechanics/rage.h"
#include "sim/mechanics/swing.h"

// Weapon abilities and the yellow hit pipeline (specs M-011..M-016).
// Ability constants are hand-authored DBC-derived values (no client data in
// this repo), TODO(verify) per their spec entries.

namespace arena {

constexpr int32_t GCD_MS = 1500;  // spec M-011

// Mortal Strike rank 6 (spell 30330), spec M-014.
constexpr int32_t MS_RAGE_COST_DECI = 300;
constexpr int32_t MS_COOLDOWN_MS = 6000;
constexpr int32_t MS_FLAT_BONUS = 210;

// Heroic Strike rank 10 (spell 30324), spec M-015. Off-GCD on-next-swing.
constexpr int32_t HS_RAGE_COST_DECI = 150;
constexpr int32_t HS_FLAT_BONUS = 176;

// --- Yellow hit resolution (spec M-012) ---

enum class YellowOutcome : int32_t {
    Miss = 0,
    Dodge,
    Parry,
    Hit,
};

const char* yellow_outcome_name(YellowOutcome o);

// Avoidance die widths (per-myriad): miss | dodge | parry | hit(remainder).
// No block side for normal strikes; no glance/crush on abilities, ever.
struct YellowTable {
    int32_t miss_end = 0;
    int32_t dodge_end = 0;
    int32_t parry_end = 0;  // >= parry_end is a hit

    YellowOutcome classify(int32_t roll) const {
        if (roll < miss_end) return YellowOutcome::Miss;
        if (roll < dodge_end) return YellowOutcome::Dodge;
        if (roll < parry_end) return YellowOutcome::Parry;
        return YellowOutcome::Hit;
    }
};

inline YellowTable build_yellow_table(const UnitSpec& attacker, const UnitSpec& defender,
                                      FacingClass facing) {
    const bool front = facing == FacingClass::Front;
    YellowTable t;
    int32_t acc = std::min(chance_miss_pm(attacker, defender), 10000);
    t.miss_end = acc;
    acc = std::min(acc + (front ? chance_dodge_pm(attacker, defender) : 0), 10000);
    t.dodge_end = acc;
    acc = std::min(acc + (front ? chance_parry_pm(attacker, defender) : 0), 10000);
    t.parry_end = acc;
    return t;
}

// Damage pipeline for a yellow hit (spec M-012): flat bonus + weapon roll +
// AP (normalized speed per M-013 or real speed), crit x2 BEFORE armor
// (D-020), then armor, then block-value subtraction floored at 0.
constexpr int32_t resolve_yellow_damage(int32_t weapon_roll, bool crit, bool blocked,
                                        const UnitSpec& attacker, const UnitSpec& defender,
                                        bool normalized, int32_t flat_bonus) {
    const int32_t speed_ms = normalized ? attacker.weapon_norm_ms : attacker.weapon_speed_ms;
    int32_t dmg = flat_bonus + weapon_roll + ap_bonus(attacker.attack_power, speed_ms);
    if (crit) dmg *= MELEE_CRIT_MULT;
    dmg = apply_armor(dmg, defender.armor);
    if (blocked) dmg = std::max(0, dmg - defender.block_value);
    return dmg;
}

struct YellowResult {
    YellowOutcome outcome = YellowOutcome::Miss;
    int32_t roll_pm = 0;
    bool crit = false;
    bool blocked = false;
    int32_t damage = 0;              // final, post-mitigation
    int32_t rage_defender_deci = 0;  // uncapped victim gain (M-016 / D-019)
};

// Crit, block and damage rolls are consumed only on hit (spec M-012/M-009).
inline YellowResult resolve_yellow(const UnitSpec& attacker, const UnitSpec& defender,
                                   FacingClass facing, uint64_t seed, RngCursor& rng,
                                   bool normalized, int32_t flat_bonus) {
    const YellowTable table = build_yellow_table(attacker, defender, facing);
    YellowResult r;
    r.roll_pm =
        roll_myriad(seed, attacker.entity_id, RngSubsystem::yellow_table, rng.yellow_table_seq++);
    r.outcome = table.classify(r.roll_pm);
    if (r.outcome != YellowOutcome::Hit) return r;

    r.crit = roll_myriad(seed, attacker.entity_id, RngSubsystem::yellow_crit,
                         rng.yellow_crit_seq++) < chance_crit_pm(attacker, defender);
    const bool can_block = facing == FacingClass::Front && defender.has_shield;
    if (can_block) {
        r.blocked = roll_myriad(seed, attacker.entity_id, RngSubsystem::yellow_block,
                                rng.yellow_block_seq++) < chance_block_pm(attacker, defender);
    }
    const int32_t weapon_roll =
        roll_range(seed, attacker.entity_id, RngSubsystem::yellow_damage, rng.yellow_damage_seq++,
                   attacker.weapon_min, attacker.weapon_max);
    r.damage = resolve_yellow_damage(weapon_roll, r.crit, r.blocked, attacker, defender,
                                     normalized, flat_bonus);
    r.rage_defender_deci = rage_taken_deci(r.damage);
    return r;
}

} // namespace arena
