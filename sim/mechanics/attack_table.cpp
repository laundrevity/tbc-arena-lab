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

#include "sim/mechanics/attack_table.h"

#include <cstdio>
#include <cstdlib>

namespace arena {

const char* outcome_name(Outcome o) {
    switch (o) {
        case Outcome::Miss: return "miss";
        case Outcome::Dodge: return "dodge";
        case Outcome::Parry: return "parry";
        case Outcome::Glance: return "glance";
        case Outcome::Block: return "block";
        case Outcome::Crit: return "crit";
        case Outcome::Crush: return "crush";
        case Outcome::Hit: return "hit";
    }
    return "?";
}

namespace {

constexpr int32_t BASE_MISS_PM = 500;          // TODO(verify) spec M-001
constexpr int32_t SKILL_DELTA_PM_PER_POINT = 4; // 0.04%/point, TODO(verify) spec M-001

constexpr int32_t clamp0(int32_t v) { return v < 0 ? 0 : v; }

} // namespace

AttackTable build_attack_table(const UnitSpec& attacker, const UnitSpec& defender,
                               FacingClass facing, bool is_player_target) {
    if (!is_player_target) {
        // Mob path (glancing/crushing/level-delta miss) is out of M0 scope;
        // fail loudly instead of returning silently-wrong math.
        fprintf(stderr, "build_attack_table: is_player_target=false is not implemented in M0\n");
        abort();
    }

    const int32_t delta_pm =
        (defender.defense_skill - attacker.weapon_skill) * SKILL_DELTA_PM_PER_POINT;
    const bool front = facing == FacingClass::Front;

    AttackTable t;
    t.width[static_cast<int32_t>(Outcome::Miss)] =
        clamp0(BASE_MISS_PM + delta_pm - attacker.hit_pm);
    // Dodge, parry and block are all gated on the mutual facing check
    // (spec M-008); block additionally needs a shield.
    t.width[static_cast<int32_t>(Outcome::Dodge)] =
        front ? clamp0(defender.dodge_pm + delta_pm) : 0;
    t.width[static_cast<int32_t>(Outcome::Parry)] =
        front ? clamp0(defender.parry_pm + delta_pm) : 0;
    t.width[static_cast<int32_t>(Outcome::Block)] =
        (front && defender.has_shield) ? clamp0(defender.block_pm + delta_pm) : 0;
    t.width[static_cast<int32_t>(Outcome::Glance)] = 0;  // never vs players (D-001)
    // Crit vs players uses the attacker's LEVEL-CAPPED skill (5*level), not
    // actual weapon skill (cmangos-tbc Unit.cpp:3957, spec M-001).
    t.width[static_cast<int32_t>(Outcome::Crit)] =
        clamp0(attacker.crit_pm +
               (5 * attacker.level - defender.defense_skill) * SKILL_DELTA_PM_PER_POINT);
    t.width[static_cast<int32_t>(Outcome::Crush)] = 0;  // never vs players (D-002)

    // Hit takes the remainder; if earlier rows overflow 10000 the table is
    // clamped left to right (spec M-001 defines this in one place).
    int32_t acc = 0;
    for (int32_t i = 0; i < OUTCOME_COUNT - 1; ++i) {
        if (acc + t.width[i] > 10000) t.width[i] = 10000 - acc;
        acc += t.width[i];
        t.end[i] = acc;
    }
    t.width[static_cast<int32_t>(Outcome::Hit)] = 10000 - acc;
    t.end[static_cast<int32_t>(Outcome::Hit)] = 10000;
    return t;
}

} // namespace arena
