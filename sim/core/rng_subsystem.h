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

namespace arena {

// The single registry of RNG subsystem IDs (CLAUDE.md rule 3). Every keyed
// roll names one of these; never reuse an ID for a different purpose.
enum class RngSubsystem : uint32_t {
    swing_table   = 0,  // white-hit attack-table roll (spec M-001)
    weapon_damage = 1,  // weapon min..max damage roll (spec M-002)
    oracle_table  = 2,  // arena_diff oracle-model Monte Carlo, table roll
    oracle_damage = 3,  // arena_diff oracle-model Monte Carlo, damage roll
};

} // namespace arena
