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
#include "sim/mechanics/attack_table.h"

// Independent model of cmangos-tbc white-swing math for the differential
// harness (arena_diff). The math here is PORTED (rewritten, not copied) from
// cmangos-tbc @ commit 009455e under GPLv2, per-function citations below and
// in the .cpp — see CLAUDE.md rule 7. It intentionally reproduces the
// oracle's FLOAT pipeline, including its truncation points and its rage
// bookkeeping quirks (ledger D-011..D-014), so the harness can quantify
// exactly where this simulator diverges. NOTHING in sim/core or
// sim/mechanics may include this; floats here never touch authoritative
// state (the harness is diagnostic-only).

namespace arena::oracle {

// Per-myriad widths in the same Outcome order as the sim table
// (miss, dodge, parry, block, glance, crit, crush, hit), built per
// Unit::RollMeleeOutcomeAgainst (Unit.cpp:2821) + the CalculateEffective*
// chance functions. facing_gate is IsFacingTargetsFront (mutual arc).
std::array<int32_t, OUTCOME_COUNT> build_table(const UnitSpec& attacker, const UnitSpec& defender,
                                               bool facing_gate);

// Monte Carlo of the oracle's swing pipeline. Deterministic: keyed by
// (seed, attacker.entity_id, oracle_table / oracle_damage subsystems).
struct McReport {
    uint64_t n = 0;
    std::array<uint64_t, OUTCOME_COUNT> outcome_counts{};

    // Final damage over contact swings (block/crit/hit, incl. full-block 0s).
    uint64_t contact_count = 0;
    int32_t damage_min = 0;
    int32_t damage_max = 0;
    double damage_mean = 0;
    double damage_sd = 0;

    // Attacker rage per swing, deci, uncapped — two bases:
    // "ours_basis": oracle ARITHMETIC restricted to this simulator's
    // conventions (contact outcomes only, post-block damage). Comparing this
    // against arena_dist isolates arithmetic fidelity from the ledgered
    // basis divergences.
    double rage_att_ours_mean = 0;
    double rage_att_ours_sd = 0;
    // "true": the oracle's actual behavior incl. rage on miss (D-011),
    // doubled clean damage on dodge/parry (D-012), pre-block+blocked basis
    // on block (D-013). Reported as informational divergence.
    double rage_att_true_mean = 0;

    // Defender rage per swing, deci, uncapped, same two bases (true basis
    // differs on blocks, D-013; dodge/parry excluded in both, Unit.cpp:2297).
    double rage_def_ours_mean = 0;
    double rage_def_ours_sd = 0;
    double rage_def_true_mean = 0;
};

McReport run_mc(const UnitSpec& attacker, const UnitSpec& defender,
                const std::array<int32_t, OUTCOME_COUNT>& table, uint64_t seed, uint64_t n);

} // namespace arena::oracle
