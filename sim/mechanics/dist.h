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
#include "sim/mechanics/abilities.h"
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

// --- Yellow (weapon-ability) distribution, spec M-012 (M5 harness item) ---

// Rate rows: 4 avoidance-die outcomes over n attacks, then crit|hit and
// block|hit over the hit count.
constexpr int32_t YELLOW_DIST_ROWS = 6;

struct YellowRateRow {
    const char* name = "";
    int32_t expected_pm = 0;
    uint64_t trials = 0;  // denominator: n for die rows, hits for |hit rows
    uint64_t count = 0;
    double observed_rate = 0;
    double expected_rate = 0;
    double ci_half_width = 0;
    bool pass = false;
};

struct YellowDistReport {
    uint64_t n = 0;
    bool normalized = false;
    int32_t flat_bonus = 0;
    FacingClass facing = FacingClass::Front;
    YellowTable table;       // expected per-myriad bounds (sim builder)
    int32_t crit_pm = 0;     // expected crit chance on hit
    int32_t block_pm = 0;    // expected partial-block chance on hit (gated)
    std::array<YellowRateRow, YELLOW_DIST_ROWS> rows{};
    bool all_pass = false;

    uint64_t hit_count = 0;  // contact attacks (incl. crit/blocked)
    int32_t damage_min = 0;  // over hits
    int32_t damage_max = 0;
    double damage_mean = 0;
    double damage_sd = 0;
    // Victim rage per attack (deci, uncapped; avoided attacks contribute 0).
    double rage_def_mean_deci = 0;
    double rage_def_sd_deci = 0;
};

// Resolves N independent yellow attacks (attacker -> defender) through
// resolve_yellow (M-012) with the engine's RNG streams. `normalized` and
// `flat_bonus` select the ability parameters (MS: true/210, HS: false/176).
// Loadout flags are irrelevant here — this is a math validator, not a match.
YellowDistReport run_yellow_distribution(const Scenario& sc, uint64_t seed, uint64_t n,
                                         bool normalized, int32_t flat_bonus);

} // namespace arena
