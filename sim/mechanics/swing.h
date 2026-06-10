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

#include "sim/core/rng.h"
#include "sim/mechanics/attack_table.h"
#include "sim/mechanics/damage.h"
#include "sim/mechanics/rage.h"

// One white swing, shared verbatim by the match runner and arena_dist so
// both consume identical RNG streams (spec M-009).

namespace arena {

// Per-(entity, subsystem) use counters. The damage counter advances only on
// contact outcomes, so table-only refactors cannot shift damage streams.
struct RngCursor {
    uint64_t table_seq = 0;
    uint64_t damage_seq = 0;
};

struct SwingResult {
    Outcome outcome = Outcome::Miss;
    int32_t roll_pm = 0;
    int32_t damage = 0;              // final, post-mitigation
    int32_t rage_attacker_deci = 0;  // uncapped gains; caller applies the cap
    int32_t rage_defender_deci = 0;
};

constexpr bool outcome_makes_contact(Outcome o) {
    return o == Outcome::Block || o == Outcome::Crit || o == Outcome::Hit;
}

inline SwingResult resolve_swing(const UnitSpec& attacker, const UnitSpec& defender,
                                 FacingClass facing, uint64_t seed, RngCursor& rng) {
    const AttackTable table = build_attack_table(attacker, defender, facing, true);
    SwingResult r;
    r.roll_pm = roll_myriad(seed, attacker.entity_id, RngSubsystem::swing_table, rng.table_seq++);
    r.outcome = table.classify(r.roll_pm);
    if (outcome_makes_contact(r.outcome)) {
        const int32_t weapon_roll =
            roll_range(seed, attacker.entity_id, RngSubsystem::weapon_damage, rng.damage_seq++,
                       attacker.weapon_min, attacker.weapon_max);
        r.damage = resolve_damage(r.outcome, weapon_roll, attacker, defender);
        r.rage_attacker_deci =
            rage_dealt_deci(r.damage, attacker.weapon_speed_ms, r.outcome == Outcome::Crit);
        r.rage_defender_deci = rage_taken_deci(r.damage);
    }
    return r;
}

} // namespace arena
