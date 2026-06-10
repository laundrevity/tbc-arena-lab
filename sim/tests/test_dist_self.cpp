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

#include "sim/mechanics/damage.h"
#include "sim/mechanics/dist.h"

using namespace arena;

namespace {

Scenario load(const char* name) {
    Scenario sc;
    std::string err;
    const bool ok = load_scenario(std::string(REPO_ROOT) + "/scenarios/" + name, sc, err);
    REQUIRE_MESSAGE(ok, err);
    return sc;
}

constexpr uint64_t N = 1000000;

} // namespace

// SELF-test: validates RNG uniformity + table sampling against our own table
// builder at N=10^6 within the 99% binomial CI. It does NOT establish
// external fidelity — that is the differential harness's job (next session).
TEST_CASE("dist self-test (RNG+table sampling, not external fidelity): m0_front_shield N=1e6") {
    const Scenario sc = load("m0_front_shield.yaml");
    const DistReport r = run_distribution(sc, 9001, N);
    CHECK(r.facing == FacingClass::Front);
    for (const OutcomeRow& row : r.rows) {
        CAPTURE(outcome_name(row.outcome));
        CHECK(row.pass);
    }
    CHECK(r.all_pass);
    // PvP exclusions hold at runtime, not just in the table (D-001/D-002).
    CHECK(r.rows[static_cast<size_t>(Outcome::Glance)].count == 0);
    CHECK(r.rows[static_cast<size_t>(Outcome::Crush)].count == 0);
    // Damage extremes match the closed-form pipeline bounds.
    const int32_t min_blocked = resolve_damage(Outcome::Block, sc.attacker.weapon_min,
                                               sc.attacker, sc.defender);
    const int32_t max_crit = resolve_damage(Outcome::Crit, sc.attacker.weapon_max,
                                            sc.attacker, sc.defender);
    CHECK(r.damage_min >= min_blocked);
    CHECK(r.damage_max <= max_crit);
    CHECK(r.damage_p50 >= r.damage_min);
    CHECK(r.damage_p99 <= r.damage_max);
    CHECK(r.rage_att_min_deci == 0);  // misses generate no rage
}

TEST_CASE("dist self-test (RNG+table sampling, not external fidelity): m0_behind N=1e6") {
    const Scenario sc = load("m0_behind.yaml");
    const DistReport r = run_distribution(sc, 9002, N);
    CHECK(r.facing == FacingClass::Behind);
    CHECK(r.all_pass);
    // Behind: dodge/parry/block must never fire (spec M-008).
    CHECK(r.rows[static_cast<size_t>(Outcome::Dodge)].count == 0);
    CHECK(r.rows[static_cast<size_t>(Outcome::Parry)].count == 0);
    CHECK(r.rows[static_cast<size_t>(Outcome::Block)].count == 0);
    CHECK(r.rows[static_cast<size_t>(Outcome::Glance)].count == 0);
    CHECK(r.rows[static_cast<size_t>(Outcome::Crush)].count == 0);
    // Without block, the damage floor is an ordinary minimum-roll hit.
    const int32_t min_hit = resolve_damage(Outcome::Hit, sc.attacker.weapon_min, sc.attacker,
                                           sc.defender);
    CHECK(r.damage_min == min_hit);
}
