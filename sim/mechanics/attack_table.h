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

enum class Outcome : int32_t {
    Miss = 0,
    Dodge,
    Parry,
    Glance,  // zero-width vs players, always (ledger D-001)
    Block,
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

// Pure function: stats + facing class -> ranges (spec M-001). Only the
// player-vs-player path is implemented in M0; is_player_target=false aborts
// loudly rather than returning silently-wrong mob math.
AttackTable build_attack_table(const UnitSpec& attacker, const UnitSpec& defender,
                               FacingClass facing, bool is_player_target);

} // namespace arena
