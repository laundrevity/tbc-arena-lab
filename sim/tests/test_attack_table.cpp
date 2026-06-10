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

#include "sim/core/fixed_trig.h"
#include "sim/core/scenario.h"
#include "sim/mechanics/attack_table.h"

using namespace arena;

namespace {

Scenario load(const char* name) {
    Scenario sc;
    std::string err;
    const bool ok = load_scenario(std::string(REPO_ROOT) + "/scenarios/" + name, sc, err);
    REQUIRE_MESSAGE(ok, err);
    return sc;
}

int32_t w(const AttackTable& t, Outcome o) { return t.width[static_cast<size_t>(o)]; }

} // namespace

// Spec M-001: exact ranges at the pinned m0_front_shield stats.
TEST_CASE("attack table: m0_front_shield ranges (full PvP table)") {
    const Scenario sc = load("m0_front_shield.yaml");
    const AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Miss) == 500);    // 5.00% base, equal skill, no +hit
    CHECK(w(t, Outcome::Dodge) == 800);
    CHECK(w(t, Outcome::Parry) == 600);
    CHECK(w(t, Outcome::Glance) == 0);    // never vs players (D-001)
    CHECK(w(t, Outcome::Block) == 500);
    CHECK(w(t, Outcome::Crit) == 2500);
    CHECK(w(t, Outcome::Crush) == 0);     // never vs players (D-002)
    CHECK(w(t, Outcome::Hit) == 5100);
    CHECK(t.end[static_cast<size_t>(Outcome::Hit)] == 10000);
}

// Spec M-001/M-008: behind the defender only miss/crit/hit remain.
TEST_CASE("attack table: m0_behind ranges (positional gating zeroes avoidance)") {
    const Scenario sc = load("m0_behind.yaml");
    // Defender chances are pinned nonzero in the fixture; the facing gate
    // alone must produce the zero widths.
    CHECK(sc.defender.dodge_pm == 800);
    CHECK(sc.defender.parry_pm == 600);
    CHECK(sc.defender.block_pm == 500);
    const AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Behind, true);
    CHECK(w(t, Outcome::Miss) == 500);
    CHECK(w(t, Outcome::Dodge) == 0);
    CHECK(w(t, Outcome::Parry) == 0);
    CHECK(w(t, Outcome::Glance) == 0);
    CHECK(w(t, Outcome::Block) == 0);
    CHECK(w(t, Outcome::Crit) == 2500);
    CHECK(w(t, Outcome::Crush) == 0);
    CHECK(w(t, Outcome::Hit) == 7000);
}

TEST_CASE("attack table: block needs a shield even from the front") {
    Scenario sc = load("m0_front_shield.yaml");
    sc.defender.has_shield = false;
    const AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Block) == 0);
    CHECK(w(t, Outcome::Hit) == 5600);
}

// Spec M-001: +/-0.04% per point of defense minus weapon skill (synthetic
// stats; both M0 fixtures pin 350/350 so this path is otherwise unexercised,
// ledger D-009).
TEST_CASE("attack table: defense vs weapon skill delta shifts ranges") {
    Scenario sc = load("m0_front_shield.yaml");

    sc.defender.defense_skill = 360;  // +10 defense => +0.4% to each, crit -0.4%
    AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Miss) == 540);
    CHECK(w(t, Outcome::Dodge) == 840);
    CHECK(w(t, Outcome::Parry) == 640);
    CHECK(w(t, Outcome::Block) == 540);
    CHECK(w(t, Outcome::Crit) == 2460);

    sc.defender.defense_skill = 350;
    sc.attacker.weapon_skill = 355;  // attacker ahead: avoidance down, crit up
    t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Miss) == 480);
    CHECK(w(t, Outcome::Dodge) == 780);
    CHECK(w(t, Outcome::Parry) == 580);
    CHECK(w(t, Outcome::Block) == 480);
    CHECK(w(t, Outcome::Crit) == 2520);
}

TEST_CASE("attack table: hit rating eats miss, floored at zero") {
    Scenario sc = load("m0_front_shield.yaml");
    sc.attacker.hit_pm = 300;
    AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Miss) == 200);
    sc.attacker.hit_pm = 900;
    t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Miss) == 0);
}

// Spec M-001: over-full tables clamp left to right; hit (then crit) squeezed.
TEST_CASE("attack table: over-full table clamps left to right") {
    Scenario sc = load("m0_front_shield.yaml");
    sc.defender.dodge_pm = 6000;
    sc.defender.parry_pm = 4000;
    const AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(w(t, Outcome::Miss) == 500);
    CHECK(w(t, Outcome::Dodge) == 6000);
    CHECK(w(t, Outcome::Parry) == 3500);  // truncated at the 10000 boundary
    CHECK(w(t, Outcome::Block) == 0);
    CHECK(w(t, Outcome::Crit) == 0);
    CHECK(w(t, Outcome::Hit) == 0);
    CHECK(t.end[static_cast<size_t>(Outcome::Hit)] == 10000);
}

TEST_CASE("attack table: classify maps rolls to range boundaries exactly") {
    const Scenario sc = load("m0_front_shield.yaml");
    const AttackTable t = build_attack_table(sc.attacker, sc.defender, FacingClass::Front, true);
    CHECK(t.classify(0) == Outcome::Miss);
    CHECK(t.classify(499) == Outcome::Miss);
    CHECK(t.classify(500) == Outcome::Dodge);
    CHECK(t.classify(1299) == Outcome::Dodge);
    CHECK(t.classify(1300) == Outcome::Parry);
    CHECK(t.classify(1899) == Outcome::Parry);
    CHECK(t.classify(1900) == Outcome::Block);
    CHECK(t.classify(2399) == Outcome::Block);
    CHECK(t.classify(2400) == Outcome::Crit);
    CHECK(t.classify(4899) == Outcome::Crit);
    CHECK(t.classify(4900) == Outcome::Hit);
    CHECK(t.classify(9999) == Outcome::Hit);
}

// Spec M-008: frontal arc is the +/- pi/2 half-plane; ties count as front.
TEST_CASE("frontal arc: cardinal and diagonal placements") {
    // Defender at origin facing +x.
    CHECK(in_frontal_arc(0, 0, 0, 200, 0));        // dead ahead
    CHECK(!in_frontal_arc(0, 0, 0, -200, 0));      // dead behind
    CHECK(in_frontal_arc(0, 0, 0, 200, 200));      // front-left diagonal
    CHECK(in_frontal_arc(0, 0, 0, 200, -200));     // front-right diagonal
    CHECK(!in_frontal_arc(0, 0, 0, -200, 200));    // rear diagonals
    CHECK(!in_frontal_arc(0, 0, 0, -200, -200));
    // Facing pi (3142 mrad): the +x side is now behind.
    CHECK(!in_frontal_arc(0, 0, 3142, 200, 0));
    CHECK(in_frontal_arc(0, 0, 3142, -200, 0));
    // Facing +y (pi/2): +y is front, -y is behind.
    CHECK(in_frontal_arc(0, 0, 1571, 0, 200));
    CHECK(!in_frontal_arc(0, 0, 1571, 0, -200));
    // Tie: exactly on the shoulder line counts as front (spec M-008).
    CHECK(in_frontal_arc(0, 0, 0, 0, 200));
    CHECK(in_frontal_arc(0, 0, 0, 0, -200));
}

// The scenario fixtures themselves resolve to the intended facing classes.
TEST_CASE("frontal arc: scenario placements classify as pinned") {
    const Scenario front = load("m0_front_shield.yaml");
    CHECK(in_frontal_arc(front.defender.pos_x_cm, front.defender.pos_y_cm,
                         front.defender.facing_mrad, front.attacker.pos_x_cm,
                         front.attacker.pos_y_cm));
    const Scenario behind = load("m0_behind.yaml");
    CHECK(!in_frontal_arc(behind.defender.pos_x_cm, behind.defender.pos_y_cm,
                          behind.defender.facing_mrad, behind.attacker.pos_x_cm,
                          behind.attacker.pos_y_cm));
}
