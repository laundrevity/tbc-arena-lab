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

#include "sim/core/scenario.h"
#include "sim/mechanics/attack_table.h"

// N-trial distribution report (CLAUDE.md rule 6: statistical pass criteria).
// Statistics here are reporting-only and may use doubles; nothing feeds back
// into authoritative state.

namespace arena {

// 99% two-sided normal quantile for the binomial CI.
constexpr double DIST_Z99 = 2.5758293035489004;

struct OutcomeRow {
    Outcome outcome = Outcome::Miss;
    int32_t expected_pm = 0;   // table-builder range width
    uint64_t count = 0;
    double observed_rate = 0;
    double expected_rate = 0;
    double ci_half_width = 0;  // z * sqrt(p(1-p)/n)
    bool pass = false;         // |observed - expected| <= ci_half_width
};

struct DistReport {
    uint64_t n = 0;
    FacingClass facing = FacingClass::Front;
    AttackTable table;  // expected ranges, included in the JSON output
    std::array<OutcomeRow, OUTCOME_COUNT> rows{};
    bool all_pass = false;

    // Final damage over contact swings (block/crit/hit; includes fully
    // blocked zeros).
    uint64_t contact_count = 0;
    int32_t damage_min = 0;
    int32_t damage_max = 0;
    double damage_mean = 0;
    double damage_sd = 0;  // sample stddev over contact swings
    int32_t damage_p50 = 0;
    int32_t damage_p99 = 0;
    double damage_mean_per_swing = 0;  // over all N swings, misses as 0

    // Uncapped attacker rage gain per swing, deci-rage, over all N swings.
    double rage_att_mean_deci = 0;
    double rage_att_sd_deci = 0;
    int32_t rage_att_min_deci = 0;
    int32_t rage_att_max_deci = 0;
    double rage_def_mean_deci = 0;
    double rage_def_sd_deci = 0;
};

// Resolves N independent swings of the scenario's attacker against its
// defender with the same RNG streams the match runner would consume.
// This validates RNG + table sampling against the table builder itself,
// NOT external fidelity (the differential harness does that later).
DistReport run_distribution(const Scenario& sc, uint64_t seed, uint64_t n);

} // namespace arena
