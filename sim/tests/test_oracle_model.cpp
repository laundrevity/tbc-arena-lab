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

#include "doctest.h"

#include <string>

#include "sim/core/scenario.h"
#include "sim/mechanics/attack_table.h"
#include "sim/oracle/cmangos_model.h"

using namespace arena;

namespace {

Scenario load(const char* name) {
    Scenario sc;
    std::string err;
    const bool ok = load_scenario(std::string(REPO_ROOT) + "/scenarios/" + name, sc, err);
    REQUIRE_MESSAGE(ok, err);
    return sc;
}

} // namespace

// The independently-ported oracle table must agree with our table builder at
// the pinned fixtures (both are audited against the same cmangos functions;
// disagreement means one port drifted).
TEST_CASE("oracle model: table widths equal sim table at both fixtures") {
    for (const char* name : {"m0_front_shield.yaml", "m0_behind.yaml"}) {
        CAPTURE(name);
        const Scenario sc = load(name);
        const bool gate = std::string(name) == "m0_front_shield.yaml";
        const AttackTable sim = build_attack_table(
            sc.attacker, sc.defender, gate ? FacingClass::Front : FacingClass::Behind, true);
        const auto ora = oracle::build_table(sc.attacker, sc.defender, gate);
        for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
            CAPTURE(outcome_name(static_cast<Outcome>(i)));
            CHECK(sim.width[static_cast<size_t>(i)] == ora[static_cast<size_t>(i)]);
        }
    }
}

// Synthetic skill deltas: the two ports must keep agreeing where formulas
// were aligned during the audit (crit level-cap, 0.04/pt steps).
TEST_CASE("oracle model: table agreement holds under synthetic skill deltas") {
    Scenario sc = load("m0_front_shield.yaml");
    for (int32_t defense : {340, 350, 355, 360}) {
        for (int32_t weapon : {345, 350, 355}) {
            CAPTURE(defense);
            CAPTURE(weapon);
            sc.defender.defense_skill = defense;
            sc.attacker.weapon_skill = weapon;
            const AttackTable sim =
                build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
            const auto ora = oracle::build_table(sc.attacker, sc.defender, true);
            for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
                CHECK(sim.width[static_cast<size_t>(i)] == ora[static_cast<size_t>(i)]);
            }
        }
    }
}

TEST_CASE("oracle model: Monte Carlo is deterministic for a fixed seed") {
    const Scenario sc = load("m0_front_shield.yaml");
    const auto table = oracle::build_table(sc.attacker, sc.defender, true);
    const oracle::McReport a = oracle::run_mc(sc.attacker, sc.defender, table, 99, 50000);
    const oracle::McReport b = oracle::run_mc(sc.attacker, sc.defender, table, 99, 50000);
    CHECK(a.contact_count == b.contact_count);
    CHECK(a.damage_mean == b.damage_mean);
    CHECK(a.rage_att_true_mean == b.rage_att_true_mean);
    CHECK(a.damage_min == b.damage_min);
    CHECK(a.damage_max == b.damage_max);
}

// Sanity bounds on the oracle pipeline at the pinned stats: post-armor
// damage of a min/max roll brackets the MC extremes, and the true-rage mean
// exceeds the ours-basis mean (the oracle also grants rage on avoids,
// D-011/D-012, and uses a fatter basis on blocks, D-013).
TEST_CASE("oracle model: MC sanity at m0_front_shield") {
    const Scenario sc = load("m0_front_shield.yaml");
    const auto table = oracle::build_table(sc.attacker, sc.defender, true);
    const oracle::McReport r = oracle::run_mc(sc.attacker, sc.defender, table, 7, 200000);
    CHECK(r.contact_count > 0);
    // Min possible: blocked minimum roll; max possible: crit of maximum roll.
    CHECK(r.damage_min >= 470);   // ~ (1040 * 0.63) - 180, generous bracket
    CHECK(r.damage_max <= 1512);
    CHECK(r.damage_mean > 800);
    CHECK(r.damage_mean < 1000);
    CHECK(r.rage_att_true_mean > r.rage_att_ours_mean);
    CHECK(r.rage_def_true_mean >= r.rage_def_ours_mean);
}
