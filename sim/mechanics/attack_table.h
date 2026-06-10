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

#include <array>
#include <cstdint>

#include "sim/core/unit_state.h"

// Single-roll white-hit attack table (spec M-001).

namespace arena {

// Order matches the oracle die (cmangos-tbc Unit.h:628-640, ledger D-015):
// block precedes glance.
enum class Outcome : int32_t {
    Miss = 0,
    Dodge,
    Parry,
    Block,
    Glance,  // zero-width vs players, always (ledger D-001)
    Crit,
    Crush,   // zero-width vs players, always (ledger D-002)
    Hit,
};
constexpr int32_t OUTCOME_COUNT = 8;

const char* outcome_name(Outcome o);

enum class FacingClass : int32_t {
    Front = 0,
    Behind = 1,
};

struct AttackTable {
    // Per-outcome range widths in per-myriad, in table order (Miss..Hit).
    std::array<int32_t, OUTCOME_COUNT> width{};
    // Cumulative ends, clamped to 10000.
    std::array<int32_t, OUTCOME_COUNT> end{};

    // roll in [0, 10000).
    Outcome classify(int32_t roll) const {
        for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
            if (roll < end[i]) return static_cast<Outcome>(i);
        }
        return Outcome::Hit;  // unreachable: end[Hit] == 10000 by construction
    }
};

// Per-chance helpers (per-myriad, PvP path, spec M-001) shared by the white
// table builder and the yellow avoidance die (spec M-012) so the two cannot
// drift. `delta4` terms use defense_skill - weapon_skill.
constexpr int32_t pm_clamp0(int32_t v) { return v < 0 ? 0 : v; }
constexpr int32_t SKILL_DELTA_PM_PER_POINT = 4;  // 0.04%/point
constexpr int32_t BASE_MISS_PM = 500;            // 5.00% base vs players

constexpr int32_t chance_miss_pm(const UnitSpec& att, const UnitSpec& def) {
    return pm_clamp0(BASE_MISS_PM +
                     (def.defense_skill - att.weapon_skill) * SKILL_DELTA_PM_PER_POINT -
                     att.hit_pm);
}
constexpr int32_t chance_dodge_pm(const UnitSpec& att, const UnitSpec& def) {
    return pm_clamp0(def.dodge_pm +
                     (def.defense_skill - att.weapon_skill) * SKILL_DELTA_PM_PER_POINT);
}
constexpr int32_t chance_parry_pm(const UnitSpec& att, const UnitSpec& def) {
    return pm_clamp0(def.parry_pm +
                     (def.defense_skill - att.weapon_skill) * SKILL_DELTA_PM_PER_POINT);
}
constexpr int32_t chance_block_pm(const UnitSpec& att, const UnitSpec& def) {
    return pm_clamp0(def.block_pm +
                     (def.defense_skill - att.weapon_skill) * SKILL_DELTA_PM_PER_POINT);
}
// Crit vs players uses the attacker's LEVEL-CAPPED skill (5*level), not
// actual weapon skill (cmangos-tbc Unit.cpp:3957, spec M-001).
constexpr int32_t chance_crit_pm(const UnitSpec& att, const UnitSpec& def) {
    return pm_clamp0(att.crit_pm +
                     (5 * att.level - def.defense_skill) * SKILL_DELTA_PM_PER_POINT);
}

// Pure function: stats + facing class -> ranges (spec M-001). Only the
// player-vs-player path is implemented in M0; is_player_target=false aborts
// loudly rather than returning silently-wrong mob math.
AttackTable build_attack_table(const UnitSpec& attacker, const UnitSpec& defender,
                               FacingClass facing, bool is_player_target);

} // namespace arena
