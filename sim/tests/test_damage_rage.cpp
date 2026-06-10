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

#include "sim/mechanics/damage.h"
#include "sim/mechanics/rage.h"

using namespace arena;

namespace {

UnitSpec pinned_attacker() {
    UnitSpec u;
    u.entity_id = 1;
    u.attack_power = 2800;
    u.weapon_min = 320;
    u.weapon_max = 480;
    u.weapon_speed_ms = 3600;
    return u;
}

UnitSpec pinned_defender() {
    UnitSpec u;
    u.entity_id = 2;
    u.armor = 6200;
    u.block_value = 180;
    return u;
}

} // namespace

// Spec M-002: AP/14 per weapon-speed-second, floored once.
TEST_CASE("damage: attack-power bonus at fixed inputs") {
    CHECK(ap_bonus(2800, 3600) == 720);  // 2800/14 * 3.6 exactly
    CHECK(ap_bonus(0, 3600) == 0);
    CHECK(ap_bonus(2800, 2600) == 520);
    CHECK(ap_bonus(1000, 1500) == 107);  // floor(107.14...)
}

// Spec M-004: armor 6200 keeps 21115/33515 of damage; 75% DR cap.
TEST_CASE("damage: armor reduction at fixed inputs") {
    CHECK(apply_armor(1200, 6200) == 756);  // floor(1200*21115/33515)
    CHECK(apply_armor(1120, 6200) == 705);
    CHECK(apply_armor(1000, 0) == 1000);
    // DR cap: armor 60000 would keep ~14.96%; clamped to 25%.
    CHECK(apply_armor(1000, 60000) == 250);
    // Post-armor damage floors at 1 (spec M-004, Unit.cpp:2470).
    CHECK(apply_armor(2, 60000) == 1);
    CHECK(apply_armor(0, 60000) == 0);
}

// Specs M-003/M-005: pipeline order armor -> crit x2 -> block value.
TEST_CASE("damage: full pipeline for hit, crit and block at fixed inputs") {
    const UnitSpec att = pinned_attacker();
    const UnitSpec def = pinned_defender();
    // weapon roll 400 + AP bonus 720 = 1120; after armor 705.
    CHECK(resolve_damage(Outcome::Hit, 400, att, def) == 705);
    CHECK(resolve_damage(Outcome::Crit, 400, att, def) == 1410);   // 705 * 2
    CHECK(resolve_damage(Outcome::Block, 400, att, def) == 525);   // 705 - 180
    // Block floors at zero (overpinned block value).
    UnitSpec wall = def;
    wall.block_value = 100000;
    CHECK(resolve_damage(Outcome::Block, 400, att, wall) == 0);
}

// Spec M-006: deci-rage at fixed inputs (c10 = 2747; oracle arithmetic:
// hf truncated before halving, single final truncation).
TEST_CASE("rage: dealt and taken at fixed inputs (oracle arithmetic)") {
    // Hit for 705 at 3.6s: hf = floor(3.5*3.6) = 12;
    // floor((375*705 + 5*12*2747)/2747) = floor(429195/2747) = 156.
    // Oracle float check: (705/274.7*7.5 + 12)/2 = 15.624 rage -> 156 deci.
    CHECK(rage_dealt_deci(705, 3600, false) == 156);
    // Crit for 1410: hf = floor(7*3.6) = 25;
    // floor((528750 + 343375)/2747) = floor(872125/2747) = 317.
    CHECK(rage_dealt_deci(1410, 3600, true) == 317);
    // Blocked hit for 525 (post-block damage, hit factor 3.5):
    // floor((196875 + 164820)/2747) = 131.
    CHECK(rage_dealt_deci(525, 3600, false) == 131);
    // Taken 705: floor(250*705/2747) = 64.
    CHECK(rage_taken_deci(705) == 64);
    // No damage, no rage (miss/dodge/parry/fully-blocked) — deliberate
    // divergence from the oracle (D-011/D-012).
    CHECK(rage_dealt_deci(0, 3600, false) == 0);
    CHECK(rage_taken_deci(0) == 0);
}

// Spec M-006: rage is clamped to [0, 1000] deci-rage.
TEST_CASE("rage: cap at 100 rage (1000 deci)") {
    CHECK(add_rage_deci(950, 159) == 1000);
    CHECK(add_rage_deci(0, 5000) == 1000);
    CHECK(add_rage_deci(500, 0) == 500);
    CHECK(add_rage_deci(10, -50) == 0);
}
